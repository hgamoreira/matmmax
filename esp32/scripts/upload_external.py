"""Upload an MSAP app over UART0, or to a fresh QEMU session, using stdlib only."""
import argparse
import errno
import fcntl
import os
from pathlib import Path
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import termios
import time
import tty
import zlib
from run_console import QEMU, qemu_command
from run_external import BUILD


class UploadError(Exception):
    pass


class Stream:
    def __init__(self, read_fd, write_fd, *, timeout=10):
        self.read_fd, self.write_fd, self.timeout = read_fd, write_fd, timeout
        self.pending = b""
        self.received = b""

    def write(self, data):
        deadline = time.monotonic()+self.timeout
        view = memoryview(data)
        while view:
            left = deadline-time.monotonic()
            if left <= 0 or not select.select([], [self.write_fd], [], left)[1]:
                raise UploadError("Timeout writing to workstation")
            try: n = os.write(self.write_fd, view)
            except BlockingIOError: continue
            if not n: raise UploadError("Workstation connection closed")
            view = view[n:]

    def until(self, marker):
        deadline = time.monotonic()+self.timeout
        while marker not in self.pending:
            left = deadline-time.monotonic()
            if left <= 0 or not select.select([self.read_fd], [], [], left)[0]:
                raise UploadError(f"Timeout waiting for {marker!r}; finish/abort any block or exit the active program first")
            try: data = os.read(self.read_fd, 65536)
            except BlockingIOError: continue
            if not data: raise UploadError("Workstation connection closed")
            self.pending += data
            self.received += data
            if len(self.pending) > 1048576:
                raise UploadError("Excessive unexpected workstation output")
        end = self.pending.index(marker)+len(marker)
        result, self.pending = self.pending[:end], self.pending[end:]
        return result

    def line(self):
        return self.until(b"\n").decode("ascii", errors="replace").strip()


def upload(stream, package, *, sync=True):
    if not 32 <= len(package) <= 262144:
        raise UploadError("App package must be 32..262144 bytes")
    if sync:
        stream.write(b"\r")
        stream.until(b">> ")
    stream.write(f":upload {len(package)} {zlib.crc32(package):08x}\r".encode())
    while True:
        line = stream.line()
        if line.startswith("ERROR"): raise UploadError(line)
        if line == "READY 256": break
    for offset in range(0, len(package), 256):
        chunk = package[offset:offset+256]
        stream.write(chunk)
        while True:
            line = stream.line()
            if line.startswith("ERROR"): raise UploadError(line)
            if line.startswith("ACK "):
                if line != f"ACK {offset+len(chunk)}":
                    raise UploadError("Unexpected upload acknowledgement: "+line)
                break
    while True:
        line = stream.line()
        if line.startswith("ERROR"): raise UploadError(line)
        if line == f"INSTALLED uploaded {len(package)}": return line


def console(stream):
    if not sys.stdin.isatty(): return
    print("Console connected. Ctrl+] closes this host session. Try :app uploaded.", flush=True)
    original = termios.tcgetattr(sys.stdin.fileno())
    try:
        tty.setraw(sys.stdin.fileno())
        if stream.pending:
            os.write(sys.stdout.fileno(), stream.pending)
            stream.pending = b""
        while True:
            ready, _, _ = select.select([sys.stdin.fileno(), stream.read_fd], [], [])
            if stream.read_fd in ready:
                try: data = os.read(stream.read_fd, 65536)
                except BlockingIOError: continue
                if not data: return
                os.write(sys.stdout.fileno(), data)
            if sys.stdin.fileno() in ready:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data or b"\x1d" in data: return
                stream.write(data)
    finally:
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, original)
        print()


def open_serial(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attributes = termios.tcgetattr(fd)
        original = list(attributes)
        original[6] = list(attributes[6])
        attributes[0] = attributes[1] = attributes[3] = 0
        attributes[2] = termios.CLOCAL | termios.CREAD | termios.CS8
        attributes[4] = attributes[5] = termios.B115200
        attributes[6][termios.VMIN] = attributes[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attributes)
        try:
            fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS))
        except OSError as error:
            if error.errno not in (errno.ENOTTY, errno.EINVAL): raise
        termios.tcflush(fd, termios.TCIFLUSH)
        return fd, original
    except BaseException:
        os.close(fd)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    connection = parser.add_mutually_exclusive_group(required=True)
    connection.add_argument("--port", help="CP2102 UART device, e.g. /dev/ttyUSB0")
    connection.add_argument("--qemu", action="store_true", help="upload to a fresh disposable emulator")
    parser.add_argument("--no-console", action="store_true")
    args = parser.parse_args()
    try:
        if not 32 <= args.package.stat().st_size <= 262144:
            raise UploadError("App package must be 32..262144 bytes")
        data = args.package.read_bytes()
        if args.qemu:
            if not (BUILD/"flash.bin").is_file():
                raise UploadError("Run esp32/scripts/external.sh build first")
            with tempfile.TemporaryDirectory(prefix="upload-", dir=BUILD) as directory:
                flash = Path(directory)/"flash.bin"
                shutil.copyfile(BUILD/"flash.bin", flash)
                process = subprocess.Popen(qemu_command(flash), stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
                try:
                    stream = Stream(process.stdout.fileno(), process.stdin.fileno())
                    # Do not inject UART input while the fresh ROM/SDK is booting.
                    stream.until(b">> ")
                    print(upload(stream, data, sync=False), flush=True)
                    if not args.no_console: console(stream)
                finally:
                    process.terminate()
                    try: process.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate(timeout=5)
        else:
            fd, original = open_serial(args.port)
            try:
                stream = Stream(fd, fd)
                print(upload(stream, data), flush=True)
                if not args.no_console: console(stream)
            finally:
                termios.tcsetattr(fd, termios.TCSANOW, original)
                os.close(fd)
    except (UploadError, OSError) as error:
        print("Upload failed: "+str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__": raise SystemExit(main())
