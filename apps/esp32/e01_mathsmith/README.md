# E01 — MatmMax engine on the ESP32-S3 serial console

**Built and tested in QEMU (2026-11-15); physical-board testing remains open.**
The user selected the initial scope in XD108: serial prompt, existing engine,
64-bit doubles, 4096 elements per matrix, FFTs and cooperative Esc cancellation.
This is a separate firmware image from [E00](../e00_console/README.md).
E00 remains the board/PSRAM diagnostic and also awaits hardware validation.

## Build and try it

From the repository root, after the existing local SDK/QEMU setup:

```sh
esp32/scripts/mathsmith.sh build
esp32/scripts/mathsmith.sh test
esp32/scripts/mathsmith.sh qemu
```

First setup, if needed: `tools/setup_esp_idf.sh`, `tools/setup_esp_qemu.sh`.
E01 uses the same pinned ESP-IDF **v5.5.6**, ESP32-S3 N16R8 memory configuration
and UART0 **115200 8N1, no flow control** as E00. `mathsmith.sh size` reports
static memory and `mathsmith.sh release` stages the hardware candidate.
QEMU uses a disposable flash copy and no physical serial port/network device.

```text
>> x=41
x = 41
>> x+1
ans = 42
>> A=[4 -2;-2 4]; A\[2;8]
ans =
    2
    3
>> fft([1 2 3 4])
ans =
    10     0
    -2     2
    -2     0
    -2    -2
```

The existing QEMU Ctrl+A then lowercase x exit sequence still fails in the
user's VS Code terminal. Its cause is unknown and the user parked it as
non-blocking. It is independent of MatmMax's commands; E01 does not claim
to fix it. `quit`/`exit` report that there is no parent shell and retain the
workspace; they do not exit QEMU.

## What works

The original `engine.c`, `mat.c`, `linalg.c`, `fft.c` and `plot.c` are compiled
directly from `apps/m01_mathsmith/`, without source edits or copies. The build
adapter supplies allocation/timing services and resolves newlib's `finite`
name collision with the plot source's static helper. Existing DOS/Linux
builds are unaffected.

- Expressions, assignments, matrices, indexing, linear algebra and statistics.
- Numerical functions (`fzero`, `integral`, `deriv`), anonymous functions and
  functions/loops/conditionals typed over several lines.
- `who`, `clear [name]`, `format short|long`, `fprintf`, `tic`/`toc`.
- Full FFT implementation, including non-power-of-two lengths. The output
  uses two columns `[real imaginary]`; use `cabs`/`cangle` for magnitude/phase.
- `>> ` for a complete command and `..> ` while a multiline block is open.
  `abort` drops an unfinished block. Esc cancels input or an open block, and
  stops calculations at the engine's polling points. It drops queued input
  when stopping a calculation. Ctrl+C is also handled if the terminal passes
  that byte to the firmware.

Input is printable ASCII, at most **399 bytes per line**. Backspace/Delete
edit the current line; CR, LF and CRLF submit it. Oversized input is discarded.
The front end preserves bounded typeahead during calculations. A full input
queue is discarded with a message. History/arrow-key editing is not included.

Platform commands use a colon:

| Command | Behaviour |
|---|---|
| `:help` or standalone `help` | E01's console/language overview |
| `:info` | SDK, detected flash/PSRAM and engine limits |
| `:mem` | Engine payload use/peak/cap, free heaps and minimum main-stack space |
| `:reset` | Discard the workspace and start a new session |
| `:reboot` | Restart the whole firmware |

## Limits and deferred features

- **4096 elements per matrix**, e.g. 64x64 square, 1x4096 vector, or another
  shape with that product. **40 variables**; the existing small-build
  function/recursion limits also apply. These are initial limits, not measured
  maximum hardware capacity.
- **4 MiB engine payload budget in PSRAM**, plus allocator headers/metadata.
  SDK services, UART buffers and the **32 KiB main stack** stay internal.
  Failed allocation returns NULL; there is no fallback to internal RAM.
  `:mem` counts engine payloads, including the engine's bounded scalar pool.
  That pool can remain after `clear`/`:reset`; a cleared workspace need not
  report zero. No future app memory budget is guaranteed by this cap.
- FFT results use two elements per sample, so the current maximum is **2048
  samples**, even though a real input vector can hold 4096 elements.
  Existing representation detail: a **single-row 1x2 input is two real
  samples**, not one complex sample. Thus the scalar `fft(1)` forward result
  is valid, but passing its 1x2 result directly to `ifft` has that ambiguity.
  Multirow complex spectra round-trip; the shared engine is unchanged.
- MatmMax groups powers left to right: `2^3^2` is **64**. This preserves the
  existing language, as described in its manual.
- Cancellation is cooperative; a kernel without poll points must finish
  before cancellation can take effect. No keyboard latency or hardware speed
  claim is made from QEMU timings.
- **Files, script storage, editor, AI, plot output and app launcher are later
  milestones.** No filesystem is mounted. File commands fail; AI has no hook.
  Plot expressions can compute data but explicitly say no display/output is
  implemented. There is no app launch/return or session persistence across
  reboot yet.
- If PSRAM is missing, the diagnostic prompt stays available, but the engine
  cannot allocate a workspace; `:info`, `:mem` and `:reboot` remain usable.

## Automated results

**23 tests PASS**: 9 E01 firmware tests, 5 native allocation/engine failure
tests, and the existing 9 E00/QEMU tests. Tests cover:

- Elementary doubles vs Python math, precision below float32 resolution,
  matrix solves/inverse/limits and failure recovery, calculus and statistics.
- The existing **191-number golden script** vs a native build of the engine,
  including eigenvalues/SVD, polynomials, matrices and programs (1e-6 relative
  comparison, preserving the existing golden-test tolerance).
- FFT lengths 1, 2, 3, 8, 12, 100, 401, 512 vs Python's DFT definition;
  multirow round trips with zero imaginary output, the 2048-sample boundary
  and refusal above it. Forward error tolerance 2e-12 times spectrum scale.
- Multiline functions, Esc interruption/workspace survival, repeated matrix
  allocate/clear cycles, reset, reboot, line overflow and absent PSRAM.
- The real allocator adapter with a libc backend on Linux: alignment,
  calloc overflow/zeroing, realloc preservation on failure and engine OOM
  recovery at the start of parsing and during matrix allocation. Those
  injected engine failures are native tests; no firmware fault injector is
  exposed. Actual target PSRAM access/oversize rejection is covered by E00.

Logs: `build/esp32/e01_mathsmith/test-logs/`, aggregate
`build/esp32/e01-tests.log`. QEMU still prints the recorded
`rtcinit: o_code calibration fail` startup warning. No physical timing or
calibration inference follows from the passing tests.

## Physical ESP32-S3 test — report PASS/FAIL per step

First run E00 on arrival to identify/test the board and memory. With E00's
results recorded, build/test E01 and stage it with `mathsmith.sh release`.
The candidate in `releases/esp32/e01_mathsmith/` contains a complete 16 MB
raw `flash.bin` (offset **0x0**), ELF/map, sdkconfig, this README, memory map
and SHA256SUMS. Neither building, testing nor staging flashes a physical board.

1. **Flash/boot:** on the confirmed N16R8 board, use its CP2102 USB connector
   and identified serial port. From the repository root, substitute the port
   below. This explicitly writes E01 to the selected board. Expect the E01
   banner, `PSRAM: ready bytes=8388608`, `New MatmMax workspace.` and `>> `.

   ```sh
   esp32/scripts/idf.sh -C apps/esp32/e01_mathsmith \
       -B "$PWD/build/esp32/e01_mathsmith" \
       -D "SDKCONFIG=$PWD/build/esp32/e01_mathsmith/sdkconfig" \
       -p /dev/ttyUSB0 -b 115200 flash monitor
   ```

2. **Numbers:** type `x=41`, `x+1`, `format long`, `sqrt(2)`; expect 41, 42
   and approximately 1.41421356. `2^3^2` must print 64. Type `:mem` and record
   its whole output, including stack minimum.
3. **Matrices:** type `A=[4 -2;-2 4]; A\[2;8]` (2 then 3), `det(A)` (12),
   `M=eye(64); sum(sum(M*M))` (64). `M=ones(65)` must report a size/memory
   error; `sum(sum(M))` must still be 64. `x` must still be 41.
4. **Tools:** `integral(@sin,0,pi)` approximately 2;
   `fzero(@(t) t^2-2,1)` approximately 1.41421356;
   `deriv(@sin,1)` approximately 0.54030231.
5. **FFT:** `fft([1 2 3 4])` must match the four rows above.
   `F=fft([1 2 3 4]); y=ifft(F); y(:,1)` must give 1,2,3,4 and `y(:,2)`
   four zeros. `fft([1 2 3])` must also work (no power-of-two refusal).
6. **Programs/cancellation:** type `for k=1:3`, then `disp(k*k)`, then `end`;
   expect continuation prompts and 1,4,9. Type `while 1`, then `end`, wait
   briefly and press Esc: expect `interrupted` and a usable prompt. `x` must
   still be 41. Begin another `for k=1:3` block, press Esc before `end`, and
   verify it cancels. Record whether cancellation feels responsive.
7. **Repeated work/errors:** use `clear all`, then `A=ones(64); B=A*A; clear
   all` once to warm the scalar pool, and record `:mem`. Repeat that same
   allocation/clear line five times and record `:mem` again. Engine used
   payload should recover. `run missing` and `ask hello` must report errors;
   `2+2` must still give 4.
8. **Workspace/reboot:** `x=42;`, `:reset`, then `x` must report undefined.
   `:reboot` must return to the E01 banner/prompt and basic arithmetic must
   still work. Record all warnings, resets or stalls.

Only the user's hardware report closes E01. Emulator success does not close
E00 or E01's real-board validation.

Reference: [memory map](../../../esp32/docs/MEMORY_MAP_E01.md),
[shared engine/manual](../../m01_mathsmith/README.md),
[allocation API](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/mem_alloc.html),
[ESP timer](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/esp_timer.html).
