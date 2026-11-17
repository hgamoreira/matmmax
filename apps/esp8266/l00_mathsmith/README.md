# L00 — serial MathSmith Light for Wemos D1 mini

**2026-11-15: corrected target image builds and fits; 19 automated native/linked-image
checks PASS. User hardware report confirms boot, serial console, 4x4 matrix
entry and eigensolver after the startup correction. Full procedure remains open.**
The user selected serial-only
MathSmith, 64-bit doubles, fewer variables, provisional 8x8 matrices, Wi-Fi off
and memory diagnostics (XD111). No launcher is added until this fit is measured
on the board. esptool identification confirmed ESP8266EX / 4 MB / 26 MHz crystal;
the subsequent boot/eigenvalue transcript supplies the first MathSmith hardware evidence.

For everyday operation, see the [ESP8266 user manual](../../../esp8266/docs/USER_MANUAL.md):
connection, calculations, matrix operations, typed programs and troubleshooting.
The release candidate also includes a copy named `USER_MANUAL.md`.

## Build and limits

From the repository root:

```sh
python3 tools/setup_esp8266.py
esp8266/scripts/mathsmith.sh build
esp8266/scripts/mathsmith.sh test
esp8266/scripts/mathsmith.sh release
```

Pins: Arduino ESP8266 core **3.1.2** with Xtensa L106 **GCC 10.3.0**
(`3.1.0-gcc10.3-e5f9fec`), balanced 32 KiB cache / 32 KiB IRAM, CPU **80 MHz**,
crystal **26 MHz**, DIO flash **40 MHz**, UART0 **115200 8N1**. The build follows
the pinned core's `platform.txt` recipes, with a custom placement of engine
constants in flash. Core/framework sources are unmodified. Installer checksums:

- Core archive: `b3f47686d7783c120c2f10bf82788f921c53db8642cc87012599abb6e335b182`.
- Compiler archive: `89d21f38a516e34e0bb44df7e30ef6b1bc2f7f125aae3b7986e5cae545a3ed83`.

The generated profile reuses the current shared engine/matrix/linear-algebra/
plot-style sources. It changes explicit limits and inserts two stack-reserve
checks in the generated parser; it never edits the shared files. Source anchors
must match exactly or the build stops for review. Original hashes and a target
input fingerprint prevent testing/releasing an outdated target build.

| Resource | L00 limit |
|---|---|
| Numeric values | 64-bit `double`; target compile-time check |
| Variables | 16 slots, including engine-created values such as `ans` |
| Matrix | 64 total elements: 8x8 square, or other shapes/vectors up to 64 elements |
| Anonymous / typed functions | 4 each; nesting/call depth reduced to 4 |
| Builtin argument count | 6 |
| Scalar cache | 8 blocks retained for reuse |
| Input line | 191 printable characters; overflow discards the whole line |
| Input bracket nesting / unary chain | 8; deeper lines rejected before evaluation |
| Typed block | At most 128 statements; also subject to memory budget |
| Numeric tool evaluation limit | 2000 |
| Formatting | Format buffer 128, output 256 bytes; strings also obey matrix limit |
| Engine payload budget | 16384 bytes; allocation metadata is additional |
| Continuation stack | Dedicated 16384 bytes in BSS; parser checks 6144-byte reserve |

Scalar expressions, scientific functions, variables, typed functions/loops,
small matrix operations/solvers and cooperative Esc/Ctrl+C cancellation are
available. The launcher, external native app upload, FFT, files, editor, AI and
rendered plots are not installed. File opens/removals are mapped to unavailable
stubs. Some unused engine help/commands still describe the larger application;
`help`/`:help` at the prompt describe this port. No framebuffer is allocated.

`clear A` clears one variable; use `clear A; clear B;` for two. Powers retain
the existing engine's left grouping (`2^3^2` is 64). Matrix temporaries count
against the budget, so a per-matrix element limit does not guarantee all
combinations fit. Allocation/stack-limit errors must leave the prompt usable.

## Console and memory

`help`/`:help` describe L00. `:info` and `:mem` report actual flash, CPU, Wi-Fi
mode, free heap, largest heap block, fragmentation, engine live/peak payload and
stack high-water margin. `:reset` starts a new workspace; `:reboot` restarts the
chip. `quit`/`exit` keep MathSmith as the main prompt. Backspace edits input;
Esc/Ctrl+C discards input or cancels a running calculation. `abort` ends an open
block. Type each command at its prompt; rapid typeahead is bounded to 255 bytes
while calculating and overflow cancels/discards the queued input.

The core normally borrows a small SDK stack. L00 explicitly links its dedicated
BSS-stack implementation (`disable_extra4k_at_link_time`), rather than merely
enlarging the borrowed stack. A linked-image test checks the full 16424-byte
continuation structure is present. Parser reserve checks cover normal parser
returns; arbitrary nesting/resource use is not a proof against every stack fault.

The engine's strings/tables live in mapped flash. The pinned core's documented
NON32XFER exception handler services byte reads there, preserving C string APIs
at a performance cost. Its execution, actual SDK heap use, watchdog servicing
and stack margins need the real-board test. The current firmware binary is
**382464 bytes**, static `.data/.rodata/.bss` **47252 bytes**, including the
dedicated stack. This is a linked fit, not a runtime free-heap measurement.
See [memory map](../../../esp8266/docs/MEMORY_MAP_L00.md).

## Automated validation

`mathsmith.sh test` runs 19 checks: Python numerical references/double-sensitive
arithmetic, matrix solve/determinant/limits, functions/loops/variable cap,
formatting/transpose, input editing/CRLF/overflow, running/pending cancellation,
nesting rejection, startup/runtime allocation failure, stack-reserve failure,
repeated matrices/clear/reset and unavailable services. Allocator tests verify
overflow rejection, zeroing and failed realloc preserving data/accounting.
Target checks inspect actual L106 sections, flash handler, dedicated stack,
binary header/capacity and build/source fingerprints. The native harness uses
the exact generated engine/front-end/allocator; injected stack/cancel failures
test control paths, not chip behaviour. **No whole-firmware ESP8266 emulator
execution is claimed.** Logs: `build/esp8266/l00-build.log` and `l00-tests.log`.

`mathsmith.sh native` runs that Linux harness (Ctrl+D ends input). It is useful
for logic tests and does not represent physical ESP8266 memory or timing.

## First board attempt and corrected image

The user reported continuous scrolling with periodic garbled characters and
no readable crash reason. Inspection found an image-layout defect: custom
constant placement preceded the core's fixed checksum fields, so `elf2bin.py`
overwrote the first eight bytes of engine constants instead. The linker now
keeps checksum length/value at **0x40201010/0x40201014**, ahead of the custom
constants. The original image is rejected by the new checker; the corrected
image passes it. This is a confirmed build defect; its relationship to the
observed output still needs a board retest/readable log.

Build, staging and flash commands now validate checksum addresses/length/CRC
and every app load address/length/payload against the ELF, masking only the
intended checksum words. Regression checks also reject modified payloads or
load addresses even when their whole-image CRC has been recomputed. Early
115200-baud `L00 boot: UART ready` and `L00 boot: starting MathSmith` markers
help locate any remaining startup failure. The first marker precedes Wi-Fi
configuration and the second precedes engine/console startup.

If output remains unreadable, close the monitor with Ctrl+] and inspect ROM
boot output at its separate baud rate, pressing the board's Reset button:

```sh
esp8266/scripts/mathsmith.sh monitor /dev/ttyUSB0 74880
```

Paste a short repeating section, including readable reset/load/checksum lines.
Return to the default `monitor /dev/ttyUSB0` for the 115200-baud application.
Changing monitor baud does not reflash the board.

### Decoded startup exception and allocator placement

The next user report at boot baud was `Fatal exception (0)` with
`epc1=0x40219dac`, other EPC/DEPC values zero and `excvaddr=0`. Against the
matching pre-fix ELF this decodes to `umm_init`, at the entry to heap startup,
before the SDK enables flash instruction cache and before Arduino `setup()`.
The saved image is under `build/esp8266/l00_mathsmith/history/pre-cache-umm-before-fix/`.

The builder had flattened nested object paths to prefixed filenames. The core
linker explicitly excludes **`umm_malloc.cpp.o`** from generic C++ placement in
flash; renaming it `umm_malloc_umm_malloc.cpp.o` defeated that rule. This put
`umm_init` in flash although the entry path called it directly with cache off.
The builder now preserves original basenames and rejects collisions. Startup
allocation code stays in IRAM: current `umm_init=0x40100ca4`,
`umm_init_heap=0x40100c04`. Disassembly confirms the init path's memory clearing
calls use ROM, and the dedicated BSS stack is retained.

Build/release/flash validation and an additional regression check require
entry and heap-init functions in IRAM and the expected archive member name.
The image matching the crash fails that check; the new image passes all 19
checks. The user subsequently supplied a successful boot/help/matrix/eigenvalue
transcript after this correction, recorded below.

## Hardware report received, 2026-11-15 — partial validation

The user transcript (local hardware log omitted; see the validation summary) confirms readable
115200-baud boot markers, `Ready` and a usable prompt after the allocator
placement fix. Core 3.1.2 reports 80 MHz CPU, 4194304-byte flash and Wi-Fi off.
Help, 4x4 matrix entry/display and `eig(A)` work on the physical D1 mini.
Exact integer characteristic-polynomial expansion independently gives
`(lambda-10)(lambda+2)(lambda^2-8)` for the supplied matrix; the displayed
`-2.8284, -2, 2.8284, 10` agree with `-sqrt(8), -2, sqrt(8), 10`.

After that workload, free heap is **27888 bytes**, largest block **27304**,
fragmentation **3%**, engine payload **5072** (peak **5664**) and minimum free
stack **11596/16384 bytes**. The earlier **33264-byte** heap report with payload
zero is **before engine initialisation**. These are specific workload readings,
not a maximum workload or repeated-cleanup/stability measurement.

This confirms the boot/console recovery and the reported matrix/eigenvalue
case. Remaining precision, bounds, functions/editing, cancellation, repeated
cleanup and reset/cold-boot checks are not yet individually reported; the full
hardware milestone stays open. No firmware change or new flash is needed just
to complete those tests.

## Hardware procedure — report PASS/FAIL for steps 1–8

Close other serial monitors before each port operation. Use the identified
`/dev/ttyUSB0`, substituting the actual device if it changes. The candidate is
`releases/esp8266/l00_mathsmith/`, with binary, ELF/map, configuration/source
fingerprints, instructions, memory map, core licence and SHA256SUMS. The binary
includes its boot image and is written at **0x0**, not the ESP32-S3 offset.

1. **Preserve existing firmware:** save the current complete 4 MB flash before
   replacing it. Check the reported backup exists and has 4194304 bytes. This
   reads the board and saves under `build/esp8266/l00_mathsmith/backups/`.

   ```sh
   esp8266/scripts/mathsmith.sh backup /dev/ttyUSB0
   ```

2. **Flash/boot:** the next command writes L00 firmware onto the D1 mini. Open
   the monitor afterwards; press the board's Reset button if its banner has
   already passed. Expect `MathSmith Light L00`, real flash 4194304, CPU 80 MHz,
   Wi-Fi mode 0 and `Ready. Type help.` followed by `>> `. ROM boot text uses
   a different baud rate and may look garbled before the readable app banner.

   ```sh
   esp8266/scripts/mathsmith.sh flash /dev/ttyUSB0
   esp8266/scripts/mathsmith.sh monitor /dev/ttyUSB0
   ```

   Ctrl+] closes the monitor. Record any reset loop, exception or allocation
   failure and stop the test there if the prompt does not become usable.

3. **Baseline and doubles:** type `:mem` and save its complete output. Expect
   payload limit/allocated stack 16384 and nonzero free heap/stack margin.
   Type `format long`, `x=41`, `x+1` (42), `sin(pi/6)` (0.5), and
   `(2^53-1)-2^53` (-1). Use `help` and confirm its smaller limits.
4. **Matrices/bounds:** `A=[4 -2;-2 4]; A\[2;8]` must give a column 2,3.
   `B=ones(8,8); sum(sum(B))` must give 64. `zeros(9,9)` must report a memory/
   element-limit error. `x+1` must still give 42; `clear A; clear B;` must work.
5. **Functions/blocks/editing:** `f=@(t) t+x;` then `f(1)` must give 42.
   Enter `s=0;`, `for k=1:8`, `s=s+k;`, `end`, one line at each prompt; `s`
   must give 36. Type `x+2`, Backspace, `1`, Enter: expect 42. Start another
   `for` block and press Esc: return to `>> ` with `x` preserved.
6. **Running cancellation:** enter `s=0;`, `while 1`, `s=s+1;`, `end`, then
   press Esc after it starts running. Expect an interrupted calculation and
   usable prompt, with `x+1` still 42. Try again with Ctrl+C. Record stalls,
   watchdog resets or unusually slow response; this is a physical timing check.
7. **Memory recovery:** warm with `A=eye(8); B=inv(A); clear A; clear B;` twice,
   then record `:mem`. Repeat that command 20 times and record `:mem` again.
   Live engine payload should recover; the largest free block/heap should
   stabilise, allowing for SDK activity. Peak/stack minimum are high-water
   counters and need not recover. Report both complete outputs.
8. **Reset and cold boot:** `:reset` must forget `x` and still evaluate `2+2`.
   `:reboot` must restart L00; record `:mem`. Close the monitor, unplug/replug
   USB, reopen it and press Reset if needed: the prompt/calculations must work
   again, with Wi-Fi off and no saved workspace. No filesystem is mounted.

Only the user's board report closes L00. At preparation time, the assistant
has not opened a physical serial port or flashed/tested this firmware.

References: [ESP8266 platform](../../../esp8266/README.md),
[core 3.1.2 MMU/flash-access documentation](https://arduino-esp8266.readthedocs.io/en/3.1.2/mmu.html),
[official package manifest](https://arduino.esp8266.com/stable/package_esp8266com_index.json).
