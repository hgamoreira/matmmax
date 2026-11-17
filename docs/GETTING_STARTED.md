# Build and first use

Run these commands from the dedicated `matmmax` repository root.
The same paths work from the workstation repository. Current installers are
tested on x86_64 Linux. You need Python 3.12 or newer, GCC/G++, Git, make,
curl, tar and xz; ESP-IDF also needs the prerequisites in its
[Linux setup documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/linux-macos-setup.html).
Installers need internet access and place dependencies under `third_party/`.

**No hardware?** Follow the [ESP8266 browser walkthrough](EMULATOR_QUICKSTART.md#6-try-the-webrepl-screenshots-interface-without-an-esp8266)
for a native Linux build: `MS_NATIVE_WIFI=1 esp8266/scripts/webrepl.sh native`.
In this PC demo, the WebREPL server and calculations run entirely on your
Linux computer. The browser connects to localhost; no ESP8266 is connected.
No SDK or cross-compiler is required for that route.

## ESP8266: a real Wemos D1 mini, 4 MB

**The instructions in this section install firmware on a physical board.**
MatmMax then runs on the ESP8266; your browser connects to its Wi-Fi address.

```sh
python3 tools/setup_esp8266.py
esp8266/scripts/webrepl.sh build
esp8266/scripts/webrepl.sh test
```

The image is `build/esp8266/l01_webrepl/mathsmith_light.bin`. Review
`apps/esp8266/l01_webrepl/README.md` before using a physical board.
For a standalone flashing environment, install esptool and pyserial in a venv:

```sh
python3 -m venv .venv-flash
.venv-flash/bin/python -m pip install 'esptool>=4,<5' pyserial
.venv-flash/bin/python -m esptool --chip esp8266 --port /dev/ttyUSB0 flash_id
```

Check the detected chip and **4 MB** flash size. Back up existing firmware
before replacing it; keep this backup private:

```sh
mkdir -p build/private-backups
.venv-flash/bin/python -m esptool --chip esp8266 --port /dev/ttyUSB0 read_flash 0x0 0x400000 build/private-backups/original.bin
.venv-flash/bin/python -m esptool --chip esp8266 --port /dev/ttyUSB0 --baud 115200 write_flash --flash_size 4MB --flash_mode dio --flash_freq 40m 0x0 build/esp8266/l01_webrepl/mathsmith_light.bin
.venv-flash/bin/python -m serial.tools.miniterm --raw --dtr 0 --rts 0 /dev/ttyUSB0 115200
```

Use the actual port on your machine. Close the monitor before flashing.
The normal ROM boot message uses 74880 baud, so a brief garbled burst before
the readable 115200-baud MatmMax banner is expected. Repeating resets or
exceptions are not normal. Exit miniterm with Ctrl+].

At the prompt, type `2+2`, then `:wifi join`. Enter your 2.4 GHz network's
SSID and password in the guided prompts. SSID is visible; the password is
masked. After a usable IP is assigned, settings and the browser key are
saved automatically. Use `:wifi` to see the board's URL, then open it from
a browser on the same network. Keep its access key private. `mathsmith.local`
depends on local mDNS support; the numeric IP is the fallback.

After setup, a USB power supply is sufficient: serial USB to the computer is
not required for browser use. The board must remain on the same reachable
network as the browser. Follow the [browser manual](../esp8266/docs/USER_MANUAL_L01.md)
and [GPIO guide](../esp8266/docs/SCRIPT_GPIO_L01.md). The editor's Open/Save
uses files on the computer, not board flash. No board filesystem is implemented.

## ESP32-S3: run E03 in QEMU

Follow the [step-by-step emulator guide](EMULATOR_QUICKSTART.md) for Linux
prerequisites, the pinned SDK/QEMU installation, E03 build, serial console,
external app upload, expected results and emulator checks. No board is needed.

The same guide also explains how to try the **ESP8266 WebREPL on a computer**
using its native Linux test harness. That route exercises the browser editor
and mathematical engine; it does not emulate the ESP8266 CPU or radio.

## Website preview

```sh
python3 website/build.py --repository hgamoreira/matmmax
python3 -m http.server 8000 --bind 127.0.0.1 --directory build/github-pages
```

Open `http://127.0.0.1:8000`. This is a static project website. To calculate
on the ESP8266, open the board's own LAN URL separately.
