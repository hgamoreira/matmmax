# MatmMax OS development: emulation and desktop simulation

Updated 2026-11-15 (XD106–XD110): **Espressif QEMU and ESP-IDF v5.5.6 are
installed locally. E00 diagnostics, E01 MatmMax, E02 launcher and E03 external
ELF/serial upload pass 82 project tests, plus 35 upstream loader checks.** The
desktop OS simulator is not built.
RP2350 emulation remains an evaluation task.
The existing Linux application is an engine/front-end reference,
not an implementation of the proposed workstation OS.

## Existing firmware emulators

### ESP32-S3: Espressif QEMU

Espressif maintains a QEMU fork with ESP32-S3 CPU, memory and selected
peripheral emulation. ESP-IDF also provides `idf.py qemu monitor` and
`idf.py qemu gdb`. E00's tested project command is
`esp32/scripts/console.sh qemu`, which runs the merged image in the installed
emulator using a disposable flash copy.

The S3 documentation supports 8 MB PSRAM via `-m 8M` and octal PSRAM via
`-global driver=ssi_psram,property=is_octal,value=true`. E00's runner passes
these options directly to QEMU.
There is also a virtual display/framebuffer, which is a test device rather
than the physical LCD interface of our eventual workstation.

E00 uses it for automated boot, console and allocation checks; E01–E03 add
numeric engine, launcher, external ELF and serial-transfer checks. It does not establish real-board
timing, USB keyboard operation, Wi-Fi behaviour or wiring correctness.

Sources: [ESP-IDF QEMU guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/qemu.html),
[S3 QEMU details](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32s3/README.md).

#### Installed setup and repeatable checks

On x86_64 Linux, from the repository root:

```sh
tools/setup_esp_qemu.sh
python3 -m unittest discover -s esp32/tests -p test_qemu.py -v
```

The installer pins **esp_develop_9.2.2_20260417** (QEMU 9.2.2), using the
release recommended by Espressif's `tools/tools.json` when checked on
2026-11-14. The downloaded archive is SHA-256 checked:
`0eecb2a34a5586c0e59110f77b9343b7b336e82fdb0e1a30e1dc1bab8a547e35`.
An optional archive path allows an offline reinstall with the same check.
Source: [Espressif tool manifest](https://github.com/espressif/esp-idf/blob/master/tools/tools.json),
[pinned QEMU release](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417).

The executable is
`third_party/esp-qemu/esp_develop_9.2.2_20260417/bin/qemu-system-xtensa`.
Everything is inside the git-ignored `third_party/` directory; there is no
system package or global PATH change. E00 adds a separately installed SDK
and compiler below.
The required shared libraries were already present on this PC. Other Linux
hosts may need the dependencies listed in the ESP-IDF guide above.

**Measured here: two tests pass.** One uses QMP to inspect the memory map
for 16 MB flash and 8 MB PSRAM with octal mode enabled. The other executes
the bundled ROM with download-mode straps and checks its UART output:

```text
ESP-ROM:esp32s3-20210327
...
waiting for download
```

The tests use disposable blank flash files under `build/esp32/emulator/`,
no network interface and no physical serial port. The download prompt is
expected: there is no application image. This proves emulator startup and
configuration; they do not themselves test firmware allocations or the OS.

#### E00: built target firmware

```sh
tools/setup_esp_idf.sh
esp32/scripts/console.sh build
esp32/scripts/console.sh test
esp32/scripts/console.sh qemu
```

E00 pins ESP-IDF **v5.5.6** and adds seven firmware tests: boot/detected
memory, internal and 1 MiB external write/read patterns, oversize allocation
rejection and recovery, repeated allocations with the external heap restored,
bounded console input/editing/cancel, reboot and absent-PSRAM diagnostics.
Together with the two setup tests, **9 PASS on 2026-11-15**. The interactive
wrapper was also tried: `test` passed and Ctrl+A then X exited normally.

The user subsequently ran all commands in VS Code's terminal and reported
they all worked (2026-11-15). Their Ctrl+A exit attempt did not work; the
exact sequence and editor routing are unconfirmed. Exit requires Ctrl+A,
release both keys, then lowercase `x` without Ctrl. Ctrl+A alone has no visible
effect; Ctrl+A then `h` prints QEMU help. If necessary, run the same command
in an external terminal. See [QEMU's shortcut reference](https://www.qemu.org/docs/master/system/mux-chardev.html)
and [VS Code's terminal shortcut routing](https://code.visualstudio.com/docs/terminal/advanced#_keyboard-shortcuts-and-the-shell).
This user check remains emulator evidence, not hardware validation.
The user later confirmed the full exit sequence also fails in VS Code and
explicitly parked it to proceed. Its cause remains unconfirmed.

QEMU prints `rtcinit: o_code calibration fail` before the prompt; tests pass
with that warning. No calibration or timing result is inferred for hardware.
The actual board still needs the user's eight-step test. See
[E00 instructions](../apps/esp32/e00_console/README.md) and
[memory map](../esp32/docs/MEMORY_MAP_E00.md) for measurements, candidate and
remaining limits. E00 has no MatmMax engine, files, networking or launcher.

#### E01: MatmMax engine console

The user selected the next scope (XD108): reuse the existing engine with
double arithmetic, 4096 elements/matrix, FFTs and cooperative Esc cancellation.
It is a separate firmware, leaving E00's image available for diagnosis:

```sh
esp32/scripts/mathsmith.sh build
esp32/scripts/mathsmith.sh test
esp32/scripts/mathsmith.sh qemu
```

**23 combined tests PASS:** 9 E01 firmware tests, 5 native allocator/failure
tests, and the 9 E00/QEMU checks. The target agrees with Python numerical/FFT
references and 191 numbers from the original golden script run on the native
engine. E01 puts engine allocations in PSRAM with a 4 MiB payload cap; the
shared sources are not edited. It is an engine port, with no files/AI/plot
output/app launcher yet. The full QEMU exit-sequence issue is parked; `quit`
does not exit the emulator. Both E00 and E01 hardware validation stay open.
See [E01 README](../apps/esp32/e01_mathsmith/README.md) and
[E01 memory map](../esp32/docs/MEMORY_MAP_E01.md).

#### E02: other programs from MatmMax

The user selected an app launcher plus test program (XD109). E02 now has
`:apps` and `:app demo [arguments]`. The demo owns a `demo>` prompt; `exit`,
Esc or a controlled `fail` return to MatmMax with its variables/matrices/
functions still live. The portable C89 registry/arena lifecycle can be reused
by an RP2350 backend; there is no RP2350 build yet.

```sh
esp32/scripts/workstation.sh build
esp32/scripts/workstation.sh test
esp32/scripts/workstation.sh qemu
```

**48 combined tests PASS**: the previous 23, nine native launcher/demo tests,
and 16 E02 firmware tests including the full engine suite. They check admission,
cleanup on success/cancel/error, unknown/nested launch refusal, input boundaries,
workspace preservation and repeated launches with heap use restored.
E01 was rebuilt and its tests rerun because E02 adds build-conditional hooks
to the shared serial front end. No numeric/DOS/Linux source was changed.

Initial apps are compiled into firmware; loading program files from SD,
production app ports, files and physical peripherals remain later scopes.
All physical-board milestones remain open. Instructions and procedure:
[E02 README](../apps/esp32/e02_launcher/README.md),
[E02 map](../esp32/docs/MEMORY_MAP_E02.md),
[portable lifecycle](../common/workstation/README.md).

### ESP8266 D1 mini: target build, native checks and partial hardware validation

The user owns an ESP8266 Wemos D1 mini and has identified ESP8266EX / 4 MB
flash / 26 MHz crystal using esptool. They selected serial MatmMax Light
with doubles, fewer variables, small matrices and Wi-Fi off (XD111).

L00 now builds with pinned Arduino ESP8266 core 3.1.2 / L106 GCC10.3. Its
382464-byte corrected firmware fits the application flash window; static RAM is 47252
bytes including a dedicated 16 KiB continuation stack. First hardware heap/stack readings
are now recorded below; full procedure remains open. It has 16 variables, 64 elements/matrix,
a 16 KiB engine payload cap and no launcher/files/FFT.

```sh
python3 tools/setup_esp8266.py
esp8266/scripts/mathsmith.sh build
esp8266/scripts/mathsmith.sh test
```

**19 tests PASS:** native numerical/console/allocator/failure checks on the
generated smaller engine profile, plus linked-image checks for flash/RAM fit,
the dedicated stack, flash-byte handler and source/build freshness. Shared
engine files and the S3 images are untouched. Engine constants are mapped into
flash; the core's NON32XFER handler services byte accesses, with a performance
cost. The core's dedicated-stack entry avoids enlarging its borrowed SDK stack.
These are build/source decisions whose execution still needs the board test.

`mathsmith.sh native` runs a Linux logic/front-end harness, **not a chip
emulator**. The installed S3 QEMU workflow is not used as ESP8266 execution
evidence. No whole-firmware ESP8266 emulator or physical MatmMax run is
claimed. [L00 instructions](../apps/esp8266/l00_mathsmith/README.md) include
backup/flash/monitor commands and eight numbered hardware steps;
[memory map](../esp8266/docs/MEMORY_MAP_L00.md) distinguishes linked sizes from
unmeasured runtime values. Use the user's existing D1 mini to make that check
before adding apps. ESP32-S3/RP2350 hardware tests still wait for delivery.

The first hardware attempt produced continuous unreadable monitor output. A
confirmed image-layout defect was corrected: core checksum fields must stay
at fixed addresses before custom flash constants. New checks compare image
metadata/load segments with the ELF and reject corruption even with a valid
recomputed CRC. The original image fails; the corrected build passes 19 checks.
It has early 115200-baud startup markers; monitor baud can be set to 74880 for
ROM logs. A subsequent trace mapped to heap startup incorrectly placed in
flash before cache initialization, because a renamed archive member bypassed
the linker rule for umm_malloc.cpp.o. Preserving original basenames corrected
that startup path. The user then reported a usable prompt, help, 4x4 matrix
entry and correct eigenvalues. Cancellation, repeated cleanup and cold boot
still need the numbered board checks.

### Both platforms: Wokwi

Wokwi documents ESP32-S3 support, including configurable flash and PSRAM
sizes/types. It also documents an RP2350/Pico 2 model: one Cortex-M33 core,
UART, master-mode I2C/SPI, DMA, timers and partial PIO/USB support. USB support
listed for the RP2350 is serial CDC; do not infer keyboard-host support.

The RP2350 model is a Pico 2, not an established simulation of the ordered
SpotPear MINI-A or the future Pimoroni board. Support for their external
PSRAM arrangements and RP2350 double-precision coprocessor has not been
established by this review; prove required features with small firmware
checks before relying on it for the port. Verify simulator/SDK versions and
any service/licensing requirements before adopting it for the workflow.

No local RP2350 emulator is installed. Wokwi's browser simulator needs no
local installation; its VS Code extension requires a license activation
flow, which may involve signing into an account. We have not activated it
or established that it can cover the MINI-A's required features.

Sources: [Wokwi ESP32 features](https://docs.wokwi.com/guides/esp32),
[Wokwi Pico 2 features](https://docs.wokwi.com/parts/board-pi-pico-2),
[VS Code setup](https://docs.wokwi.com/vscode/getting-started).

## E03 installable programs: serial now, SD storage later

The user agreed to the recommended loader proof followed by serial upload
(XD110). E03 now loads a separately compiled native demo into PSRAM and returns
to MatmMax with its workspace intact. A versioned workstation API and bounded
MSAP package identify ESP32-S3/ABI v1. A and B are independently built files;
B's greeting is absent from the OS flash, proving execution of newly uploaded
code without an OS reflash.

```sh
tools/setup_elf_loader.sh
esp32/scripts/external.sh build
esp32/scripts/external.sh test
python3 esp32/scripts/upload_external.py --qemu build/esp32/e03_external/demo_b.msp
```

At the math prompt, `:app uploaded` enters B's `demo>` prompt; `exit`, Esc and
`fail` return normally, cancelled or with a controlled error. Ctrl+] closes the
uploader's host console; the older QEMU mux shortcut issue remains parked.
`external.sh qemu` starts without an uploaded app; `:app sample` loads the A
fixture embedded as package bytes. `external.sh apps` rebuilds only app files.

Pinned **Espressif ELF loader 2.1.0** is installed with archive SHA-256 checks;
local CMake integration compiles its unmodified sources with the existing pinned
SDK/toolchain. Broad SDK/libc exports and shared-library loading are disabled;
only `wk_app_api_v1` and `strcmp` resolve. Module code/data and the declared app
arena are released on ordinary return; uploaded package bytes remain until
replace/unload/reboot. Native code shares the firmware address space and stack,
so this does not provide isolation or hard-fault recovery.
Source: [Espressif ELF loader v2.1.0](https://components.espressif.com/components/espressif/elf_loader/versions/2.1.0/readme?language=en).

**82 project tests PASS**: all earlier 48, five native MSAP/CRC tests, 26 E03
firmware tests including the inherited numeric/launcher suite, and three host
CLI tests. They cover valid A/B replacement, bad CRC/target/ABI/ELF/imports,
partial-transfer timeout, old-slot/workspace preservation, fresh module state,
repeated loads with heap recovery, reboot/unload and absent PSRAM. The uploader
is exercised through both QEMU pipes and a QEMU-backed serial PTY. Upstream's
35 host loader tests also pass without skips, using original pinned fixtures.
E01/E02 were rebuilt and regression-tested after conditional front-end changes.

The two transfer/storage roles remain complementary:

- **USB serial/CP2102:** implemented byte transfer over UART0 115200 8N1, with
  256-byte acknowledged chunks, CRC32 and full validation/linking before slot
  replacement. One installed app is held in RAM; no persistent write occurs.
  Physical CP2102 reconnect/reset behaviour still needs hardware checks.
- **SD:** future removable persistent program/document/data storage. Once its
  interface/filesystem is selected and hardware-tested, it can supply the same
  package/loader contract. No SD driver, persistence or filesystem is in E03.
- **RP2350 and scripts:** target binaries need a separate compiler/loader
  backend; the Espressif component establishes no RP2350 support. MatmMax `.m`
  scripts use the interpreter once file services exist, rather than this ELF ABI.

See [E03 instructions and numbered hardware test](../apps/esp32/e03_external/README.md),
[memory map](../esp32/docs/MEMORY_MAP_E03.md),
[ABI/package format](WORKSTATION_APP_ABI.md) and
[serial protocol](../esp32/docs/SERIAL_UPLOAD.md). No physical board has been
flashed/tested; E00–E03 hardware milestones remain open.

## Proposed desktop version of the OS

Build the portable OS services and application code as a native Linux
program, using a desktop implementation of the same platform interfaces:

- Display and keyboard through a terminal or a window, to be chosen.
- Files through a dedicated test directory representing the device storage.
- Real server connections for AI/file integration and fake server responses
  for deterministic tests.
- Allocation accounting, configurable memory budgets and injected allocation
  failures. These test failure handling, not the chips' exact RAM layout.
- The same application registration, launch/return and session ownership code
  intended for firmware.

This would let the user exercise MatmMax, ported WordSmith/CodeSmith and
return to the preserved workspace on the development PC. Applications become
available as they are ported; the existing DOS executables do not become
native simulator modules automatically.

This is a platform simulator using native host instructions. A full chip
emulator additionally executes target instructions and models registers and
peripherals. Use the desktop build to test portable behaviour and firmware
emulation to test target builds. Compiler/libm differences, device timing,
memory mappings and drivers still need target checks.

Next: the user's E03 emulator check and E00–E03 hardware tests when the board
arrives, then agree the next application/storage step; scope the desktop
interface and evaluate RP2350's required emulator features separately.
Continue one app/milestone at a time.
Do not start implementing a new CPU emulator as a prerequisite. The full OS
implementation and RP2350 simulator adoption remain open.
Only the user's real-machine PASS/FAIL closes a hardware step.
