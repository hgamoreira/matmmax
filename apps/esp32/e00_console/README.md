# E00 — ESP32-S3 console and memory smoke test

First firmware milestone for the ordered **ESP32-S3 N16R8 CP2102** board.
Scope: boot, UART0 input/output, flash/PSRAM reporting and allocation checks
(XD107). The MathSmith engine, networking, screen, filesystem and application
launcher come later. No RP2350 firmware is built by these commands.

**Status (2026-11-15): built; 9 automated tests PASS (7 firmware, 2 emulator
setup). Hardware candidate staged. No board has been flashed or
hardware-validated; E00 remains open for the user's hardware test.**

## Build and run on this PC

From the repository root, on x86_64 Linux with git, curl, Python 3.9+ and a
native compiler installed:

```sh
tools/setup_esp_qemu.sh
tools/setup_esp_idf.sh
esp32/scripts/console.sh build
esp32/scripts/console.sh test
esp32/scripts/console.sh qemu
```

The first setup downloads the tools. ESP-IDF **v5.5.6** is pinned to commit
`880a203ae603d08a94d06aa01e0d6761958e5e9e`; its tool manifest verifies the
compiler/build-tool archives. Its own Python environment uses Espressif's
v5.5 constraints, separate from the host server's `.venv`. SDK and tools live
in git-ignored `third_party/`; build output stays in `build/esp32/e00_console/`.
The wrapper selects ESP32-S3 and its local environment for each invocation;
there is no global shell or toolchain change.

QEMU starts from a temporary copy of `flash.bin`, with 16 MB flash, 8 MB octal
PSRAM, UART0 and no network device. **Ctrl+A, then X** exits the interactive
emulator. It never opens a physical serial port. Inspect binary/static memory
sizes with `esp32/scripts/console.sh size`.

For the exit sequence, **hold Ctrl and press A, release both, then press
lowercase `x` without Ctrl**. Ctrl+A alone has no visible effect. Ctrl+A then
`h` prints QEMU's shortcut help. If the complete sequence does not reach QEMU
in VS Code, try the same command in a separate terminal window. VS Code can
intercept terminal shortcuts; that is a possible cause, not a confirmed
diagnosis of this session. No editor keybindings are changed by the project.
References: [QEMU shortcut sequences](https://www.qemu.org/docs/master/system/mux-chardev.html),
[VS Code terminal shortcuts](https://code.visualstudio.com/docs/terminal/advanced#_keyboard-shortcuts-and-the-shell).

```text
MathSmith workstation E00 - console smoke test
SDK: v5.5.6
...
Flash: 16777216 bytes
PSRAM: ready bytes=8388608
...
e00> help
help | info | mem | test | echo TEXT | reboot
```

## Commands and behaviour

| Command | Result |
|---|---|
| `help` | Command list |
| `info` | SDK/chip revision/cores, detected flash and PSRAM, console and `double` size |
| `mem` | Total, free and largest byte-addressable blocks, separately for internal and external heaps |
| `test` | Memory patterns, oversize rejection, recovery allocation and heap integrity |
| `echo TEXT` | Print TEXT back, independently of the typed-character echo |
| `reboot` | Restart and return to the diagnostic prompt |

Commands are case-sensitive. CR, LF and CRLF submit one line; backspace and
Delete erase a character; Esc cancels a pending line. Input is printable ASCII,
at most 127 bytes. A longer line is discarded at Enter and the prompt recovers.
Arrow-key editing/history and terminal escape sequences are not implemented.

`test` allocates 4096 bytes internally and 1 MiB externally, checks the pointer
belongs to the requested memory type, writes/reads every byte with two patterns,
then frees the buffers. The external buffer exceeds the cache size, exercising
external memory access. A request for physical PSRAM size plus one byte must
return NULL; a subsequent 4096-byte external allocation must work. Finally,
ESP-IDF checks heap integrity. This checks a buffer and allocation recovery,
not every physical bit or all possible out-of-memory conditions. ESP-IDF's
startup PSRAM test is also enabled.

Default `malloc` and task stacks stay internal. External allocations are
explicit. If PSRAM is absent, the diagnostic console still starts, `info`
reports `unavailable`, and `test` reports `SELFTEST: FAIL`. A failed PSRAM
startup memory test may stop before the console; capture the SDK message.
Detailed layout: [memory map](../../../esp32/docs/MEMORY_MAP_E00.md).

## Automated verification

`esp32/scripts/console.sh test` runs the installed-emulator checks and the
actual target firmware tests. The firmware suite checks boot/detected memory,
patterns and allocation recovery across repeated tests, external heap recovery,
CR/LF/CRLF, backspace/Delete, invalid/empty commands, cancel, line length limits,
reboot and operation with PSRAM removed from the emulated machine.
UART transcripts are saved in `build/esp32/e00_console/test-logs/`.
No host/DOS application source or shared routine is changed for this milestone.

Measured in QEMU: `ms_console.bin` **209520 bytes**; at the ready prompt,
**377608 bytes internal heap free**, **8383888 bytes external heap free**.
Three repeated memory tests return both heaps to their original measured
free values. These figures describe E00, not the eventual OS budget.
QEMU prints `rtcinit: o_code calibration fail` during startup; the tests pass
with that warning present. Real-board calibration and timing are untested.

**User emulator check (2026-11-15):** the user opened E00 in VS Code's
terminal, tried all commands and reported they all worked. Exiting with
Ctrl+A did not work for them; the exact sequence/key routing is unconfirmed.
This report validates their console use in QEMU, not the physical board.
The user subsequently confirmed the full exit sequence failed too and parked
it as non-blocking while proceeding to E01. Its cause remains unresolved.

## Hardware candidate and physical-board test (PASS/FAIL)

After the automated tests pass, stage a candidate:

```sh
esp32/scripts/console.sh release
```

`releases/esp32/e00_console/` contains the merged flash image, ELF and linker
map, SDK configuration, these instructions, memory map and SHA256SUMS.
`flash.bin` is a complete 16 MB raw image starting at flash offset **0x0**.
Staging does not flash anything. The following procedure is for the user when
the board arrives; it has not been performed.

1. **Identify:** confirm the board/module marking is ESP32-S3 N16R8 and record
   its revision. Use the USB connector routed through the CP2102, initially
   with no external peripherals connected. Record the Linux serial device
   (`/dev/ttyUSB0` is an example, not a detected port).
2. **Flash and boot:** after building/testing/staging the candidate, run the
   following from the repository root, substituting the port as needed. This
   writes the E00 firmware to the selected board. If the board requires manual
   download mode, follow its own BOOT/RESET procedure. In the monitor, expect
   the E00 banner and `e00>` prompt. Monitor exit is Ctrl+].

   ```sh
   esp32/scripts/idf.sh -C apps/esp32/e00_console \
       -B "$PWD/build/esp32/e00_console" \
       -D "SDKCONFIG=$PWD/build/esp32/e00_console/sdkconfig" \
       -p /dev/ttyUSB0 -b 115200 flash monitor
   ```

3. **Identify memory:** type `info`. Expect ESP32-S3, two cores, flash
   **16777216** bytes, PSRAM `ready` with **8388608** bytes, and `double: 8
   bytes`. Record the chip revision and complete output. A mismatch is FAIL.
4. **Console:** type `help`, then `echo hello`. Expect the command list and a
   separate `hello` result. Type `wrong`: expect `Unknown command; type help.`
   and another usable prompt. An empty Enter should also return to the prompt.
5. **Memory checks:** type `mem`, `test`, `test`, `test`, `mem`. Every memory
   check must say PASS and each run must end `SELFTEST: PASS`. External total,
   free and largest block should recover to their previous values. Record both
   `mem` outputs and the test results; report any reset or stall.
6. **Editing/cancel:** type `echo hellp`, use backspace, type `o`, Enter:
   expect `hello`. Type `reboot` but press Esc before Enter: expect `Cancelled.`
   with no reboot, then verify `echo still here` works.
7. **Long input:** paste the following line and Enter. Expect `Line too long
   (127 bytes maximum); discarded.` Then `echo recovered` must print
   `recovered` and return to the prompt.

   ```text
   echo 01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
   ```

8. **Restart:** type `reboot`. Expect `Rebooting...`, the startup banner and
   a usable prompt; run `test` again. Press the board's RESET button and check
   the banner/prompt again. Report PASS/FAIL separately for software and button
   resets.

Report **PASS/FAIL for steps 1–8**, chip/module markings, `info`, both `mem`
outputs and any failure text. Only the user's report can close E00.

## Sources

- [ESP-IDF v5.5.6 release](https://github.com/espressif/esp-idf/releases/tag/v5.5.6)
- [ESP-IDF external RAM configuration](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-guides/external-ram.html)
- [Heap capability allocator](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/mem_alloc.html)
- [Espressif QEMU ESP32-S3 model](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32s3/README.md)
