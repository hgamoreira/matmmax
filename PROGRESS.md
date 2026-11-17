# MatmMax status — 2026-11-17

The current sources use MatmMax in the microcontroller consoles and WebREPL.
The ESP8266 branding build is L01-r15. ESP32-S3 E03 was rebuilt and passed
82 emulator and host checks. ESP8266 L01, L00, the Wi-Fi probe and the
native Chrome matrix example passed their automated checks. The board
results below refer to L01-r14 or earlier firmware.

Read this before changing the firmware. The first source release contains
ESP32-S3 E03 and ESP8266 L01-r14. The parent source commit and exported-file
hashes are recorded in `EXPORT_MANIFEST.json`.

Fresh source-checkpoint checks: **89 ESP8266 L01 + 19 L00 + 6 probe checks**,
the real Chrome native browser/editor/GPIO checks, and **82 ESP32-S3 project +
35 upstream loader checks**, all PASS. Site/export policy checks and desktop/
phone browser layout checks also pass. These are host/emulator results.

The curated export was also verified independently: both ESP8266 L01 and
ESP32-S3 E03 built from its own folder using the pinned dependencies; all
89 L01 checks and 29 E03 checks passed there. The ESP8266 rebuilt image
matched the hardware-tested reference SHA-256 exactly. Only website word
spacing and status documentation changed after this build verification.

## ESP8266 Wemos D1 mini / 4 MB flash

**User-confirmed on hardware:** boot and serial math; 4×4 eigensolver; browser
arithmetic and linear solve; saved Wi-Fi/browser key; home-network browser
access without serial USB to the computer; reconnect after command reboot
and after cold USB-charger power; browser editor file Save/Open/Run (sum 36);
runtime-error tail halt; Stop during unfinished and actively running code;
scripted onboard LED output, five counted flashes and final off state.

The home DHCP issue was resolved in R12 by requesting broadcast replies
before acquiring an IP. Earlier short incoming replies were observed, but
their underlying origin has not been established. Keep receive validation.
R13 added the editor; R14 added typed GPIO functions and cooperative delay.

**Still open:** the separately requested hardware `delay(60000)` Stop check,
ADC, external pin loopback, full GPIO procedure and sustained/stress use.
Do not mark the entire L01 procedure closed. See the numbered procedures in
`apps/esp8266/l01_webrepl/README.md` and `esp8266/docs/SCRIPT_GPIO_L01.md`.

Validated firmware reference: 467,376 bytes; SHA-256
`6da48711542e9ca220b8f220836fbfda3759faa6f1f81ff399481ae7e3545e9b`.
The reference is from the original workstation build path. Rebuilding at a
different absolute path may change debug/build metadata and image hashes.
The source export contains no binary or saved device settings.

## ESP32-S3 N16R8

E00 console, E01 math, E02 launcher and E03 ELF/serial upload are tested in
Espressif QEMU. The recorded baseline is 82 project checks plus 35 upstream
loader checks. Variables survive app return; the uploaded slot is RAM-only.
**Physical board tests have not been reported.** Start with E00's numbered
hardware procedure when the board arrives.

## Next work

Publish the source snapshot and static project website, then complete the
remaining ESP8266 hardware checks and ESP32-S3 arrival checks. Keyboard,
screen, SD storage and AI are future scope choices. RP2350 remains a draft.
Read `docs/DECISIONS.md` for the portable platform decisions and
`docs/RELEASING.md` for sharing instructions.
