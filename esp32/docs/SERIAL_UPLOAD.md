# E03 serial upload protocol v1

UART0 **115200 baud, 8N1, no hardware/software flow control**. USB uses the
board's CP2102 serial bridge, not USB mass storage. Real CP2102 behaviour is
not hardware-validated yet; QEMU pipes and a QEMU PTY exercise the byte stream
and host serial-device code.

The workstation must be at its completed MatmMax `>> ` prompt. Finish/abort
any open block and exit the active program. Do not run another terminal on the
same port while uploading. The supplied host tool handles framing/pacing;
binary data must not be pasted as console text.

## Exchange

1. Host sends ASCII `:upload SIZE CRC32` and CR. SIZE is a decimal integer
   32..262144; CRC32 is exactly eight hexadecimal digits over the **whole
   package**, using the CRC defined in the app ABI document.
2. The receiver allocates a bounded PSRAM candidate, drops queued input and
   replies `READY 256` with CRLF. A bad header, unfinished block or allocation
   failure is rejected without a binary receive phase.
3. Host sends exactly `min(256, remaining)` raw package bytes. It waits for
   `ACK OFFSET` with CRLF, where OFFSET is cumulative bytes received, before
   sending the next chunk. The final chunk can be shorter. ACK means bytes
   received, not that the package is installed.
4. Each chunk has a **5-second complete-receive deadline**. A partial transfer
   times out, frees its candidate, resets input and returns to the math prompt.
5. After the last chunk, the receiver checks the whole-package CRC, MSAP header/
   target/ABI/length/payload CRC, and actual ELF validation/linking. This check
   does not execute the app. It deinitialises its temporary ELF before commit.
6. On success, atomically replace the old RAM slot and reply
   `INSTALLED uploaded SIZE`, then return to `>> `. On rejection, reply
   `ERROR upload: REASON; previous app kept`, discard the candidate and keep
   the prior slot/workspace. No retransmission is implicit in v1.

Raw receive has no Esc escape: all byte values are legal payload bytes. If a
host transfer is interrupted, wait for the receive deadline before retrying.
Bytes still arriving after a reset are new serial input; a sender must stop
sending after failure and resynchronise. A lost final response can leave the
host uncertain whether a valid package committed; inspect `:apps`/run the app
before retrying. CRC32 does not authenticate code.

This protocol does not flash firmware or write persistent storage. One uploaded
program is held in RAM, named `uploaded`, until replaced, `:unload` or reboot.
The native package/ABI and arena/ELF cleanup contract are described in
[WORKSTATION_APP_ABI.md](../../docs/WORKSTATION_APP_ABI.md).

## Host tool

```sh
# A fresh disposable QEMU session; upload B and retain a console afterwards:
python3 esp32/scripts/upload_external.py --qemu build/esp32/e03_external/demo_b.msp

# Real board, after E03 firmware and UART have been hardware-tested:
python3 esp32/scripts/upload_external.py --port /dev/ttyUSB0 build/esp32/e03_external/demo_b.msp
```

The tool uses Python's standard library, checks file size before reading it,
performs stop-and-wait transfer, and keeps an interactive console on a TTY.
**Ctrl+] closes that host session**, restoring the terminal. This is distinct
from the parked Ctrl+A/X issue in the older QEMU mux runner. QEMU RAM installs
disappear when its host session closes. `--no-console` is for automated transfer
checks; in QEMU that also terminates the fresh emulator afterwards.

The serial-device backend sets raw 115200 8N1 and attempts to deassert DTR/RTS.
It restores the device's original terminal attributes when closing. Some real
boards/terminal programs reset the chip on connection changes; this must be
checked on the ordered board. A reset loses the prototype RAM install. Keeping
the uploader's console open avoids needing a second terminal connection for
the first run; it is not a claim that real hardware reset behaviour is proven.

The tool waits for QEMU's ready prompt before injecting UART bytes into a fresh
ROM/SDK boot. Its physical-port mode requests a prompt with CR and reports a
timeout if it cannot synchronise. It does not invoke esptool or auto-flash.
