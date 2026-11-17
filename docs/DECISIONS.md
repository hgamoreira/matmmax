# Portable workstation decisions

Excerpt from the workstation project; identifiers retained.

## XD103 — Separate drafts for two portable workstation routes (2026-11-11, the user's request)

The user asked for development directories and first-draft READMEs for both
alternatives discussed: **RP2350 with PSRAM** and **Pi Zero W running Linux**.
Create [rp2350/](../rp2350/README.md) and [pizero/](../pizero/README.md) as
platform development areas, following XD95's organisation. This records the
directions and open questions; it does not start a firmware/application port or
settle every draft recommendation.

- **RP2350:** native firmware, with MathSmith as the proposed first application.
  The RP2350B-MINI-A With-PSRAM board examined has 8 MB external PSRAM and is a
  candidate, not hardware reported owned or validated. Toolchain, peripherals,
  limits and subsequent applications remain to be scoped.
- **Pi Zero W:** a portable Linux computer for the whole suite and future
  programs. The user accepts a terminal interface without a full desktop and
  development in a Linux VM. A Pi 3 is not required. Corrected owned inventory:
  two original Pi Zeros, one Zero W, one Pi 2 and one Pi 3; no Zero 2/Zero 2 W.
  ARMv6 builds and real Zero W tests are needed; the existing PC Linux release
  is not a Pi binary.
- **Portable hardware:** a small keyboard and portable screen are desired.
  CardKB v1.1 I2C is a budget keyboard candidate. The user's monochrome RCA CRT
  comes from a video doorbell; its power, video timing and text readability
  remain unknown. No new keyboard, display or RP2350 purchase was reported.
- **Existing layout:** `pi/` remains the old machines' gateway, `linux/` remains
  the existing PC Linux platform, and application sources stay in place. Share
  reusable logic and tests; choose build integration when implementing a step.
- **Status:** documentation only. No new platform build, VM/system image,
  deployment or hardware PASS is claimed. Each route's README lists proposed
  steps and unresolved choices for a later session.

## XD104 — MathSmith is the RP2350's main environment and application launcher (2026-11-11, the user's direction)

The user wants to use MathSmith as the RP2350 machine's OS-like environment and
open WordSmith and future programs from there. The intended experience is to boot
into MathSmith and return to its session after closing another application.
The [RP2350 draft](../rp2350/README.md) now describes that direction.

- **User-facing environment:** MathSmith's prompt, calculation workspace and
  `.m` scripts, extended with an application launch mechanism.
- **Proposed architecture:** shared hardware/platform services underneath the
  applications, with a launcher independent of the numeric evaluator. Initially
  compile native app modules into one firmware image and run one foreground app
  at a time, retaining MathSmith's session during normal launch/return.
- **Behaviour to prove:** preserve variables/history, restore input/display,
  reclaim application buffers and refuse launches that cannot fit without
  destroying the session. Test with a small app before porting WordSmith.
- **Still open:** exact commands/shortcuts, memory limits, app API, launch from
  scripts and any later loading of separately compiled programs. No multitasking
  or process isolation is promised by this draft.
- **Status:** design documentation only; no launcher, app switch or new hardware
  test has been implemented. The Pi Zero W remains the separate Linux route.

## XD105 — Port to both ESP32 and RP2350; ESP32 first while sourcing RP2350 (2026-11-14, the user's direction)

The user chose to port for both microcontroller platforms. They report that
the Pimoroni Pico Plus 2 W is out of stock and ESP32 boards are easier to
source from AliExpress. RP2350 remains a desired target, but sourcing it may
take time. This is a record of the user's availability report, not a stock
check or a purchase.

- **Sequence:** start the ESP32 route while sourcing an RP2350 board. Keep
  [esp32/](../esp32/README.md) and [rp2350/](../rp2350/README.md) as separate
  platform development areas; neither replaces the Pi Zero W Linux route.
- **ESP32 candidate:** ESP32-S3 with PSRAM, as proposed in the comparison.
  The exact board and memory configuration have not been selected. This is a
  workstation port, separate from the existing ESP32-WROOM-32 serial gateway
  in `../esp32-serial-wifi`.
- **Shared behaviour:** reuse the MathSmith engine and its double-precision
  numeric representation, with hardware/SDK services supplied by each port.
  The intended MathSmith environment and application launch/return of XD104
  apply to both. Add programs one at a time; do not duplicate or rewrite the
  validated engines as part of choosing hardware.
- **Open before implementation:** exact board, SDK/toolchain version, first
  console connection and memory budget; keyboard, display, storage and later
  networking scope. A serial-console smoke test followed by engine checks is
  the proposed first sequence, not firmware already built.
- **Status:** documentation only; no toolchain installed, firmware built or
  flashed, speed comparison measured, or new hardware test passed.

### XD105 amendment — both initial boards ordered; wireless RP2350 later (2026-11-14, user report)

The user reports ordering the **ESP32-S3 N16R8 CP2102 development board**
(16 MB flash, 8 MB PSRAM), with the pictured **44-pin ESP32-S3 screw-terminal
breakout**, and the **RP2350B Tiny with 8 MB PSRAM**. The supplied RP2350 image
identifies the board as **SpotPear RP2350B-MINI-A**, now the first selected
RP2350 target rather than only a candidate.

The **Pimoroni Pico Plus 2 W** is a later planned purchase when stock returns,
for onboard wireless and access to the AI assistant on the external model
server. It is not reported ordered. Use the same RP2350 application logic
with board-specific configuration and network integration when that target
is available. The ESP32-S3 can also serve the network/AI direction; the MINI-A
has no onboard wireless and starts with offline functionality unless another
transport is agreed.

Neither ordered board is reported delivered or tested. Confirm board revisions,
actual flash/PSRAM and ESP32 breakout fit/pin mapping on arrival. Peripherals,
SDK versions and first implementation scopes remain open; this records
hardware selection, not completed firmware or a hardware PASS.

## XD106 — Install Espressif QEMU first for the ESP32-S3 route (2026-11-14)

The user wants an emulator installed to test the MathSmith-based OS for both
microcontroller routes. Following the ESP32-first sequence, install Espressif's
QEMU fork locally under git-ignored `third_party/`, with a pinned release and
SHA-256, and automated checks before beginning firmware implementation.
`tools/setup_esp_qemu.sh` installs **esp_develop_9.2.2_20260417** for x86_64
Linux without system-wide changes; this does not select an ESP-IDF SDK version.

`esp32/tests/test_qemu.py`: **2 PASS** — a 16 MB flash/8 MB PSRAM memory map
with octal mode enabled, and the bundled ROM reaching its UART download prompt.
These check the installed emulator, not application boot or PSRAM allocation
from firmware. No firmware, numeric comparison, OS lifecycle or hardware PASS.

For RP2350, Wokwi's Pico 2 model remains a candidate. No local install/account
activation is performed: the MINI-A's external PSRAM and required coprocessor
behaviour still need evaluation. Keep the planned native desktop simulation
for shared OS/app behaviour; no new CPU emulator is required. SDK/toolchain,
console-firmware scope and peripherals remain open. Commands, limitations and
sources: [emulation notes](MATHSMITH_EMULATION.md).

## XD107 — E00, the minimal ESP32-S3 console firmware (2026-11-15)

The user agreed to begin the minimal console firmware and selected the
recommended scope: **ESP32-S3 first, stable pinned ESP-IDF, CP2102/UART0 at
115200 baud, boot/input/output and PSRAM/allocation checks in QEMU**, with a
hardware procedure for the board's arrival. RP2350 stays a subsequent port.

Use ESP-IDF **v5.5.6**, commit
`880a203ae603d08a94d06aa01e0d6761958e5e9e`, with its manifest-selected tools
and constrained Python dependencies, separate from the host server's venv.
SDK, tool packages and Python environment live under `third_party/`.
Sources: `apps/esp32/e00_console/`; commands: `esp32/scripts/console.sh`;
generated output: `build/esp32/e00_console/`, `releases/esp32/e00_console/`.

Target the ordered N16R8 configuration: 16 MB flash (DIO at 40 MHz), 8 MB
octal PSRAM (40 MHz). Keep the default malloc/task stacks in internal RAM
and allocate external test buffers explicitly through the capability allocator.
Permit the console to start if PSRAM is absent, then report a failed memory
diagnostic. This makes a board/configuration fault inspectable. No networking,
filesystem, MathSmith engine or launcher belongs to E00.

The diagnostic prompt provides `help`, `info`, `mem`, `test`, `echo TEXT` and
`reboot`. The memory test checks buffer location and two patterns in 4 KB
internal RAM and 1 MB external RAM, rejects a request larger than physical
PSRAM, proves allocation works afterwards, frees its buffers and checks heap
integrity. Input is bounded; an oversized line is discarded, backspace edits,
and Esc cancels. QEMU uses temporary flash copies and no network interface.
The UART transcript replaces the DOS-only XTDUMP screen hook for this console.

**Built and emulator-tested (2026-11-15): 9 PASS** (7 firmware, 2 setup).
Repeated memory tests recover the external heap; the interactive runner also
passes `test` and exits with Ctrl+A/X. `ms_console.bin` is 209520 bytes;
377608 internal and 8383888 external bytes free at the ready prompt in QEMU.
The candidate is staged in `releases/esp32/e00_console/` with hashes and the
memory map. QEMU's `rtcinit: o_code calibration fail` startup warning is
recorded, with no real-board calibration/timing inference. No board flashed.
Only the user's eight-step hardware PASS/FAIL can close E00; its README has
the procedure. The engine and launcher remain subsequent scopes.

## XD108 — E01, MathSmith's existing engine on the ESP32-S3 console (2026-11-15)

The user confirmed the QEMU exit sequence still fails in VS Code, parked it
as non-blocking, and authorised proceeding. E00's real-board test remains
open. They selected the recommended E01 scope: serial prompt, existing engine,
64-bit `double`, 4096 elements/matrix (64x64 square), FFTs and cooperative Esc
cancellation, tested in QEMU against Python references. Files, plot output and
application launching remain subsequent scopes.

Create a separate ESP-IDF project at `apps/esp32/e01_mathsmith/` with commands
in `esp32/scripts/mathsmith.sh`. Keep E00's image and procedure available.
Compile the existing engine/matrix/linear algebra/FFT/plot sources directly;
do not fork them or change the DOS/Linux builds. Use the default 40-variable
limits and full FFT implementation, not `MS_486` or the 16-bit FFT restriction.

A forced include for these target translation units adapts allocations to
PSRAM, with a conservative **4 MiB payload cap**, accounting/peak reporting
and correct calloc/realloc failure behaviour. Allocation metadata is additional.
Native tests can lower the cap to inject failures; firmware exposes no tuning
command yet. Keep SDK malloc/task stacks internal, with a **32 KiB main stack**.
The same include maps `clock()` to ESP timer for tic/toc and renames the
portable plot source's static `finite` helper after including math.h, avoiding
newlib's public symbol. No validated shared source is edited.

Use `>> ` and `..> ` prompts, 399 printable characters per line, and platform
commands prefixed `:` to avoid claiming mathematical variable names. Esc stops
at engine poll points and drops queued input; pending multiline blocks can be
aborted. Future files/AI/plots/launcher are described explicitly as unavailable.
**Built and tested:** 23 combined tests PASS (9 E01 firmware, 5 native
allocation/failure, 9 E00/QEMU). The 191-number golden script matches the
native engine; FFTs match Python's definition and cancellation preserves the
workspace. The original engine/DOS/Linux sources are unchanged. E00's test
harness gained only an optional build directory for reuse; its tests reran.
Native allocation failures preserve realloc data and the engine workspace;
no firmware injector is exposed. All memory/size figures, current representation
limits and hardware steps are in E01's README and memory map.

`releases/esp32/e01_mathsmith/` is a separate hashed hardware candidate. No
physical board flashed, no hardware PASS, and E00 is still open. The next
scope is not selected. The full QEMU exit-sequence failure remains parked.

## XD109 — E02, run a foreground program and return to MathSmith (2026-11-15)

The user reiterated that the OS must run other programs and explicitly chose
**launcher plus test program** as the next step. Initial apps are native modules
compiled into the same firmware, one foreground program at a time. Separate
program files from SD, WordSmith, storage and physical keyboard/display drivers
remain later scopes. E00/E01's hardware procedures stay open.

Create E02 separately at `apps/esp32/e02_launcher/`, with platform commands in
`esp32/scripts/workstation.sh`. The platform-neutral C89 registry/lifecycle is
`common/workstation/`; the portable test program is `apps/workstation/demo/`.
E02 reuses E01's engine, allocator and serial front end. The latter gains hooks
behind `MS_WORKSTATION_LAUNCHER`; build E01 without that define and rerun its
regression tests. No shared numeric engine or DOS/Linux routine changes.

`:apps` lists registered programs; `:app NAME [arguments]` launches one from
the prompt. Launching while a MathSmith block is unfinished is refused, leaving
the pending block intact. MathSmith stays resident; apps receive no session
pointer. The demo has its own `demo>` prompt and echoes text rather than
evaluating it. `exit` returns normally, Esc cancels, and `fail` demonstrates a
controlled application error return. MathSmith variables/matrices/functions
must remain usable afterwards.

Each program declares its scratch-arena size; the demo gets **4096 PSRAM
bytes**, outside the engine payload budget. Admit a launch only after that
allocation succeeds. The launcher zeroes the arena, sets active ownership,
passes console services and literal argument text, then frees the original
arena and restores input ownership on normal, cancelled or error returns.
Queued input is discarded on entry/return. Unknown apps and insufficient
memory return without running a program; nested launches are refused.
This is a trusted, shared-address-space model: no process isolation,
multitasking, executable loader or recovery from an app hard fault is claimed.

**Built and emulator-tested (2026-11-15): 48 tests PASS**, comprising the
previous 23, nine native launcher/demo tests and 16 E02 whole-firmware tests
(the full nine engine tests plus seven launch/return tests). Normal exit,
cancellation, controlled errors and repeated launches preserve math values/
functions and restore arena/heap use. Native tests verify zeroing, admission
failure/recovery, duplicate registry/nested-launch refusal and freeing the
original arena. Firmware also refuses arena allocation with PSRAM absent.
E01 was rebuilt and regression-tested after the conditional front-end hooks;
no numeric/DOS/Linux source changed. Its build hash changes, although its
tested E01 behaviour and 338240-byte binary size are preserved.

The interactive example was also run: `x=41`, open demo, echo `x=999`, exit,
then `x+1` is 42. E02 binary: 341024 bytes; its hardware candidate and memory
map are separate from E00/E01. No physical board flashed/tested. Only user
hardware PASS/FAIL closes the hardware milestones. Further app/storage/UI
scope is not selected; the QEMU exit shortcut remains parked.

## XD110 — E03, external ELF programs and serial transfer before SD (2026-11-15)

The user agreed to the recommended sequence: load one separately compiled
program in QEMU, then add serial upload, with SD hardware/storage later.
E03 uses a RAM-only uploaded slot plus a separately built embedded test fixture;
it does not implement persistence, a filesystem, production apps or an RP2350
loader. E00/E01/E02 hardware validation stays open.

Pin Espressif ELF loader **v2.1.0** with its registry archive SHA-256 and source
commit; compile its unmodified C sources using local CMake integration. The
version-only cmake_utilities dependency is unnecessary for this integration.
Keep dynamic shared-library support and the broad SDK/libc export tables off.
The only module imports are `wk_app_api_v1` and `strcmp`. ABI v1 supplies a
version/size, borrowed `WkAppContext` and finish callback; no MathSmith pointer
or SDK services are exported. Trusted modules share the address space; this
does not add process isolation or hard-fault recovery.

Native packages use **MSAP v1**: a 32-byte little-endian header followed by
the target ELF, with target/ABI, declared arena size, exact image length and
CRC32. Cap complete input at **256 KiB**, arena at **1 MiB**. The S3 loader
relocates executable code into PSRAM, then deinitialises it on app return;
the existing launcher owns the arena and console switch. `.m` files remain
a different interpreter/file-services scope.

USB serial uses UART0's existing 115200 8N1 settings, a bounded header,
256-byte stop-and-wait chunks and CRC32. Validate/link the complete candidate
without executing it before replacing the old RAM slot. Reject malformed,
wrong-target/ABI, incomplete and incompatible files while preserving the old
slot/workspace. The host tool uses Python stdlib; its QEMU mode starts a
disposable machine and waits for the prompt before sending UART bytes.
Real serial-port/reset behaviour still needs the user's hardware test.

The embedded A and uploaded B variants are built independently of the OS.
The B greeting is verified absent from firmware, proving a newly transferred
program runs without reflashing it. Each recorded package is 2260 bytes.
The package carries both ELF machine 94 and specific target identifier 1
(ESP32-S3), avoiding acceptance of a different SoC with the same instruction set.

**Built and emulator-tested: 82 project tests PASS**, including all 48 prior
checks, five native package/CRC checks, 26 E03 firmware checks (16 inherited)
and three host CLI checks. Failure/timeout/replacement, fresh module state,
workspace preservation and repeated cleanup pass. Both QEMU pipes and a
QEMU-backed serial PTY exercise the uploader, including host-port reconnect.
Upstream's **35 host tests PASS without skips**, using pinned original fixtures
and unmodified tests with checkout paths rebound. E01/E02 were rebuilt and
regression-tested after conditional shared-front-end hooks; numeric engine
sources were not edited by this milestone. Candidate/instructions/memory map
are under `releases/esp32/e03_external/`, `apps/esp32/e03_external/README.md`
and `esp32/docs/MEMORY_MAP_E03.md`. Full ABI/protocol documents live in
`docs/WORKSTATION_APP_ABI.md` and `esp32/docs/SERIAL_UPLOAD.md`.

No physical board was flashed/tested. Real PSRAM execution/cache and CP2102
reconnect/reset behaviour remain hardware questions; RAM installs disappear
on reset. The uploader's optional console exits with Ctrl+], a separate host
control from the parked old QEMU mux shortcut issue. Changes stay uncommitted.


## XD111 — ESP8266 D1 mini, serial MathSmith Light first (2026-11-15)

The user has a Wemos D1 mini, identified with esptool as ESP8266EX / 4 MB
flash / 26 MHz crystal, and selected a serial-only MathSmith feasibility build
with 64-bit doubles, fewer variables, provisional 8x8 matrices, Wi-Fi off and
memory diagnostics. Measure fit before adding an app launcher; FFT, graphics,
files, editors and external native modules are deferred. ESP32/RP2350 boards
have not arrived; their hardware milestones remain open.

Use pinned ESP8266 Arduino core 3.1.2 and its matching L106 GCC 10.3.0
(3.1.0-gcc10.3-e5f9fec), with package-manifest SHA-256 checks and local installs.
The build follows the core's platform recipes without requiring Arduino IDE.
Generate explicit smaller source configuration in build/, with anchor checks
and original-source hashes; do not edit the shared validated engine.
Initial limits: 16 variables, 64 elements/matrix (8x8 square, also longer
vectors), four functions of each kind, recursion depth four, 16 KiB engine
payload cap and 16 KiB continuation stack. Add parser stack-reserve guards
in the generated profile and bounded input nesting at the front end.

Place engine constants in mapped flash and enable the core's documented
NON32XFER handler for byte access; this avoids copying its large string tables
into DRAM but has a performance cost requiring real-board measurement.
Use balanced 32 KiB cache / 32 KiB IRAM, DIO 40 MHz flash, CPU 80 MHz,
26 MHz crystal, UART0 115200 8N1 and no filesystem. Firmware fit and native
checks are being measured; no ESP8266 emulator/hardware execution is claimed.


**L00 build/check result:** 16 automated native/linked-image tests PASS. Final
firmware binary 382256 bytes; static .data/.rodata/.bss 47252 bytes, flash-mapped
code/constants 349468 bytes. Actual runtime free heap/stack is unmeasured.
Formatting buffers are reduced to 128/256 and typed statements to 128.
The core's dedicated BSS continuation stack is explicitly linked with
`disable_extra4k_at_link_time`; its normal borrowed SYS-stack path must not be
used with the enlarged 16 KiB continuation. A linked-image check verifies its
16424-byte object, flash-byte handler, memory bounds and source freshness.
Generated source guards preserve normal error returns when stack reserve is low.

Candidate/instructions: `releases/esp8266/l00_mathsmith/`,
`apps/esp8266/l00_mathsmith/README.md`, `esp8266/docs/MEMORY_MAP_L00.md`.
Build/test/release never open a board. Explicit backup/flash/monitor commands
use the existing esptool 4.13/pyserial environment; no hardware action has been
run by the assistant. No ESP8266 whole-firmware emulator run is claimed.
Only the user's numbered hardware report closes L00; all changes stay uncommitted.


**L00 first hardware attempt/correction:** the user reports continuous unreadable
monitor output; no working prompt or readable reset reason yet. Review found
custom flash constants displaced fixed CRC metadata while elf2bin still patched
0x1010/0x1014, corrupting engine bytes. Constants now follow the core CRC/version
prefix. Build/release/flash validate metadata, whole-image CRC and all app load
segments against ELF; negative tests include corruption with recomputed CRC.
The original image fails the new check; corrected image passes all18 tests.
Current binary382384bytes, IROM349596bytes; RAM unchanged. Startup markers and
optional monitor baud help collect further evidence. Actual causality of the
reported scroll and hardware success remain unconfirmed pending user retest.


**L00 allocator startup correction:** user trace `epc1=0x40219dac`, fatal
exception0, maps exactly to umm_init before SDK/cache initialization in the prior
image. Renaming the nested core object defeated the linker's explicit
`EXCLUDE_FILE(umm_malloc.cpp.o)`, so the heap startup was incorrectly in flash.
Preserve original object basenames; reject collisions. Require early entry/heap
init functions in IRAM in build/release/flash checks, and test archive names.
Old image fails; corrected build passes19 checks. Current binary382464bytes,
IROM347676bytes, IRAM27239bytes; DRAM/stack unchanged. Hardware retest remains
required; no working prompt/PASS is reported yet.


**L00 hardware report received:** after the allocator-placement correction the
user confirms readable boot/console/help and a 4x4 matrix/eigenvalue calculation
on the physical D1 mini. Exact integer characteristic-polynomial expansion
verifies the displayed eigenvalues. After that workload: free heap27888bytes,
largest block27304, fragmentation3%, payload5072/peak5664, minimum free stack
11596/16384. Startup free heap33264 precedes engine allocation and must not be
reported as the ready-workspace baseline. Transcript is in L00's hw directory.
Full numbered validation remains open for unreported precision/bounds/functions/
cancellation/cleanup/restart checks. No firmware change is made for this report;
no reflash needed to continue the procedure.

## XD112 — ESP8266 L01 browser console and direct GPIO commands (2026-11-16)

The user selects browser work after suggesting a MicroPython-like WebREPL,
and asks MathSmith to expose pins through direct commands. Implement a separate
L01 build using the existing bounded light engine: single browser controlling
one shared RAM workspace, a self-contained page in flash, bounded WebSocket
transport, cooperative Stop, serial setup/recovery and browser-owned history.
No MicroPython protocol compatibility is implied. L00 remains a recovery image.

Network configuration initially uses serial `:wifi join "SSID" "PASSWORD"`,
RAM only, with credentials masked in firmware echo. A random per-boot access
key is printed over serial; upgrade checks both the key and same-origin Host/
Origin. First scope uses plain HTTP/WebSocket on the trusted LAN, no TLS or
Internet exposure. Serial Ctrl+C reclaims ownership and cancels work; a browser
cannot change Wi-Fi settings. Reconnect keeps completed assignments and discards
partial input/blocks. No simultaneous evaluation by serial/browser.

Direct platform commands: `:pins`, `:pin Dn input|pullup|output [0|1]`, `:read Dn`,
`:write Dn 0|1` and `:analog A0`. D0..D8 use the pinned D1 mini mapping. GPIO1/3
and flash GPIO6..11 are not exposed. D0 has no INPUT_PULLUP; D3/D4/D8 are
boot-strapping pins and D4's board LED is active low. Digital signals are 3.3 V.
A0 reads raw counts; board revision/divider determines allowed external voltage.
These are prompt commands, not math functions callable in engine loops yet.
No unsolicited output-pin changes at startup/reset; a configured output retains
its level across a MathSmith workspace reset and browser disconnect.

The initial same math limits are provisional until Wi-Fi-active real-board
measurements. Validate parsers, real native sockets, browser UI and target image
off-board; only the user's numbered board report closes L01. Physical GPIO,
radio/heap/stack/watchdog behaviour cannot be validated with host stubs. No
display/keyboard drivers, AI, plots, file storage or app loading in this step.

**Serial-entry correction after user report:** initially both inline Wi-Fi
fields were masked and terminal arrow Esc prefixes cancelled the line, leaving
`[A`/`[B` text. L01 now offers `:wifi join` alone with visible SSID and masked
password prompts, bounds/backspace/cancel and cleared credential buffers.
Inline setup remains available and masks only the password. Fixed-state CSI/
SS3 decoding ignores unsupported serial editing keys; standalone Esc has a
75 ms disambiguation timeout, while Ctrl+C and browser Stop stay immediate.
Web ownership upgrades are refused during guided serial setup. This keeps the
small end-of-line editor without treating cursor keys as cancellation.
The input fix is checked off-board; successful physical Wi-Fi/browser use
still requires the user's report.

**Asynchronous join clarification:** a selected answer initially said a browser
URL appeared, but the user's explicit correction withdraws that report: no URL
appears and status remains7. Wi-Fi association/IP is unconfirmed. In
pinned core3.1.2, `status()` maps raw SDK STATION_CONNECTING to status7 through
its default case, so status7 alone is not a failed-join diagnosis. Optional r3
diagnostics expose raw SDK state, mode, begin status, elapsed time and the last
disconnect reason; mode-enable failure is checked. A small retained event
handler stores only reason/count. Joining remains asynchronous, with no forced
30-second blocking wait or credential/SSID printing in diagnostics. The user
confirms they are on r2; r3 diagnostics are needed before inferring a cause.

For this unresolved L01 issue, a separately built `wifi_probe` diagnostic
omits the engine/web server and uses a dedicated4KiB stack with the same
pinned radio/SDK settings. It preserves L00/L01 candidates and accepts RAM-only
credentials on serial. Use r3 diagnostics first; the probe is a fallback
comparison, not a claimed fix or new validated milestone. An identical SDK/
toolchain is not an independent reference for all possible build defects.

**R4 IP readiness correction:** the user's r3 log remains SDK1/status7 through
140seconds with zero disconnect events; measured free heap21288 and stack
minimum-free15096 do not establish exhaustion. Upstream issue7432 reports a
stale SDK connection status despite a valid IP event. This is not yet proven on
the user's board. Observe association, IP assignment, disconnect and DHCP timeout
independently, expose actual IP, and gate the browser on usable IP readiness
instead of status3 alone. Explicit link-down/timeout invalidates readiness even
if old SDK status/IP lingers. New native regressions cover those states. Four
retained event handlers add some runtime allocations; measure on hardware.
After30seconds, diagnostics stop advising indefinite waiting. User's full radio/
browser result remains open. Source: https://github.com/esp8266/Arduino/issues/7432.

**Subsequent physical report:** after r4 was supplied the user explicitly reports
browser connection working at192.168.10.157. Record browser reachability as
confirmed, with exact mechanism/version counters and the remaining numerical,
memory, cancellation/reconnect and GPIO tests still unreported. No new firmware
change is made for that report; the whole L01 procedure remains open.

The next `:mem` explicitly identifies L01-r4 with browser-workload free
heap18696/largest13824/fragmentation23%, continuation minimum-free11708/16384
and engine live4984/peak5329. The user then confirms both requested numerical
results (4 for 2+2, column[2;3] for the 2x2 solve). Record these limited hardware
passes; no limit increase or firmware change follows this snapshot. Repeated
memory stability, Stop/reconnect and electrical GPIO/ADC remain unreported.

## XD113 — Saved Wi-Fi and autonomous browser boot on ESP8266 (2026-11-16)

The user confirms browser disconnect/reconnect retains `x=41` and returns42,
then requests Wi-Fi settings stored once and replaceable later. R5 saves
SSID/password and the browser key after a usable connection to the requested
SSID is observed. Boot loads those settings and joins without a USB terminal.
Network replacement uses serial `:wifi join`; a failed join retains the previous
saved record. Serial `:wifi forget` logically removes auto-connect/key and
cleans up old credentials; `:wifi off` affects the current session only.
Workspace values remain RAM-only. The SDK's credentials persistence remains off.

An explicit join disconnects the current station first without erasing SDK
credentials, ensuring fresh events even when retrying the same active network.
This permits retrying a failed settings write rather than relying on `begin`
to emit events for an unchanged connected configuration.

Reserve physical flash sectors0x3fa000 and0x3fb000 for two versioned records.
The L01 linker moves the empty filesystem/temporary-sketch boundary to0x3fa000
while retaining EEPROM at0x3fb000 and RF/SDK sectors from0x3fc000. There is no
mounted filesystem or OTA feature. The raw adapter refuses I/O if these linker
addresses or the 4MB flash size disagree. Each record is160bytes with explicit
encoding, generation, CRC32 and a commit word written last. Update the inactive
sector and verify it before considering a save successful; identical settings
perform no write. Interrupted writes preserve the previous committed record.
Forget commits a newer tombstone before erasing the previous live record, so
an interrupted cleanup cannot reactivate the old configuration. Retry cleanup
on a reported failure. Settings are not encrypted on flash.

The saved browser key remains stable over reboot and successful network changes.
Forget rotates it. Opt-in browser local storage can remember the key at that
origin. Best-effort mDNS advertises mathsmith.local and HTTP port80; the bounded
HTTP/WebSocket parser allows only this fixed hostname or the numeric device IP,
with Origin still required to match Host. DNS/MDNS failure must leave numeric-IP
and serial recovery available. Measure added mDNS/event/settings memory on the
board; native file-backed restart/fault tests do not establish flash/radio PASS.

**R6 correction after the R5 board report:** the user observes association but
no usable IP or GotIP event through98seconds. Saved=no/pending=yes; free heap
19688/largest19168 and continuation minimum-free15064 do not show obvious idle
exhaustion. No DHCP client-state/packet trace exists yet. Restore the working
R4 cold mode(STA)/begin sequence, removing R5's unconditional disconnect after
enabling a new station. Replacing an existing station attempt uses modeOFF,
a brief yield, then modeSTA/begin for fresh events. Enable reconnect policy
after readiness. At15seconds of association without a usable address, attempt
one DHCP stop/config restart per join/boot in the outer loop. Expose SDK DHCP
state and recovery attempted. Preserve all matching-SSID/fresh-IP save checks,
the previous committed settings and numeric-IP fallback. No guessed static
address or indefinite recovery loop. This is a candidate recovery policy;
the exact original stall cause and physical R6 result remain unconfirmed.
Pinned core's zero-address config path reenables DHCP:
https://github.com/esp8266/Arduino/blob/3.1.2/libraries/ESP8266WiFi/src/ESP8266WiFiSTA.cpp.

**R6 physical result:** restart executes and DHCP reports enabled, but no IP
arrives through52seconds. Settings remain unsaved. Record this recovery attempt
as unsuccessful. Before another firmware change, compare the exact archived R4
binary on the same network. Its SHA/image/ELF checks are verified; `webrepl.sh
flash-r4 PORT` selects it explicitly and ordinary flash still selects R6. The
host checks do not establish real DHCP exchange or a router/core fault. See
`esp8266/docs/R4_COMPARISON.md`. Saved-network startup is still outstanding.

**Later R6 hotspot validation:** join/save, software-reboot load/auto-join/key
retention, cold USB-charger startup and browser math receive user PASS. Current
home R4/R6 attempts still fail. Altibox app shows esp-40573A at192.168.10.157
while R6 itself has no IP/GotIP through362seconds. No confirmed DHCP/router
cause follows from the app entry. A local board-only packet capture needs
interactive sudo unavailable to the assistant; normal Wi-Fi visibility is also
limited. The user instructs continuing the home fix.

**R7 diagnostic step:** add read-only `:wifi diag` using the pinned station and
lwIP APIs, before changing connection policy again. Snapshot actual station MAC,
bounded SSID/hostname, association-event BSSID/channel, RSSI, station interface
matched by MAC, flags/IP and DHCP client state/tries/PCB/timer/xid/server/offered
IP. Collect before output can yield. Handle missing/unknown state and sanitize
terminal text. Do not change readiness, reconnect, configure DHCP or write
settings from this command; R6 policies/record format stay intact. Exclude
passwords/browser keys. Counters describe client state, not wire captures.
Preserve the verified R6 binary/ELF/config/source for recovery. Native/target/UI
checks cannot establish the actual R7 DHCP state or a fixed home connection.

**R7 hardware and R8 targeted recovery:** user supplies actual station MAC,
home AP BSSID 02:00:00:00:00:01/channel11/RSSI-52dBm, station/link up but lwIP
DHCP OFF/tries0/PCB0 and no address/offer/server after the prior SDK restart.
The core's zero-address config path returns true while ignoring SDK start
return; SDK enabled status alone did not prove client operation. This snapshot
does not establish the original cause before the earlier stop/config action.

R8 replaces that recovery with a direct real lwIP2 start on the matched station
only when SDK DHCP is enabled, no SDK/interface IPv4 address exists, interface
and link are already up, and client is absent or OFF with PCB0. Wait15seconds
after association and inspect at most once per second; consume at most one
attempt per join/boot, including errors. Leave active clients/PCBs alone. No
forced link-up, guessed/static IP or readiness/save relaxation. Report real
err_t/before/after/tries; the pinned header maps dhcp_start to dhcp_start_LWIP2,
whose target relocation is verified. Normal glue handles bound-IP propagation.
Read-only diagnostics, saved record/key and engine remain compatible. Sources
and procedure: `esp8266/docs/DHCP_RECOVERY_L01.md`. R8 hardware result pending.

**R8 evidence/R9 network variant:** user now reports actual SELECTING/tries6/
PCB1, up/link1/RSSI-49dBm and no accepted offer/IP through92seconds. R8 correctly
does not stop that active client. The current failure is not OFF DHCP. The
custom build selected `lwip2-536`/LWIP_FEATURES0, the core's no-features variant.
Pinned lwipopts guards DHCP client-ID/DISCOVER-hostname hooks behind FEATURES;
upstream8223/8247 document related router compatibility, not an Altibox diagnosis.

For L01 only, R9 selects the bundled standard v2 Lower Memory IPv4 variant,
`lwip2-536-feat`, FEATURES1/MSS536/IPV6=0, rebuilding all headers/core/app with
matching flags. Keep the pinned core/SDK versions, saved record/key, R8 guards,
engine and browser protocol. Reject169.254/16 AutoIP as LAN readiness/save;
do not let the feature variant's fallback silently count as home success.
L00/probe remain feature0 and rebuilt byte-identical. Check target link map/
compatibility hook, resource fit, all affected regressions and actual home
behaviour. Native/source evidence does not prove wire delivery or a fixed home
connection. Source/test procedure: `esp8266/docs/DHCP_COMPATIBILITY_L01.md`.


**R9 evidence/R10 packet observation (2026-11-16):** home remains SELECTING,
tries5/PCB1 at47seconds, no offered/server/interface IP. Free heap18736,
largest18216, stack minimum-free11868/16384 give no obvious exhaustion.
The standard DHCP option comparison did not resolve that run. Keep the passing
R6 hotspot baseline and saved profile while the home replacement is pending.

Before further compatibility changes, use the pinned core's existing
`phy_capture` hook for fixed-size per-join DHCP counters and last metadata.
Observe TX after SDK linkoutput (success means driver ERR_OK, not router
receipt), RX before copy into allocated lwIP storage (allocation success is
not DHCP acceptance). Match own BOOTP MAC/interface, bound Ethernet/VLAN/IPv4/
UDP/options parsing and checksum inspection. Ignore unrelated traffic; retain
no bodies/hostname text/credentials/keys. No allocation, output, yield, packet
mutation or network-result change in the hook. Snapshot only for `:wifi diag`.
This cannot see packets discarded earlier by radio/decryption/driver, does not
reassemble fragments or option-overload, and is not an over-the-air capture.

R10 preserves R9 network flags, R8 guarded recovery, records/key, engine and
browser ownership. Packet/truncation/mutation sanitizer tests and real target
hook relocations pass; hardware packets and home fix remain pending. Source,
meaning and board procedure: `esp8266/docs/DHCP_PACKETS_L01.md`. No privileged
laptop capture is required for this observation step.


**R10 evidence/R11 observer rejection details (2026-11-16):** six Discover
frames accepted by SDK driver, classified RX0 but aggregate malformed6; no
home IP at111seconds. RSSI-56dBm, SELECTING/tries6/PCB1, no obvious memory
exhaustion. The previous observer did not retain malformed direction or reason;
RX0 therefore does not establish no reply. Do not assign router/core fault or
change DHCP validation based on that aggregate. The historical R4 success and
later exact-R4 failure remain evidence against blaming persistence alone;
lease/router/radio/client history causes remain unproven.

R11 keeps fixed metadata even when observer parsing fails: separate rejected
TX/RX counts and last direction, boundary success, exact check, lengths, partial
BOOTP/xid/cookie/IP/type and failing option offset/code/declared/remaining.
Label totals parser-rejected; distinguish unchecked checksum after length
failure. No arbitrary option values/raw payloads/hostname contents, packet
mutation, allocation or network policy change. This is an observer verdict,
not proof that lwIP rejected the packet or the router sent invalid data.
All10 reason paths, separate direction/reset/privacy/immutability and actual
formatted output are tested, alongside existing sanitizer/target/UI checks.
R11 physical result and home fix remain open. Guide: `esp8266/docs/DHCP_PACKETS_L01.md`.


**R11 evidence/R12 pre-address broadcast replies (2026-11-16/17):** real RX4
rejections, TX4 Discover accepted, matched xid25184131; last opcode2 reply from
192.168.10.1 with yiaddr192.168.10.157 has only174 Ethernet bytes for declared328
IP/308 UDP bytes. Boundary allocation succeeded. Cookie/type/checksum were
unreached, not measured zeros/invalid checksum. Pinned glue source plus target
ethernet_input disassembly confirm the same174bytes are allocated/copied to
lwIP; this is not just observer filtering. No upstream/router/radio/SDK/chain
cause is proven, but router silence is no longer a supported explanation.

Choose a standard delivery-path workaround comparison: set BROADCAST on st0
Discover/Request only with ciaddr0 (RFC2131 section4.1). Wrap the existing
lwip_hook_dhcp_amend_options at construction before UDP/IP checksums; invoke
the original to retain client-ID, leave caller hostname addition intact.
Guard station name/index; other interfaces/types and renewals with ciaddr stay
unchanged. No receive repair/validation relaxation/static IP, guessed .157
assignment, radio change, vendor archive patch or settings/key/engine change.
The PHY observer remains read-only. This may bypass the affected reply path;
it does not establish why bytes are missing or a confirmed hardware fix.

L01 alone receives the link wrap. Validate only-bit change/bounds/repeated
application/renewal/types, checksummed wire decoding, real target wrapper call
before UDP transmission and original hook retention. Test the shared build
change across L00/probe and verify baseline binaries identical. Preserve exact
R11 recovery build/source. Board procedure/limits: `esp8266/docs/DHCP_BROADCAST_L01.md`.
Reference: https://www.rfc-editor.org/rfc/rfc2131.html#section-4.1.


**R12 home hardware result (2026-11-17):** user reports "it is working" and
browser access without a serial USB connection to this computer. Mark home
join/browser access PASS and retain the pre-address broadcast flag as the
working workaround. Preserve R12 bin/ELF/map/config/source/build script at
`build/esp8266/history/l01-r12-home-browser-pass/`. Do not revert it for cleanup
or infer a proven router/radio/SDK truncation cause from success. No firmware
source changes are needed to record this result. Exact report:
`apps/esp8266/l01_webrepl/hw/d1_mini_2026-11-17_r12_home_browser_pass.txt`.
No separate R12 packet trace, calculation, cold-boot/key/mDNS, sustained-use
memory or electrical pin result is supplied; full L01 remains open.


**R12 follow-up (2026-11-17):** user reports a command-line reboot followed by
successful reconnection. Record one software restart/browser reconnect PASS on
the home network. Retain the R12 workaround unchanged. This does not establish
cold power interruption, repeated cycles, unchanged key or memory stability.
The appended hardware report records the exact user statement; full L01 stays
open. Documentation/release checksum update only; no firmware change/reflash.


**R12 cold startup follow-up (2026-11-17):** after the explicit instruction to
remove power for a few seconds, power from a USB charger and reconnect in the
browser, user says "also worked". Mark one cold startup/browser reconnect PASS
on home Wi-Fi, alongside the prior warm reboot PASS. Keep the R12 workaround
and image unchanged; independent startup/browser use is now hardware validated.
No repeated-cycle/memory/math/electrical checks are inferred; full L01 stays
open. Appended exact-context hardware report and release documentation updated.


## XD114 — ESP8266 browser program editor and computer-side scripts (2026-11-17)

User accepts the recommended editor with Open/Save and Run/Stop. Extend the
existing L01 app (r13) while keeping the user-validated R12 DHCP/settings/key
behavior and exact recovery image. Older full L01 hardware validation stays
partial; this new step is explicitly authorized. No board filesystem/native
binary loader, GPIO expression functions, peripheral drivers or AI in this step.

Use a plain browser textarea, computer file upload/download, browser-held source
and a single-line-at-a-time queue acknowledged by the board. Provisional browser
limits32KiB/1024physical lines,191printable ASCII characters per transmitted
line, existing engine limits unchanged. Normalize line endings and tab indentation,
prevalidate the whole script before running, reject platform-control commands
in programs (console still exposes them), and report source line on failure.

Extend R readiness with one explicit result byte, retaining the original prompt
bytes for old console clients. New editor requires result capability; it must
stop on actual engine errors rather than parse output text, and abort an
incomplete block at EOF. Stop/disconnect cancels queued lines; reconnect does
not replay. Completed assignments stay in shared RAM with no transactional
rollback. Protect unsaved editor text against replacement and retain it through
connection loss. Native protocol and real Chrome tests plus exact target/image
checks precede a numbered user hardware procedure. No physical assistant flash.


**XD114 implementation ready (2026-11-17):** L01-r13 keeps the R12 app/network
baseline and adds plain browser editor, New/Open/Save (.ms/.m/.txt), initial
sum example, Ctrl+Enter and shared Stop/Esc. Open/Save use the computer/phone;
no board file service. Normalize BOM/CRLF/CR and tabs4spaces; whole-source
32KiB/1024physical-line/191ASCII-line validation before sending. Source queue
is browser-only; one line per actual board result. Platform-control lines and
standalone abort/quit/exit stay in console. Protect unsaved edits and preserve
source on drop; no automatic resume. Readiness extends compatibly to R00/R10
success, R01/R11 engine/input error, R02 cancellation. Older console clients
can ignore byte3; new editor disables batch Run on legacy readiness.

Actual engine failure halts the tail, printed error-text does not. Incomplete
EOF blocks are aborted. Status identifies the line being processed, which may
be a closing end for a block-body error. Stop waits for actual cancellation,
including pending-ack races; completed assignments are not rolled back. Extra
state16bytes static BSS; no larger board queue/IRAM/engine cap. R13 image465120,
SHA256327364b230c79118537c471d95769d363769834c882ae0c6005bab61688dcf24.
80 native/target checks pass; Chrome covers script loops/functions, actual
errors, source validation, block cleanup, Stop, explicit/transport disconnect,
file roundtrip, unsaved edits, legacy guard and prior UI/auth/reboot flows.
R13 physical tests remain pending. Guide: `esp8266/docs/BROWSER_EDITOR_L01.md`;
R12 recovery preserved and its hardware PASS not transferred to R13.


**XD114 first hardware result (2026-11-17):** user supplies R13 editor example
output s=36 and connected :mem: heap15256/largest14376/fragmentation6%, stack
minimum-free12288/16384, engine4904/5292/16384. Record editor multiline loop
execution + usable post-run console/diagnostics PASS. No obvious exhaustion in
this workload; no repeated-use/leak/peripheral conclusion from one snapshot.
File roundtrip/Stop/error/disconnect/R13 startup remain individually unreported.
Exact report: `apps/esp8266/l01_webrepl/hw/d1_mini_2026-11-17_r13_editor_sum_memory.txt`.
Preserve exact firmware/source as l01-r13-editor-sum-pass, keep R12 recovery,
update docs only. Full editor/L01 validation remains open.


**XD114 hardware clarification (2026-11-17):** user says the supplied s=36/:mem
run came from a saved file they opened. Record Save/Open/Run round-trip PASS
for that same run; do not ask for a repeat. This is not a fresh memory sample
or proof of New/unsaved-confirmation/Stop/error/stress checks. Append the exact
clarification to the hardware report; no firmware change or reflash.


**XD114 runtime-error hardware PASS (2026-11-17):** supplied editor source
assigns a=1, triggers undefined missing_editor_name, then would assign a=99.
User output stops on that error; subsequent console a reports1. This confirms
actual error-result queue halt, retained completed assignments and console
recovery. Append exact log to R13 hardware report; no new memory or Stop/
disconnect result inferred. Documentation/release update only, firmware unchanged.


**XD114 unfinished-block Stop hardware PASS (2026-11-17):** user reports
"success" after the explicit for k=1:8/no end/Stop/ordinary prompt/2+2=4
procedure. Record block-entry cancellation and post-Stop arithmetic PASS.
Do not infer cancellation during active execution. Append report/context;
firmware unchanged, full editor/L01 validation remains open.


## XD115 — ESP8266 scriptable GPIO and cooperative delay (2026-11-17)

After R13 active Stop hardware PASS, user explicitly chooses scriptable GPIO
as the next stage. Build L01-r14 with pinmode, digitalwrite, digitalread,
analogread and millisecond delay inside expressions/loops/functions. Start
hardware validation with onboard D4 active-low LED; external loopback/ADC
checks separately opt-in with known wiring/voltage. Existing label-only D0..D8
policy, no UART/flash pins, D0 no pullup, explicit output-before-write remain.

Console and builtin functions share one pin registry; :reset/Stop do not undo
pin state. Setters/delay return numeric0; reads return numeric levels/raw ADC.
Validate types/arity/finite integer ranges and allocate return scalar before
hardware mutation. Delay0..60000ms polls Wi-Fi/Stop in <=5ms slices. Cancellation
becomes normal interrupted engine error/result2; completed writes stay latched.

Generate the extra builtin definitions/table entries only for L01 using counted
profile anchors; do not edit shared engine/DOS/Linux behavior. Keep R12 network
fix/key/layout and R13 editor, provisional matrices16vars/64elements/16KiB engine
cap. Rebuild/test L00/probe after shared profile tool change, require identical
baseline binaries. Native GPIO is a stub; physical D4/loopback/ADC/watchdog
results require user report. Preserve exact R13 recovery first. No new display,
keyboard, PWM, pin interrupts, filesystem/native programs or AI in this scope.


**XD115 implementation ready (2026-11-17):** L01-r14 generates five builtins
(pinmode2..3args,digitalwrite2,digitalread1,analogread0..1,delay1). They use
Mat character labels/modes and finite numeric scalar integer levels/times;
return numeric0 for setters/delay, numeric reads. Shared pins.c registry keeps
console and program configuration coherent; output-before-write and D0/pin
restrictions match prior policy. Return matrices are reserved before I/O; errors
free them. Delay polls the existing cancellation/network path, yields SDK in
<=5ms slices and leaves completed output states latched on cancellation.

Counted generation inserts gpio_engine.h/table entries only for L01. Shared
M01 engine untouched; original hash remains
`e55cec6cf500efefc10933ab20d051102b1bc7c62582f8b60265be7441da960c`.
L00/probe rebuilt byte-identical and19/6 tests PASS. 88 L01 tests PASS, including
shared state, loops/functions, return values, invalid types/levels/ranges/arity,
delay elapsed/cancel/drop/serial recovery, repeated temporary-value cleanup and
target poll/SDK weak-alias wiring. Chrome editor runs GPIO/blink/delay-Stop,
checks retained output/tail halt and prior editor/auth/reboot flows. Native ADC
321 and pins are stubs, not physical results. R14 image467376bytes SHA256
`6da48711542e9ca220b8f220836fbfda3759faa6f1f81ff399481ae7e3545e9b`,
static occupancy56472 (+208 vs R13), IRAM28212 unchanged. Stage SCRIPT_GPIO.md
and blink.ms. Hardware results remain pending; preserve exact R13/R12 recovery.
Guide: `esp8266/docs/SCRIPT_GPIO_L01.md`.


**XD115 first hardware result (2026-11-17):** user reports four flashes and
final read1 for the requested five-blink script. Record physical LED activity/
readback/usable post-run console, but leave count OPEN. Memory free15128,
largest14312/frag6%, stack minimum-free10808/16384, engine4928/5379/16384;
no obvious exhaustion in this single different workload. Do not assume missed
first pulse or software off-by-one. Add a native-only optional write trace and
test exact GPIO2 levels [1]+[0,1]*5 and k=5; this tests issued writes only.
Retest unchanged board firmware with startup-off delay1s, printed counts1..5,
500ms phases (blink_count.ms). Full GPIO/memory/stability remains open.
Report: `apps/esp8266/l01_webrepl/hw/d1_mini_2026-11-17_r14_blink_memory.txt`.


**XD115 numbered blink hardware PASS (2026-11-17):** user says "i counted 5,
wrote until 5, and ended off" after the controlled startup-delay/500ms-phase
script. Mark visible five-blink count/printed counts/final OFF PASS. Keep R14
unchanged and preserve exact bin/source/profile at
`build/esp8266/history/l01-r14-numbered-blink-pass/`. Earlier four-flash cause
remains unassigned; do not call it a proven firmware error or missed pulse.
No new memory/readback/cancellation/ADC/input/stress result is inferred.

## XD116 — Dedicated MathSmith OS source repository and project website (2026-11-17)

The user requests a commit and GitHub sharing for the two working versions,
then chooses **repository plus GitHub Pages website**, **MIT if compatible**,
and a **dedicated `mathsmith-os` repository** rather than the whole DOS
workstation repository. Earlier no-commit preferences are superseded.

Commit ESP8266 L01 locally as `00d85a3`; ESP32-S3 E03 was already in `12d2994`.
Original project code/documentation use MIT. Downloaded SDKs, libraries and
tools retain their licenses; the ESP8266 core is LGPL, so the initial public
release is source-only. Binary redistribution needs corresponding source,
notices and rebuild/relink materials rather than an MIT-only firmware label.

`sharing/export.py` reads an explicit selection from a committed revision,
retains the source layout/shared engine and executable bits, omits DOS apps,
hardware logs and generated dependencies, substitutes example network IDs in
Markdown, and writes provenance/checksums. No parent Git history is copied.
The new repository gets a curated README/progress/target index. The static
`website/` is built without external packages and uses a manually triggered
GitHub Actions Pages workflow. Public documentation separates ESP8266's
reported hardware workflows from ESP32-S3 QEMU results, and keeps outstanding
hardware checks open. No RP2350 implementation or AI integration is claimed.

Local preparation does not create a remote or publish it. Commands and release
notes are in `docs/SHARING_ON_GITHUB.md` and `sharing/RELEASING.md`.
