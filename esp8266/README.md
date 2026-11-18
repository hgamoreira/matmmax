# ESP8266 MatmMax Light

**Current software: L02-r2 on a Wemos D1 mini, ESP8266EX / 4 MB flash.**
L02 adds programs saved on the board's own flash, and the board answers as
`matmmax.local`. L02-r2 runs on the user's board: boot, saved Wi-Fi, browser
connection and the user's own program-store tests are reported working. The
numbered store procedure has no step-by-step report yet. Core browser, math,
editor and LED workflows were confirmed earlier on L01-r14. Remaining checks
are recorded in [PROGRESS.md](../PROGRESS.md).

- [Build and first use](../docs/GETTING_STARTED.md).
- [L01 browser/GPIO manual](docs/USER_MANUAL_L01.md).
- [Programs saved on the board (L02)](docs/PROGRAM_STORE_L02.md): `ls`, `run NAME`, `type`, `rm`, `:store`.
- [Math language and L00 manual](docs/USER_MANUAL.md).
- [Scriptable GPIO guide and hardware procedure](docs/SCRIPT_GPIO_L01.md).
- [L01 firmware and full numbered procedure](../apps/esp8266/l01_webrepl/README.md).
- [L00 serial-only baseline](../apps/esp8266/l00_mathsmith/README.md).
- [Memory map](docs/MEMORY_MAP_L01.md), [settings](docs/SETTINGS_L01.md),
  [browser protocol](docs/WEB_PROTOCOL_L01.md).

The chip is much smaller than the ESP32-S3 target. Limits are 16 variables,
64 elements per matrix, 16 KiB engine payload and a dedicated 16 KiB stack.
Wi-Fi and a single browser owner share the mathematical workspace with serial.
Programs can be saved on the board (1000 KiB of flash, LittleFS) or on the
computer. A saved program runs by name, the same as `.m` scripts on the DOS
and Linux versions. Nothing runs by itself at power-up. There is no FFT,
graphics, native app launcher or AI integration in this build.
`apps/esp8266/l01_webrepl/examples/ledtest.m` is a small program to store
and run: it blinks the D4 LED five times.

The home-network DHCP fix requests broadcast replies before the first IP;
it preserves receive validation. Earlier diagnostic reports are retained in
the original workstation repository, while this source snapshot includes the
technical notes and reviewed validation summary without personal hardware logs.
