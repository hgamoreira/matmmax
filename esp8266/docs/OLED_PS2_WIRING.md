# D1 mini + SSD1306 OLED + PS/2 — proposed wiring

2026-11-15. The user owns **SSD1306 0.96-inch** four-pin I2C and seven-pin SPI
modules, and prefers PS/2 keyboard input. Start with the four-pin module labelled
**Vcc, Gnd, scl, sda**, in that printed order. **128x64 is the initial geometry
assumption**, not independently verified from the physical size. The first
display test should show all four edges and eight 8-pixel text rows to verify it.
No new firmware is installed; current L00 still uses serial only.

## Four-pin OLED

Disconnect USB/power before changing connections. For an OLED module rated for
3.3 V operation:

| OLED label | D1 mini connection | Meaning |
|---|---|---|
| Vcc | 3V3 | Display supply; check module voltage rating |
| Gnd | G | Common ground |
| scl | D1 / GPIO5 | I2C clock |
| sda | D2 / GPIO4 | I2C data |

These signal pins match the pinned D1 mini variant. OLED pullups must be to
3.3 V on the ESP side; do not power an unknown pullup-equipped module from 5 V
and connect its I2C signals directly. This is an interface-voltage constraint,
not a request to change board power wiring experimentally.

Use a scanner to discover the address before driver selection; SSD1306 addresses
are commonly 0x3c/0x3d, but neither is measured here. Address alone does not
confirm pixel geometry. Keep TX/RX available for flashing and diagnostics.
The seven-pin SPI module is not part of this first pin assignment.

## PS/2 signal reservation

| Keyboard signal | Adapter path | D1 mini |
|---|---|---|
| Clock | Suitable 5 V ↔ 3.3 V bidirectional level translator | D5 / GPIO14 |
| Data | Second translator channel | D6 / GPIO12 |
| Ground | Common keyboard/translator/board ground | G |
| Keyboard supply | Appropriate 5 V supply | Confirm board/USB supply capacity |

For a standard translator board, its low-voltage reference is 3.3 V and
high-voltage reference is the keyboard's 5 V. Actual translator/socket pin names
must be checked against the parts available before giving connection-by-pin
instructions. The clock GPIO supports interrupts; its ISR and data buffer need
testing while calculations/networking run. Do not connect 5 V PS/2 clock/data
directly to the ESP8266. The user confirms they have both a PS/2 socket/breakout
and a bidirectional 3.3 V/5 V level shifter. Actual adapter pin labels/model and
keyboard layout remain to be established before detailed hookup/driver tests.

## Proposed implementation order

1. OLED discovery and geometry test, retaining serial diagnostics.
2. Bounded text console on OLED, with wrapping/paging for numeric output.
3. PS/2 input, editing and cancellation with serial input retained.
4. Record combined heap, largest free block, stack and IRAM use; then scope
   Wi-Fi/AI with UI resources accounted for.

This is a reviewable plan, not a claim of tested wiring or a selected driver
release. Driver/keyboard hardware tests will be numbered for user PASS/FAIL.
The known working serial image remains the fallback.

Sources: [Wemos board/schematic](https://www.wemos.cc/en/latest/d1/d1_mini.html),
[PS2Keyboard hardware requirements](https://www.pjrc.com/teensy/td_libs_PS2Keyboard.html),
[OLED voltage requirements vary by board](https://learn.adafruit.com/monochrome-oled-breakouts/power-requirements).
