# ESP8266 peripheral budget — OLED/PS2 direction, exact display pending

2026-11-15. User wants keyboard/display capacity considered alongside Wi-Fi/AI.
The user prefers **OLED and PS/2** and owns four-pin and seven-pin OLED modules.
User-supplied labels: four-pin **Vcc, Gnd, scl, sda** (I2C); seven-pin
**gnd, VDD, SCK, SDA, RES, DC, LS** (consistent with SPI, with LS possibly a
misread CS). SDA on the SPI module denotes serial data input, not proof of I2C.
The user now identifies both as **SSD1306, 0.96 inch**. Controller is confirmed;
use **128x64 provisionally**, to be checked with a geometry test, since physical
size alone does not prove resolution. Module supply specification remains to
be checked. No peripheral driver or new network milestone is implemented.
The budgets below remain estimates until actual driver/runtime measurement.

Proposed connections and remaining hardware facts:
[SSD1306/PS2 wiring plan](OLED_PS2_WIRING.md).

For the initial text console, prefer an identified four-pin I2C module to save
pins for PS/2 and retain serial diagnostics. SPI can provide faster updates but
uses more GPIO; the existing tentative PS/2 pins overlap hardware SPI and must
be reassigned if SPI is chosen. This is a recommendation, not settled wiring.
[OLED bus/driver identification](https://github.com/olikraus/u8g2/wiki/setup_tutorial)
and [SPI/I2C tradeoff](https://learn.adafruit.com/monochrome-oled-breakouts/wiring-128x64-oleds).

## What is actually available

The reported matrix/eigenvalue workload leaves **27888 bytes free heap**,
largest block **27304**, engine payload **5072** (peak 5664), minimum free
stack **11596/16384**. Wi-Fi is off. The workload does not fill the 16 KiB engine
payload cap; that cap is not all additional spare capacity. Free heap must cover
larger workspaces/temporaries, network allocations, UI state and allocator costs.

Current linked image **382464 bytes**, with **27239/32768 bytes IRAM code**
(5529 bytes remaining) and a separate 32 KiB instruction-cache reservation. Fonts
and most display logic can be in flash, but keyboard ISR/video/DMA code can
consume IRAM. A driver cannot be judged only by its framebuffer or flash size.
See [memory map](MEMORY_MAP_L00.md) for ownership and the measured workload.

## OLED is the first recommendation

For a 128x64 monochrome OLED, the pure full-frame bitmap is
`128*64/8 = 1024 bytes`. A one 8-pixel-page buffer is 128 bytes; direct text modes
can avoid a full framebuffer. Library objects, I2C buffers, history and input
queues are additional. [U8g2 modes](https://github.com/olikraus/u8g2/wiki/setup_tutorial)
and [SSD1306 constructor/buffer descriptions](https://github.com/olikraus/u8g2/wiki/u8g2setupcpp)
document full/page/text alternatives. No library is pinned/adopted yet.

Reserve **2–4 KiB for a compact UI** as a planning envelope, not a measurement:
small bitmap/page buffer, line/history state and keyboard queue. Choose bounded
scrollback; keep fonts in flash. Measure actual flash/IRAM/heap/stack after the
driver build, then again with Wi-Fi active, rather than promising a fixed total.

Display usability matters too: 128x64 with 6x8 cells fits 21 columns by 8 rows;
8x8 cells fit 16x8. Matrix output and AI explanations need wrapping/paging or
scrolling. A 256x64 monochrome display has a 2048-byte bitmap and room for 42x8
6x8 cells, if its physical size/driver suits the user. Resolution/controller
and bus must be known before choosing a driver or wiring it.

## Keyboard and tentative pin reservation

A PS/2 keyboard takes data and clock signals; modest scan-code/input buffers
are plausible, but ISR placement and reliable input while Wi-Fi/calculation
are active need testing. Standard PS/2 signalling must be adapted to the
D1 mini's 3.3 V GPIO where necessary. An I2C keyboard/controller is another
option; matching devices can share the display bus when addresses differ.

The board's USB connector feeds a USB-to-UART bridge; it is not a ready USB
keyboard host. A USB keyboard requires an appropriate host controller/bridge
or a genuine supported PS/2 mode; a passive adapter does not add host support.
PS/2 is the user's preferred keyboard route. They confirm having a socket/breakout
and a bidirectional 3.3 V/5 V level shifter; exact part labels/layout remain open.
Standard PS/2 keyboards
may pull clock/data to 5 V, so provide suitable level translation rather than
connecting those signals directly to ESP8266 GPIO.
[PS2Keyboard hardware requirements](https://www.pjrc.com/teensy/td_libs_PS2Keyboard.html).

Tentative allocation, **not connection instructions**:

| Function | D1 mini label | ESP8266 GPIO |
|---|---|---|
| I2C OLED clock | D1 | 5 |
| I2C OLED data | D2 | 4 |
| PS/2 clock, interrupt input | D5 | 14 |
| PS/2 data | D6 | 12 |
| Serial development console | TX / RX | 1 / 3, retained |

These mappings agree with the pinned core variant and
[Wemos board documentation/schematic](https://www.wemos.cc/en/latest/d1/d1_mini.html).
They leave D7/GPIO13 available; D0/GPIO16 has different interrupt capabilities.
Avoid first-design dependence on boot-strapping D3/GPIO0, D4/GPIO2 and D8/GPIO15.
Pins reserved for PS/2 overlap future HSPI use, so an SD design would need a new
allocation. Board revision, peripheral power, voltage levels and any extra
reset/interrupt pins must be checked before actual wiring.

## RCA/composite is a separate investigation

RCA is a connector; here the assumed signal is PAL/NTSC composite video. There
are actual ESP8266 CVBS+PS/2 examples, e.g.
[matgoebl's ESP8266 NTSC C64 project](https://github.com/matgoebl/esp8266-ntsc-c64-emulator).
Its documentation states video uses RX/GPIO3 and its attached adapter prevents
serial flashing. Its Wi-Fi/OTA mode is entered separately from emulator mode;
that is not proof of concurrent MathSmith/video/keyboard/AI operation.

Software video entails continuously scheduled scan output, DMA/scan buffers,
interrupt handling and pin restrictions. A text-only renderer can avoid a
large full-screen pixel buffer, so RAM alone is not a reason to declare RCA
impossible. The key unknown is a reliable timing/IRAM/pin budget with the current
core and Wi-Fi/math workload. No compatible driver or PAL mode is established.
Do not reuse classic ESP32 DAC libraries as evidence for ESP8266 or ESP32-S3.
Do not confuse ESP8266 RF-broadcast experiments with an RCA CVBS connection.

If RCA is essential, consider a separate video/terminal controller that accepts
text from the ESP8266, or scope a video proof separately after choosing the
display standard. That moves scan generation to another device but adds hardware
and bus/power requirements. It is not part of the current agreed L00 firmware.

Recommendation: reserve compact UI resources now; plan OLED+PS/2 (or an I2C
keyboard controller) first, then measure the combined MathSmith/UI/Wi-Fi build
and keyboard latency. Keep serial diagnosis available. Agree actual peripherals
and the next implementation milestone before adding drivers or purchases.
