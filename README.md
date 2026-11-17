# MathSmith OS

A small mathematical programming environment for microcontrollers. Boot into
MathSmith, calculate with matrices and scripts, and build toward a standalone
workstation. This is early firmware with two working targets and different
capabilities; it is not a general-purpose multitasking OS.

![MathSmith WebREPL showing the script editor and console solving a linear system, finding eigenvalues and checking the result](esp8266/docs/images/webrepl.png)

*The browser editor and console running a matrix script: solve a system, find
its eigenvalues, and check the result. Captured with the native firmware test
harness, using the same WebREPL interface served by the ESP8266.*

| Target | Working version | Validation |
|---|---|---|
| **ESP8266 Wemos D1 mini, 4 MB** | **L01-r14**: serial and browser console, saved Wi-Fi, browser script editor, scriptable GPIO | Real-board boot, math, home Wi-Fi, autonomous reconnect, editor Run/Stop and onboard LED confirmed by the user. The full peripheral/stress procedure remains open. |
| **ESP32-S3 N16R8** | **E03**: serial MathSmith, FFT, app launcher, separate ELF apps uploaded over serial | Tested in Espressif QEMU. Physical board validation pending. |
| RP2350 with PSRAM | Architecture draft | No working RP2350 firmware yet. |

Both working targets use **64-bit doubles** and the same portable MathSmith
engine. ESP8266 limits matrices to 64 elements (8×8 square), with 16 variables
and a 16 KiB engine payload budget. ESP32-S3 allows 4096 elements (64×64
square) and uses PSRAM. Limits are allocations, not a promise that every
combination fits in memory.

```text
>> A=[4 -2;-2 4]
>> A\[2;8]
ans =
   2
   3
```

## Try without hardware

Choose either route below. If you already cloned the repository, use its
existing folder and skip the clone/cd lines.

### ESP8266: try the WebREPL in your browser

On **x86_64 Linux**, you need Python **3.12+**, Git and GCC/G++. On
Ubuntu/Debian, `sudo apt-get install git python3 build-essential` supplies the
packages; check that your distribution's Python meets the version requirement.
No ESP8266 board, USB adapter, ESP-IDF, QEMU or cross-compiler is needed.

```sh
git clone https://github.com/hgamoreira/mathsmith-os.git
cd mathsmith-os
MS_NATIVE_WIFI=1 esp8266/scripts/webrepl.sh native
```

Keep the terminal open. Copy the complete **Browser URL** it prints into a
browser on the same computer, then click **Connect**. Try `2+2` in the console,
use the program editor, and open/save scripts on your computer. Stop the
harness with **Ctrl+C** in the terminal when finished.

This runs the real ESP8266 WebREPL interface and math engine as a Linux
program. GPIO/ADC and network setup are simulated; it does not emulate the
ESP8266 CPU or physical Wi-Fi. See the
[WebREPL walkthrough](docs/EMULATOR_QUICKSTART.md#6-try-the-webrepl-screenshots-interface-without-an-esp8266)
for the matrix example shown above.

### ESP32-S3: run the firmware in QEMU

Run the ESP32-S3 firmware in Espressif QEMU on **x86_64 Linux**. After
installing the [Linux prerequisites](docs/EMULATOR_QUICKSTART.md#1-install-linux-prerequisites):

```sh
git clone https://github.com/hgamoreira/mathsmith-os.git
cd mathsmith-os
tools/setup_esp_idf.sh
tools/setup_esp_qemu.sh
tools/setup_elf_loader.sh
esp32/scripts/external.sh build
python3 esp32/scripts/upload_external.py --qemu build/esp32/e03_external/demo_b.msp
```

At `>>`, try `2+2`, then `x=41`, `:app uploaded`, `exit`, and `x+1` (42).
Press **Ctrl+]** to close the emulator console. Enter each line at its current
prompt. The [full emulator guide](docs/EMULATOR_QUICKSTART.md) covers tool
installation, expected results, tests, troubleshooting, and running the
**WebREPL on your computer** with the native ESP8266 test harness.

## Start here

- [Build and first use](docs/GETTING_STARTED.md): ESP8266 on a board or ESP32-S3 in QEMU.
- [ESP8266 browser/GPIO manual](esp8266/docs/USER_MANUAL_L01.md) and [math language manual](esp8266/docs/USER_MANUAL.md).
- [ESP32-S3 external apps](apps/esp32/e03_external/README.md) and [app ABI](docs/WORKSTATION_APP_ABI.md).
- [Validation status](PROGRESS.md), including the remaining hardware checks.
- [Project website](https://hgamoreira.github.io/mathsmith-os/) (available after GitHub Pages is enabled).

## What is available now

The ESP8266 serves its own browser console on your local network. Configure
Wi-Fi once over serial; after a successful connection, credentials and the
browser key are saved and subsequent boots reconnect. The browser editor
opens and saves `.ms` files **on your computer**, then sends acknowledged
lines to the board. Stop cancels running code. The browser console exposes
the same workspace as serial.

GPIO can be used inside a script. This blinks the D1 mini's active-low LED:

```text
pinmode('D4','output',1);
for k=1:5
    digitalwrite('D4',0); delay(500);
    digitalwrite('D4',1); delay(500);
end
```

The ESP32-S3 has a foreground app launcher and a versioned API. A separately
compiled demo can be uploaded and run without reflashing the OS; returning
preserves MathSmith variables. Uploaded apps are trusted native code in the
same address space. Uploads are RAM-only and disappear after reboot.

## Current limits

Neither microcontroller build includes AI integration, an attached keyboard,
OLED driver or SD storage. Those remain future milestones. ESP8266 has no
FFT, graphics or native app launcher. Its HTTP/WebSocket browser console is
intended for a trusted local network. The project website hosts documentation;
open the board's own URL to use MathSmith.

## Development and sharing

Builds are currently supported on **x86_64 Linux**. SDK/compiler versions are
pinned by installers under `tools/`; downloaded dependencies, personal flash
backups and generated firmware stay outside Git. See
[CONTRIBUTING.md](CONTRIBUTING.md) for checks and
[RELEASING.md](docs/RELEASING.md) for GitHub Pages and source releases.

This repository is a curated source snapshot from the larger workstation
project. `EXPORT_MANIFEST.json` records the initial export's provenance and file
checksums; later changes are tracked in Git.
Original project source is [MIT licensed](LICENSE); downloaded SDKs and tools
retain their [own licenses](THIRD_PARTY_NOTICES.md). The initial public
release is source-only.
