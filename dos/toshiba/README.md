# MatmMax for DOS (8086/CGA preview)

MatmMax targets compatible DOS PCs with an Intel 8086, 640 KB of
conventional RAM, DOS 3.30, and CGA-compatible graphics. You can also
run it in DOSBox-X. Plotting uses BIOS mode 6 (640×200). The app uses
software floating point and works offline. Use a writable directory
for editing scripts.

The executable and its banner retain the original names
`MATHSMTH.EXE` and MathSmith M08.

The preview provides matrices, numerical functions, FFTs, scripts,
a full-screen editor and CGA plots. The [quick manual](toshiba/docs/MATMMAX_PREVIEW.txt)
covers the prompt, example scripts, plotting and editor keys. The
[full DOS user’s guide and reference manual (PDF)](../../docs/MATMMAX_MANUAL.pdf)
covers the language, numerical methods, and both 16-bit DOS builds.

## Build on Linux and run in DOSBox-X

On Linux, install GNU Make and [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2/releases/tag/2025-12-01-Build)
with its 16-bit DOS tools and libraries. The path here is an example. Run
these commands from `dos/toshiba` in the MatmMax checkout.

```sh
make WATCOM=/opt/open-watcom
mkdir -p build/dos-run
cp build/m01_mathsmith/MATHSMTH.EXE apps/m01_mathsmith/examples/*.m build/dos-run/
dosbox-x -conf dosbox-x.conf -c "mount c build/dos-run" -c "c:" -c "mathsmth"
```

At the MatmMax prompt, enter these lines.

```text
A=[4 -2;-2 4];
A\[2;8]
run primes
plot(1:5,[3 1 4 1 5])
```

The solve prints 2 and 3 as a column. The script finds 15 primes up to 50.
Press a key to return from the plot. Use `quit` to return to DOS, or
`exit` at the DOSBox-X DOS prompt to close the emulator.

For automated checks on Linux, install GCC, Python 3 and DOSBox-X,
then run `make WATCOM=/opt/open-watcom test` from the same directory.

## Run on a DOS PC

Copy the contents of `build/dos-run` to a writable DOS directory. A
DOS-formatted 720 KB floppy also works if your machine has that drive.
Boot DOS and type `MATHSMTH` from that directory. Try the solve, script
and plot above. To check the editor, enter `edit hello`, type
`disp(20)`, press F2 to save and press F5 to run. Expect 20.

Core calculations, scripts and CGA plots were tested on a Toshiba T2100
in earlier builds. The source preview is tested in DOSBox-X. A fresh
physical test of the current preview, including the editor, remains open.

## Optional AI helper

Build `MSASK.EXE` only if you want the assistant. It needs a packet
driver and a working DOS IP connection, a host running the provided
server, and Ollama. The Toshiba used serial SLIP via an external
gateway. The [helper guide](toshiba/docs/MSASK_SETUP.txt) describes the DOS setup.
The [sample configuration](MSASK.SMP) uses example addresses.

Build the 16-bit large-model Watt-32 library with the pinned upstream
source and DOSBox-X. Run this from `dos/toshiba`.

```sh
git clone https://github.com/gvanem/Watt-32.git third_party/Watt-32
git -C third_party/Watt-32 checkout c727114d209138e3057c122134cf7c3f3e67c374
WATCOM=/opt/open-watcom tools/build_watt32.sh
make WATCOM=/opt/open-watcom APP=m07_msask
```

Copy `build/m07_msask/MSASK.EXE` and `MSASK.SMP` to the MatmMax DOS
directory. Rename or copy the sample to `MSASK.CFG` and set your DOS
client and server addresses. On the host, start Ollama and run the
server with a model installed on that host.

```sh
mkdir -p build/assistant-files
python3 host/server.py --bind HOST_LAN_IP --root build/assistant-files --model INSTALLED_MODEL
```

Replace `HOST_LAN_IP` and `INSTALLED_MODEL` with your values. The
workstation server listens on port 8064 by default and provides file
access to its chosen root, so use a dedicated directory and a trusted
LAN. The full hardware test of the assistant remains open.

## Licensing

The project source is [MIT-licensed](LICENSE). This preview distributes
source. Obtain the [Open Watcom toolchain](https://github.com/open-watcom/open-watcom-v2)
and optional [Watt-32 library](https://github.com/gvanem/Watt-32) from
their upstream projects, under their respective terms.
