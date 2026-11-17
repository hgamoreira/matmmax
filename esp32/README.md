# ESP32-S3 workstation

**Current: E03, tested in Espressif QEMU; physical hardware validation pending.**
The intended board is an ESP32-S3 N16R8 CP2102 development board (16 MB flash,
8 MB octal PSRAM). Delivery, actual memory and board revision still need the
E00 hardware checks. No physical board has been flashed by this project yet.

Start with [build and first use](../docs/GETTING_STARTED.md). The suite has
82 project checks across the four firmware stages, plus 35 upstream loader
checks. Automated PASS is not a physical-board result.

| Stage | Capability | Procedure |
|---|---|---|
| E00 | UART0 115200 console, PSRAM/memory and allocation checks | [README](../apps/esp32/e00_console/README.md) |
| E01 | Shared MathSmith engine, double arithmetic, 4096 matrix elements, FFT | [README](../apps/esp32/e01_mathsmith/README.md) |
| E02 | Built-in foreground app launcher; preserve workspace on return | [README](../apps/esp32/e02_launcher/README.md) |
| E03 | External ELF apps and bounded serial upload into RAM | [README](../apps/esp32/e03_external/README.md) |

E03 includes the preceding features. Uploaded code is trusted native code in
one address space, not an isolated process. There is one uploaded slot;
reboot loses it. SD storage, attached keyboard/display and AI are future work.
The reusable launcher lives in `common/workstation/`; the future RP2350 port
can supply its own backend. See the [app ABI](../docs/WORKSTATION_APP_ABI.md),
[serial protocol](docs/SERIAL_UPLOAD.md), [memory map](docs/MEMORY_MAP_E03.md)
and [current status](../PROGRESS.md).
