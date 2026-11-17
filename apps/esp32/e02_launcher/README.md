# E02 — run programs from MathSmith and return to its workspace

**Built and tested in QEMU, 2026-11-15: 48 combined tests PASS. Hardware
validation remains open.** Scope selected by the user: app launcher plus a
small test program (XD109), initially compiled into the firmware, one foreground
program at a time. E00/E01 remain separate builds and hardware candidates.

## Try it

From the repository root, using the already installed SDK/QEMU:

```sh
esp32/scripts/workstation.sh build
esp32/scripts/workstation.sh test
esp32/scripts/workstation.sh qemu
```

Type the lines below one at a time, waiting for each prompt. Program switches
discard queued typeahead, so pasting the entire transcript can discard lines.

```text
>> x=41
x = 41
>> :apps
Applications:
  demo - Launcher test program
...
>> :app demo
Demo program. Text is echoed here, not evaluated.
...
demo> x=999
Demo echo: x=999
demo> exit
Returned to MathSmith; workspace kept.
>> x+1
ans = 42
```

This runs another native app module with its own prompt. MathSmith's session
stays resident while that app runs. `:app demo hello world` passes the literal
text `hello world` as arguments. The demo can also return with Esc or `fail`;
the latter is a controlled error return, not a CPU fault. All three paths
release its scratch memory and return to the same math workspace.

## Behaviour and boundaries

- `:apps` lists the compiled app registry. `:app NAME [arguments]` launches
  one. Names are case-sensitive; arguments are literal text, not shell syntax.
  An unknown app or failed arena allocation reports an error without running it.
- Finish or `abort` an open MathSmith block before launching; the launcher
  refuses to switch while a block is incomplete. App names are not reserved
  math variable names: `demo=8` remains a normal assignment.
- The demo receives a **zeroed 4096-byte PSRAM arena**, outside the engine's
  4 MiB payload cap. The launcher frees the original arena on normal, cancelled
  and error returns and rejects nested launches. The demo has no MathSmith
  session pointer and does not modify variables, matrices or functions.
- Console input ownership switches to the app. Already queued input is dropped
  on entry/return; bytes arriving later are ordinary new input. Backspace,
  CR/LF/CRLF and line overflow are tested on both sides of the switch. The demo
  accepts 127 printable characters per line. Returning prints the math prompt;
  there is no framebuffer/history screen to restore in this serial version.
- MathSmith E02 inherits E01's engine, numeric limits, allocator/timer and UART0
  **115200 8N1** console. Existing commands, `:mem`, `:reset`, and `:reboot`
  remain available. `quit` retains the workspace because MathSmith itself is
  the workstation prompt. It does not terminate QEMU.
- **Initially apps are compiled into the firmware.** Adding another app needs
  a firmware rebuild. Loading separate native program files from SD is a later
  feature. Files, production editors, AI, plot output and physical keyboard/
  display drivers are also later scopes. The demo proves the launch/return
  contract before a WordSmith port.
- All modules share one address space/call stack. There is no process isolation,
  multitasking or recovery from a module's hard fault. Lifecycle cleanup covers
  callbacks that return normally, with success/cancel/error status.

The user parked the VS Code/QEMU exit-sequence problem; E02 does not change
it. Hardware arrival/driver work and the real E00/E01 procedures remain open.

## Structure and validation

Portable C89 lifecycle: `common/workstation/launcher.c`; portable test app:
`apps/workstation/demo/demo.c`. ESP32 admission uses the PSRAM capability
allocator. E02 reuses E01's serial front end with build-conditional hooks, not
a fork of the numeric engine. E01 is rebuilt without these hooks and tested
again. Its UART tests now accept an optional build directory/banner so that
the exact same nine numeric/front-end tests also run against E02.

**48 PASS:** the previous 23 E00/E01/allocator/QEMU tests; 9 native launcher/
demo tests; 16 E02 firmware tests (the 9 engine tests plus 7 launcher tests).
Coverage includes:

- Normal exit, cancellation and controlled errors, plus repeated launches with
  live math values/functions intact and engine/external-heap use restored.
- Unknown names, argument/usage errors and refusal inside incomplete math blocks.
- Native arena zeroing/cleanup, failed allocation followed by recovery, original
  arena ownership, registry validation and nested-launch rejection.
- Actual target arena-allocation rejection when PSRAM is absent; the shell stays
  usable. Native failure injection covers admission without running the app.
- Demo input/editing/overflow and CRLF transitions back to the MathSmith prompt.
- E01's Python numeric/FFT references and 191-number golden script on **both**
  E01 and E02, plus its cancellation, memory, reset and reboot cases.

Logs: `build/esp32/e02-tests.log`,
`build/esp32/e02_launcher/test-logs/`. `workstation.sh size` reports static use.
The shared numeric and DOS/Linux sources are unchanged. E00 remains unchanged;
the rebuilt E01 has the same tested behaviour, with an updated build hash.
The existing QEMU calibration warning is recorded without physical timing or
calibration claims. Memory layout: [E02 map](../../../esp32/docs/MEMORY_MAP_E02.md).

## Hardware candidate and numbered test (PASS/FAIL)

After the automated checks pass, run `esp32/scripts/workstation.sh release`.
It stages `releases/esp32/e02_launcher/`: raw 16 MB `flash.bin` (offset **0x0**),
ELF/map, sdkconfig, README, memory map and SHA256SUMS. No physical port is opened.
Run E00 and E01's hardware checks first when the board arrives; then:

1. **Flash/boot:** identify the N16R8 board's CP2102 port, then substitute it
   below. This explicitly writes E02 onto that board. Expect the E02 banner,
   PSRAM ready with 8388608 bytes and `>> ` prompt.

   ```sh
   esp32/scripts/idf.sh -C apps/esp32/e02_launcher \
       -B "$PWD/build/esp32/e02_launcher" \
       -D "SDKCONFIG=$PWD/build/esp32/e02_launcher/sdkconfig" \
       -p /dev/ttyUSB0 -b 115200 flash monitor
   ```

2. **List and errors:** `:apps` must list `demo`. `:app missing` must say unknown
   application and return to a usable math prompt. `2+2` must give 4.
3. **Launch/return:** type `x=41; A=[1 2;3 4]; f=@(t) t+x;`, then `:app demo`.
   Wait for `demo>`, type `x=999`, then `exit`. Expect a demo echo, then the
   return message and math prompt. `x+1` must give 42, `A(2,1)` 3, `f(1)` 42.
4. **Arguments/editing:** `:app demo hello world` must show those arguments.
   Type `hellp`, backspace, `o`, Enter: expect `Demo echo: hello`. `exit` must
   return normally. Nothing from that text should appear as a math assignment.
5. **Cancel/error:** reopen the demo and press Esc; expect a cancelled-program
   message and math prompt. Reopen and type `fail`; expect a controlled error
   return. After each, `x+1` must still give 42 and another launch must work.
6. **Memory cleanup:** warm with one launch/exit, then record `:mem`. Launch
   and return repeatedly (normal, Esc and `fail`, at least twice each). Record
   `:mem` again: live engine payload and external free heap should recover.
   Stack minimum/peak counters can change; they are high-water measurements.
7. **Pending block:** type `for k=1:3`, then `:app demo`. Expect refusal with
   the block still open. Type `disp(k)`, then `end`: expect 1,2,3. Now launching
   the demo should work; exit it and check arithmetic again.
8. **Reset:** `:reboot` must return to E02. Launch/exit the demo once more and
   verify arithmetic. Reboot intentionally discards the old math workspace.

Report PASS/FAIL for steps 1–8, both memory outputs and any warning/stall/reset.
Only that real-board report closes the hardware milestone. No board has been
flashed or hardware-validated by this work.
