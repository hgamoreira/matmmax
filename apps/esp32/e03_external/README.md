# E03 — load external programs and upload them over serial

**Built and tested in QEMU, 2026-11-15: 82 project tests PASS, plus 35 upstream
loader tests PASS without skips. Hardware validation remains open.** User scope
(XD110): separately compiled native demo, then USB-serial transfer; SD storage
waits for hardware. E00/E01/E02 remain separate builds/candidates.

## Try it

From the repository root, with the existing SDK/QEMU installed:

```sh
tools/setup_elf_loader.sh
esp32/scripts/external.sh build
esp32/scripts/external.sh test
python3 esp32/scripts/upload_external.py --qemu build/esp32/e03_external/demo_b.msp
```

The last command boots a disposable E03 machine and installs the separately
built B package into RAM. It leaves a console open in an interactive terminal.
Type each line at its prompt; app switches discard queued typeahead:

```text
>> x=41
>> :apps
Applications:
  demo - Launcher test program
  sample - Separately built ELF demo A (loader fixture)
  uploaded - Serial-installed app (2260 bytes, RAM only)
>> :app uploaded
External demo B: loaded from a separately built ELF.
Demo program. Text is echoed here, not evaluated.
...
demo> x=999
Demo echo: x=999
demo> exit
Returned to MatmMax; workspace kept.
>> x+1
ans = 42
```

**Ctrl+] closes this uploader console** and terminates its disposable QEMU.
It is a separate host shortcut from the parked Ctrl+A/X problem in the older
QEMU runner. The `exit` above returns from the demo; it does not exit QEMU.

`esp32/scripts/external.sh qemu` starts E03 without an uploaded app. Use
`:app sample` to test loading A, or `:app demo` for E02's built-in comparison.
`external.sh apps` builds A/B packages without rebuilding/flashing the OS.

## Commands, ownership and limits

- MatmMax inherits E01's double arithmetic, 4096 elements/matrix, FFTs,
  cooperative cancellation, 4 MiB engine payload limit and UART0 115200 8N1.
  `:apps` and `:app NAME [literal arguments]` retain the E02 lifecycle.
- `sample` is an external ELF embedded as package bytes, relocated at runtime.
  `uploaded` is the single serial-installed package slot. B's distinctive
  greeting is absent from the OS firmware; uploading B runs new code without
  reflashing the OS. Finish/abort pending math blocks before app launch/upload.
- `:unload` removes the uploaded package. `:reset` resets MatmMax variables;
  `:reboot` resets the whole board and loses the uploaded app. There is no SD,
  filesystem, persistence or multiple installed filenames in this milestone.
- MSAP v1 packages identify ESP32-S3, Xtensa and ABI v1, with exact length and
  CRC32. Maximum complete package: 256 KiB; declared arena: 1 byte..1 MiB.
  The demo declares 4096 bytes. Upload is paced in 256-byte acknowledged chunks
  with a 5-second per-chunk receive deadline. The Python tool frames binary
  transfer; do not paste binary bytes into a terminal.
- Before replacing the old slot, validate and link the new candidate without
  executing it. Rejected or incomplete transfers preserve the old slot and
  math workspace. Each run allocates a zeroed arena and a fresh relocated ELF;
  ordinary exit, Esc and controlled errors release both. Only the stored
  package stays allocated between runs, until unload/replacement/reboot.
- All apps are trusted native code in the firmware address space/main stack.
  Normal-return cleanup does not provide process isolation or recover CPU
  faults. Only the ABI bootstrap and `strcmp` are exported, with broad SDK/libc
  tables disabled. RP2350 needs a separate target build and loader backend.

[Package and API contract](../../../docs/WORKSTATION_APP_ABI.md),
[serial protocol](../../../esp32/docs/SERIAL_UPLOAD.md),
[memory map](../../../esp32/docs/MEMORY_MAP_E03.md),
[external demo source](../../workstation/external_demo/README.md).

## Reproducibility and checks

The loader is pinned to Espressif `espressif/elf_loader` **2.1.0**, source
commit `cc06c1c383ba78f913596a176a4513c07657ede5`, archive SHA-256
`31a00c76ae203d2f29d257bde2c26fb0d79cb0ee46e3b67e61baa361f01a8e4c`.
The installer accepts a downloaded archive for an offline install. Local
CMake compiles unmodified upstream sources, with explicit version macros and
no component-manager dependency fetch. SDK/toolchain/QEMU pins remain E00's.
The numeric engine sources are reused without edits by this milestone.

The 82-test suite comprises the preceding 48 tests, 5 native package/CRC tests,
26 E03 firmware tests (including all 16 inherited numeric/launcher tests) and
3 host CLI tests. It covers malformed/truncated/wrong-target/ABI packages,
invalid ELF/imports, bad transfer CRC, interrupted uploads, atomic replacement,
reboot/unload, no-PSRAM refusal, fresh module `.bss`, workspace preservation and
repeated exit/cancel/error cycles with restored heap use. CLI checks exercise
both QEMU pipes and a QEMU-backed serial PTY, including closing/reopening the
host port without reflashing. E01/E02 were rebuilt and all prior tests rerun
after adding conditional hooks to their shared front end.

Upstream's 35 host checks use unmodified tests plus pinned original fixtures:

```sh
python3 tools/setup_elf_loader_tests.py
python3 esp32/scripts/test_elf_upstream.py
```

Logs: `build/esp32/e03-tests.log`, `build/esp32/e03-upstream-tests.log` and
`build/esp32/e03_external/test-logs/`. QEMU's existing calibration warning is
recorded; these checks do not establish board timing, PSRAM reliability or
CP2102 reset behaviour.

## Hardware candidate and numbered test (PASS/FAIL)

`esp32/scripts/external.sh release` stages `releases/esp32/e03_external/`:
full 16 MB `flash.bin` at offset **0x0**, ELF/map/sdkconfig, A/B packages,
instructions, memory/ABI/protocol documents, loader licence and SHA256SUMS.
Staging never opens a physical port. When the board arrives, run E00/E01/E02
hardware checks first. Then record PASS/FAIL for each step below:

1. **Flash/boot:** identify the N16R8 board's CP2102 port, substitute it below,
   and flash E03. Expect the E03 banner, PSRAM ready (8388608 bytes) and `>> `.

   ```sh
   esp32/scripts/idf.sh -C apps/esp32/e03_external \
       -B "$PWD/build/esp32/e03_external" \
       -D "SDKCONFIG=$PWD/build/esp32/e03_external/sdkconfig" \
       -p /dev/ttyUSB0 -b 115200 flash monitor
   ```

2. **Loader/return:** set `x=41; A=[1 2;3 4]; f=@(t) t+x;`. Run `:app sample`;
   expect **External demo A** and `demo>`. Type `x=999`, then `exit`.
   Verify `x+1` = 42, `A(2,1)` = 3, `f(1)` = 42 at the math prompt.
3. **Serial install:** finish at `>> ` and close IDF monitor (Ctrl+]). Run
   the following and keep its console open. Expect `INSTALLED uploaded 2260`.
   Record any reset caused by closing/opening the physical port; that reset
   can clear variables, so set the values from step 2 again after connection.

   ```sh
   python3 esp32/scripts/upload_external.py --port /dev/ttyUSB0 \
       build/esp32/e03_external/demo_b.msp
   ```

4. **New program:** `:apps` must list `uploaded`. Run `:app uploaded hello`;
   expect **External demo B**, arguments `hello`, and `demo>`. Exit and verify
   the variables/matrix/function just set. No OS rebuild/flash occurs here.
5. **Cancel/error:** run uploaded B and press Esc, then reopen and type `fail`.
   Expect cancellation/error messages, a usable math prompt, preserved values
   and a successful later launch. These are controlled returns, not CPU faults.
6. **Cleanup:** warm by running/exiting once; record `:mem`. Repeat normal,
   Esc and `fail` returns at least twice each, then record `:mem` again. Live
   engine payload and external free heap should recover; high-water counters
   may change. `:unload` must remove `uploaded` and free its stored package;
   `:app uploaded` then reports no uploaded app. `:app sample` still works.
7. **Replacement:** close the host console with Ctrl+] and upload A using the
   same physical-port command with `sample.msp`. Run it and expect A. Close,
   upload B again and expect B. Record unexpected resets or lost input; actual
   CP2102 reconnect behaviour remains a hardware question, and a reset loses
   a RAM-only install/workspace.
8. **Restart:** while B is installed, `:reboot`. Expect E03 and a usable math
   prompt, `uploaded` absent from `:apps`, and `:app uploaded` refused. Run
   `:app sample`, exit and verify arithmetic. A fresh serial upload must work.

Report steps 1–8, memory outputs and any warnings/stalls/resets. No physical
board was flashed or validated during development; only the user's real-board
report closes this hardware milestone.
