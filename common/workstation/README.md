# Portable foreground application launcher

E02 (XD109) supplies a small C89 lifecycle independent of ESP-IDF, MatmMax,
display hardware and filesystems. ESP32-S3 provides the first backend; RP2350
can reuse this code with its own service callbacks. No RP2350 build is claimed.

`WkApp` declares a name, description, required arena size and `run` callback.
`WkServices` provides allocation/release, character input, text output and input
reset. `wk_init` validates the registry and callbacks; `wk_find` looks up a name;
`wk_run` starts one registered program synchronously.

Lifecycle:

1. Refuse a nested launch or unknown program.
2. Mark the program active and allocate its declared arena. If allocation
   fails, clear active ownership and return `WK_NO_MEMORY` without running it.
3. Zero the arena, give the app its console services and borrowed argument
   text, reset queued input, then call `run`.
4. On success, cancellation or an error return, reset queued input, free the
   **original** arena and clear active ownership. Return a status to the shell.

Apps own the foreground console while running. Their arena and arguments are
borrowed only for that call; apps must not retain them or free the arena.
All temporary app buffers should come from the declared arena. Resources
outside it would need explicit lifecycle support before use. The host shell
keeps its session; no MatmMax pointer appears in this API.

The model is trusted code in one address space and one foreground call stack.
The portable launcher does not itself load executable files; E03 adds an
ESP32-S3 loader backend. There is no process isolation, multitasking or recovery
from a hard fault. Cleanup covers callbacks that **return**.

Current portable app: [demo](../../apps/workstation/demo/README.md).
ESP32 integration: [E02](../../apps/esp32/e02_launcher/README.md).
Native C89 checks are run by `esp32/scripts/workstation.sh test`
(`esp32/tests/test_launcher_native.py`).

E03 adds `app_api.h` (versioned module bootstrap/context) and `package.[ch]`
(bounded MSAP header validation and incremental CRC32). These preserve the
foreground lifecycle while allowing separately linked code. See
[package/ABI specification](../../docs/WORKSTATION_APP_ABI.md) and
[E03 integration](../../apps/esp32/e03_external/README.md).
