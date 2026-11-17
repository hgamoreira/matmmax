# RP2350 workstation draft

**No working RP2350 firmware or hardware validation exists yet.**
The intended first board is a SpotPear RP2350B-MINI-A / RP2350B Tiny variant
with 8 MB PSRAM. Its delivered revision and actual memory remain to be checked.
The Pico Plus 2 W is a possible later wireless target.

The intended experience is MathSmith as the main prompt, with foreground
applications that return to the preserved mathematical workspace. The
[portable C launcher](../common/workstation/README.md) and
[app API](../docs/WORKSTATION_APP_ABI.md) already have an ESP32-S3 backend;
that does not constitute an RP2350 implementation.

Future work needs a Pico SDK toolchain, memory/console diagnostics, a platform
allocation/input/output backend, then the existing engine and launcher tests.
Keyboard, display, storage and networking require separate agreed milestones.
See [decisions XD103–XD105](../docs/DECISIONS.md) and [status](../PROGRESS.md).
