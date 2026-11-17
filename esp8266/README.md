# ESP8266 MatmMax Light

**Current software: L01-r15. Hardware baseline: L01-r14 on a Wemos D1 mini, ESP8266EX / 4 MB flash.**
Core browser, math, saved Wi-Fi, autonomous boot, editor and LED workflows
are user-confirmed on the real board. Remaining peripheral/stress checks
are recorded in [PROGRESS.md](../PROGRESS.md).

- [Build and first use](../docs/GETTING_STARTED.md).
- [L01 browser/GPIO manual](docs/USER_MANUAL_L01.md).
- [Math language and L00 manual](docs/USER_MANUAL.md).
- [Scriptable GPIO guide and hardware procedure](docs/SCRIPT_GPIO_L01.md).
- [L01 firmware and full numbered procedure](../apps/esp8266/l01_webrepl/README.md).
- [L00 serial-only baseline](../apps/esp8266/l00_mathsmith/README.md).
- [Memory map](docs/MEMORY_MAP_L01.md), [settings](docs/SETTINGS_L01.md),
  [browser protocol](docs/WEB_PROTOCOL_L01.md).

The chip is much smaller than the ESP32-S3 target. Limits are 16 variables,
64 elements per matrix, 16 KiB engine payload and a dedicated 16 KiB stack.
Wi-Fi and a single browser owner share the mathematical workspace with serial.
Editor files are stored on the computer. There is no board filesystem, FFT,
graphics, native app launcher or AI integration in this build.

The home-network DHCP fix requests broadcast replies before the first IP;
it preserves receive validation. Earlier diagnostic reports are retained in
the original workstation repository, while this source snapshot includes the
technical notes and reviewed validation summary without personal hardware logs.
