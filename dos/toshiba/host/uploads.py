"""Uploads from the C64 (protocol v1, /c64/v1/files/upload-*).

This is the only code in the server that writes files. Rules:
- targets are inside the storage root, in an existing folder, with a name
  the C64 can show and send (files.is_c64_name); the one exception is the
  "documents" folder of 03_wordproc, which the first upload into it creates;
- data goes to a hidden temporary file in the target folder
  (".c64upload-<id>.part", not listed to the C64) and is appended strictly in
  order;
- only upload-finish with the right size and CRC-16 puts it in place, by an
  atomic rename. Without "replace" an existing file is never touched; with it,
  the old file stays intact until the new one is complete and checked;
- abandoned uploads (idle UPLOAD_IDLE_SECONDS) are deleted; leftovers from an
  earlier server run are deleted at start-up.
"""
import os
import secrets
import struct
import threading
import time
from pathlib import Path

import files

UPLOAD_FILE_MAX = files.DOWNLOAD_FILE_MAX       # 16 MB
UPLOAD_CHUNK_MAX = 1000
RAW_CHUNK_MAX = 4096        # for a raw POST (the XT; docs/NET_PROTOCOL.md, XT additions)
UPLOAD_SESSIONS_MAX = 4
UPLOAD_IDLE_SECONDS = 10 * 60
TEMP_PREFIX = ".c64upload-"
TEMP_SUFFIX = ".part"

STATUS_EXISTS = 8           # target exists and replace was not allowed
STATUS_OUT_OF_ORDER = 9     # data offset is not the current end of the upload
STATUS_CHECK_FAILED = 10    # size or CRC-16 at upload-finish do not match
STATUS_UNKNOWN_UPLOAD = 11  # id not known (never started, finished, or expired)
STATUS_BUSY = 12            # too many uploads at once

UPLOAD_REPLY = struct.Struct("<BBII")   # version, status, upload id, bytes received so far

# The body the WiC64 firmware (2.1.0, httpClient.h) sends for HTTP_POST_DATA.
POST_HEADER = b'--WiC64-Binary-Data\nContent-Disposition: form-data; name="data"\r\n\r\n'
POST_FOOTER = b"\r\n--WiC64-Binary-Data--\r\n"
POST_BODY_MAX = len(POST_HEADER) + UPLOAD_CHUNK_MAX + len(POST_FOOTER)


def reply(status, upload_id=0, received=0):
    return UPLOAD_REPLY.pack(files.PROTOCOL_VERSION, status, upload_id & 0xFFFFFFFF, received & 0xFFFFFFFF)


def unwrap_post_body(body):
    """The chunk inside the WiC64's multipart envelope, or None."""
    if not (body.startswith(POST_HEADER) and body.endswith(POST_FOOTER)):
        return None
    return body[len(POST_HEADER):len(body) - len(POST_FOOTER)]


def wrap_post_body(chunk):
    """A raw chunk in the WiC64 envelope, so the XT's raw POST reaches the same code."""
    return POST_HEADER + chunk + POST_FOOTER


def remove_leftovers(root):
    """Delete temporary files from an earlier run (they are only ever ours)."""
    removed = 0
    for path in Path(root).rglob(TEMP_PREFIX + "*" + TEMP_SUFFIX):
        if path.is_file() and not path.is_symlink():
            path.unlink()
            removed += 1
    return removed


class Session:
    def __init__(self, upload_id, directory, name, replace):
        self.id = upload_id
        self.target = directory / name
        self.temp = directory / (TEMP_PREFIX + "%08x" % upload_id + TEMP_SUFFIX)
        self.replace = replace
        self.received = 0
        self.crc = 0xFFFF
        self.touched = time.monotonic()
        self.handle = open(self.temp, "xb")

    def discard(self):
        try:
            self.handle.close()
        finally:
            try:
                self.temp.unlink()
            except FileNotFoundError:
                pass


class Uploads:
    def __init__(self, root, log=None):
        self.root = Path(root).resolve()
        self.sessions = {}
        self.lock = threading.Lock()
        self.log = log

    def note(self, text, *args):
        if self.log:
            self.log.info("UPLOAD " + text, *args)

    def expire(self):
        now = time.monotonic()
        for upload_id, session in list(self.sessions.items()):
            if now - session.touched > UPLOAD_IDLE_SECONDS:
                session.discard()
                del self.sessions[upload_id]
                self.note("%08x abandoned, temporary file removed", upload_id)

    def target_check(self, directory_path, name, replace):
        """(status, directory) for a new upload."""
        if not directory_path == "" and not directory_path.endswith("/"):
            raise files.RequestError(files.STATUS_BAD_REQUEST)
        if len(directory_path) > files.PATH_MAX or not files.is_c64_name(name):
            raise files.RequestError(files.STATUS_BAD_REQUEST)
        documents = self.root / files.DOCUMENTS_FOLDER
        if directory_path == files.DOCUMENTS_FOLDER + "/" and not documents.exists() \
                and not documents.is_symlink():
            documents.mkdir()               # WordSmith's first save creates it
            self.note("created %s", documents)
        self.make_project_folder(directory_path)
        directory = files.resolve(self.root, directory_path)
        if not directory.is_dir():
            raise files.RequestError(files.STATUS_WRONG_TYPE)
        target = directory / name
        if target.is_symlink():
            raise files.RequestError(files.STATUS_ACCESS_DENIED)   # never replace links
        if target.exists():
            if not target.is_file():
                raise files.RequestError(files.STATUS_WRONG_TYPE)
            if not replace:
                raise files.RequestError(STATUS_EXISTS)
        return directory

    def make_project_folder(self, directory_path):
        """CodeSmith's mirror of a program (XD44): the first upload into code/<project>/ creates code/ and the project
        folder, when both names are ones the C64 can show; nothing else is ever created."""
        parts = directory_path.split("/")
        if len(parts) != 3 or parts[0] != files.CODE_FOLDER or parts[2] != "" or not files.is_c64_name(parts[1]):
            return
        for relative in (parts[0] + "/", parts[0] + "/" + parts[1] + "/"):
            path = self.root / relative
            if path.is_symlink():
                return                                  # never create anything behind a link
            if not path.exists():
                path.mkdir()
                self.note("created %s", path)

    def begin(self, directory_path, name, replace):
        with self.lock:
            self.expire()
            try:
                directory = self.target_check(directory_path, name, replace)
            except files.RequestError as e:
                return reply(e.status)
            if len(self.sessions) >= UPLOAD_SESSIONS_MAX:
                return reply(STATUS_BUSY)
            upload_id = 0
            while upload_id == 0 or upload_id in self.sessions:
                upload_id = secrets.randbits(32)
            try:
                session = Session(upload_id, directory, name, replace)
            except OSError:
                return reply(files.STATUS_ACCESS_DENIED)
            self.sessions[upload_id] = session
            self.note("%08x begins: %s%s%s", upload_id, directory_path, name, " (replace)" if replace else "")
            return reply(files.STATUS_OK, upload_id, 0)

    def data(self, upload_id, offset, body, chunk_max=UPLOAD_CHUNK_MAX):
        with self.lock:
            self.expire()
            session = self.sessions.get(upload_id)
            if session is None:
                return reply(STATUS_UNKNOWN_UPLOAD, upload_id)
            chunk = unwrap_post_body(body)
            if chunk is None or len(chunk) == 0 or len(chunk) > chunk_max:
                return reply(files.STATUS_BAD_REQUEST, upload_id, session.received)
            if offset != session.received:
                return reply(STATUS_OUT_OF_ORDER, upload_id, session.received)
            if session.received + len(chunk) > UPLOAD_FILE_MAX:
                return reply(files.STATUS_TOO_LARGE, upload_id, session.received)
            session.handle.write(chunk)
            session.received += len(chunk)
            session.crc = files.crc16(chunk, session.crc)
            session.touched = time.monotonic()
            return reply(files.STATUS_OK, upload_id, session.received)

    def finish(self, upload_id, size, crc):
        with self.lock:
            self.expire()
            session = self.sessions.pop(upload_id, None)
            if session is None:
                return reply(STATUS_UNKNOWN_UPLOAD, upload_id)
            received = session.received
            if size != received or crc != session.crc:
                session.discard()
                self.note("%08x check failed: %d bytes CRC %04x, C64 says %d bytes CRC %04x",
                          upload_id, received, session.crc, size, crc)
                return reply(STATUS_CHECK_FAILED, upload_id, received)
            session.handle.flush()
            os.fsync(session.handle.fileno())
            session.handle.close()
            try:
                self.put_in_place(session)
            except files.RequestError as e:
                session.discard()
                return reply(e.status, upload_id, received)
            except OSError:
                session.discard()
                return reply(files.STATUS_ACCESS_DENIED, upload_id, received)
            self.note("%08x saved %s (%d bytes)", upload_id, session.target, received)
            return reply(files.STATUS_OK, upload_id, received)

    @staticmethod
    def put_in_place(session):
        target = session.target
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise files.RequestError(files.STATUS_WRONG_TYPE)
        if session.replace:
            os.replace(session.temp, target)
            return
        # Without replace: fail if the name appeared meanwhile. A hard link is
        # atomic; file systems without links fall back to check-then-rename.
        try:
            os.link(session.temp, target)
            session.temp.unlink()
        except FileExistsError:
            raise files.RequestError(STATUS_EXISTS)
        except OSError:
            if target.exists():
                raise files.RequestError(STATUS_EXISTS)
            os.replace(session.temp, target)

    def abort(self, upload_id):
        with self.lock:
            session = self.sessions.pop(upload_id, None)
            if session is None:
                return reply(STATUS_UNKNOWN_UPLOAD, upload_id)
            session.discard()
            self.note("%08x aborted by the C64", upload_id)
            return reply(files.STATUS_OK, upload_id, session.received)
