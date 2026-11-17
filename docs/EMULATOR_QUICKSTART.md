# Try MatmMax without a board

There are two ways to try the project on a computer:

| Route | What runs | What you can try |
|---|---|---|
| **ESP32-S3 in Espressif QEMU** | The actual ESP32-S3 firmware, with an emulated CPU, flash and PSRAM | Serial math, FFT, app launch/return and external program upload |
| **ESP8266 WebREPL PC demo — no board** | The WebREPL server and math engine run as a Linux program on your computer | Open its localhost URL in a browser on that computer |

The supplied installers support **x86_64 Linux**. Use Python **3.12 or newer**
and a terminal. Native Windows/macOS installers are not provided here.

Neither requires a physical board or USB serial adapter. The native harness
does not emulate the ESP8266 CPU, radio, memory limits or electrical pins.
There is no RP2350 emulator setup in this project yet.

## 1. Install Linux prerequisites

For Ubuntu/Debian, the ESP-IDF build prerequisites plus the host compiler and
archive tools are:

```sh
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 build-essential curl xz-utils
```

For QEMU, also install its shared-library dependencies:

```sh
sudo apt-get install libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0
```

Some distributions use a renamed package for the same library; follow your
package manager's replacement. For other Linux distributions, see Espressif's
[build prerequisites](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/linux-macos-setup.html#step-1-install-prerequisites)
and [QEMU prerequisites](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-guides/tools/qemu.html#prerequisites).
Check that your installed `python3` meets this project's version requirement;
installing the package on an older distribution may not be sufficient.

The WebREPL-only native route below needs Python and GCC/G++ (`build-essential`
on Ubuntu/Debian), but does not need ESP-IDF, QEMU or the cross-compiler.

## 2. Get the source

```sh
git clone https://github.com/hgamoreira/matmmax.git
cd matmmax
python3 --version
uname -m
```

`uname -m` should report `x86_64`. Keep this terminal in the repository root
for the commands below. Use the current `main` branch for these instructions;
the first `v0.1.0` source snapshot predates this guide.

## 3. Install the ESP32-S3 tools and build E03

Run these as your normal user, without `sudo`:

```sh
tools/setup_esp_idf.sh
tools/setup_esp_qemu.sh
tools/setup_elf_loader.sh
esp32/scripts/external.sh build
```

The installers download pinned ESP-IDF **v5.5.6**, Espressif QEMU
**esp_develop_9.2.2_20260417**, ELF loader **2.1.0** and the required tools into
`third_party/`. Allow several GiB for tools, SDKs and build products. Internet
access is required for the first installation. Rerunning setup checks/reuses
the installed dependencies.

You do not need a separate global Arduino or ESP-IDF installation, or to source
an `export.sh`: the project's wrapper selects its own SDK environment.
Use the pinned Espressif QEMU, rather than a generic system QEMU package.

The build creates the merged firmware image and example program packages:

```text
build/esp32/e03_external/flash.bin
build/esp32/e03_external/sample.msp
build/esp32/e03_external/demo_b.msp
```

These are generated locally; the source release does not contain prebuilt
firmware downloads.

## 4. Boot the emulator and open the console

This command starts a fresh QEMU instance, uploads demo B into RAM, and opens
its serial console:

```sh
python3 esp32/scripts/upload_external.py --qemu build/esp32/e03_external/demo_b.msp
```

Expect an `INSTALLED uploaded ...` message and the MathSmith `>>` prompt.
Type commands one line at a time at the current prompt:

```text
2+2
x=41;
A=[4 -2;-2 4];
A\[2;8]
fft([1 2 3 4])
:mem
:apps
:app uploaded
```

The first result is `4`; the linear solve produces the column `[2;3]`.
The app changes the prompt to `demo>`. Type `hello` to see it echoed,
then `exit` to return to MathSmith. Now enter `x+1`: it returns `42`, showing
that the mathematical workspace survived the app's return.

**Press Ctrl+] to close this host console and stop its QEMU instance.**
This is the recommended route if a terminal intercepts QEMU's own shortcuts.
No physical serial port is opened. Each run uses a disposable flash copy;
workspace variables and the uploaded RAM program disappear on restart.

To boot without an upload instead:

```sh
esp32/scripts/external.sh qemu
```

Use `:app sample` for the embedded external-ELF example. This direct QEMU
console uses **Ctrl+A, then X** to exit; it is a sequence, not a simultaneous
three-key chord. MathSmith's `quit` is not an emulator shutdown command.
Inside the demo, `exit` returns to MathSmith.

## 5. Run the emulator checks

After building E03, run its focused suite:

```sh
python3 -m unittest discover -s esp32/tests -p test_external.py -v
```

The recorded baseline is **29 E03 checks PASS**. For the full **82-check**
project suite, build the earlier stages as well, then run:

```sh
esp32/scripts/console.sh build
esp32/scripts/mathsmith.sh build
esp32/scripts/workstation.sh build
esp32/scripts/external.sh test
```

This suite includes earlier firmware tests, so running it with only E03 built
will leave required images missing. These results verify host/emulator behavior;
physical ESP32-S3 board validation remains pending.

## 6. Try the WebREPL screenshot's interface without an ESP8266

**Everything in this section runs on your Linux computer. No ESP8266 board
is connected.** The local Linux program performs the calculations; the browser
only displays its editor and results. This is the PC demo shown in the README
screenshot. On a real board, the ESP8266 performs those calculations instead.
Only Python **3.12+**, Git and GCC/G++ are needed; no ESP8266 SDK, cross-compiler,
QEMU, Wi-Fi setup or USB connection is involved. If you want only this route,
skip sections 3–5 above. On Ubuntu/Debian, install `git python3 build-essential`,
then clone the project as in section 2 and run this from its root:

```sh
MS_NATIVE_WIFI=1 esp8266/scripts/webrepl.sh native
```

The wrapper builds the native harness and immediately starts it with the
browser service enabled. `MS_NATIVE_WIFI=1` selects simulated network setup;
it does not join a real Wi-Fi network.

Keep the process and terminal open. It prints a **Browser URL** similar to
`http://127.0.0.1:PORT/#ACCESS_KEY`, with an actual port and generated key.
Open that complete URL in a browser on the same computer and click **Connect**.
No real Wi-Fi SSID or password is needed. The URL is generated for this run;
do not type the example placeholders literally.

Paste this into the program editor, then click **Run program**:

```text
A=[4 -2;-2 4];
b=[2;8];
x=A\b
eig(A)
A*x
```

The console shows `x=[2;3]`, eigenvalues `2` and `6`, and `A*x=[2;8]`.
**Open file** and **Save file** use files on your computer. You can also try
loops, functions and Stop. Press **Ctrl+C in the harness terminal** when done.

This runs the real browser UI and math engine on Linux. GPIO and ADC use test
stubs; heap/stack and radio behavior do not represent a physical ESP8266.
The native server binds to localhost. On a real board, use the separate
[hardware installation instructions](GETTING_STARTED.md#esp8266-a-real-wemos-d1-mini-4-mb).

## Troubleshooting

- **Installer rejects the host:** the bundled installers are for Linux
  `x86_64`; they do not install native Windows/macOS or Arm packages.
- **QEMU reports a missing `.so` library:** install the QEMU prerequisites
  above. `ldd third_party/esp-qemu/esp_develop_9.2.2_20260417/bin/qemu-system-xtensa`
  identifies missing libraries on Linux.
- **"Run tools/setup_esp_idf.sh first" or missing SDK tools:** rerun the setup
  script from this checkout before building. SDK caches record their install
  path; do not copy another checkout's `third_party/` folder as an installation.
- **"Build first" or missing `flash.bin`:** finish
  `esp32/scripts/external.sh build`, and check its first reported error if it
  fails. Run the launch command from the repository root.
- **Ctrl+A is intercepted in VS Code:** use the uploader's `--qemu` console
  and Ctrl+] exit shortcut described above.
- **Native browser URL does not appear:** leave stdin open by running the
  harness directly in an interactive terminal. A restricted sandbox must allow
  localhost sockets. This is a native harness issue, not a Wi-Fi join problem.

See the [E03 manual](../apps/esp32/e03_external/README.md) for app details and
the [WebREPL manual](../esp8266/docs/USER_MANUAL_L01.md) for browser operation.
