# Separately compiled workstation demo

E03 wraps the portable [demo](../demo/README.md) in an ELF entry point. It is
compiled independently of the OS, using the pinned ESP32-S3 Xtensa compiler.
The module imports only `wk_app_api_v1` and `strcmp`; console I/O and its
zeroed 4096-byte arena come from the versioned host context.

`entry.c` checks ABI version/size, checks its `.bss` counter starts fresh,
prints the A/B greeting, runs the demo and reports its return status through
`finish` before returning. Reloading creates fresh module data each time.
There are no file, SDK, MathSmith-session or persistent-state dependencies.

From the repository root:

```sh
esp32/scripts/external.sh apps
python3 esp32/scripts/upload_external.py --qemu build/esp32/e03_external/demo_b.msp
```

Then type `:app uploaded` at the MathSmith prompt. Expect `External demo B`
and `demo>`. `exit`, Esc or `fail` return with success, cancellation or a
controlled error. Ctrl+] closes the host uploader console.

The A and B packages are each 2260 bytes in the recorded build. A is also
embedded as aligned package bytes for `:app sample`; B is absent from the
firmware image and must be transferred. `external.sh apps` rebuilds packages
without building or flashing the OS. Firmware changes to the exported ABI
require compatible app builds.

See [E03 instructions](../../esp32/e03_external/README.md),
[package/ABI](../../../docs/WORKSTATION_APP_ABI.md) and
[serial protocol](../../../esp32/docs/SERIAL_UPLOAD.md).
