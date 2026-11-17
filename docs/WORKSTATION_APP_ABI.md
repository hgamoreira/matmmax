# Workstation native app ABI and package v1

E03, XD110, 2026-11-15. Implemented first for **ESP32-S3**, using the pinned
ESP-IDF v5.5.6 Xtensa toolchain (esp-14.2.0_20260121). RP2350 needs its own
compiler, target identifier and loader backend; none is implemented here.
`.m` interpreter scripts remain separate from native packages.

## Entry and services

Build a freestanding, position-independent ELF32, little-endian, Xtensa
application with entry `void app_main(int argc, char **argv)`. E03 passes
argc=0/argv=NULL; literal console arguments are in the workstation context.
The build/strip flags follow Espressif's section-based ELF loader workflow.
Use `esp32/scripts/build_external.py` and the external demo as the initial
template; ordinary firmware ELFs are not the app build product.

The module imports `wk_app_api_v1()` from the firmware. The returned
`WkAppApiV1` has:

| Field | Meaning |
|---|---|
| `abi_version` | Must equal `WK_APP_ABI_V1` (1) |
| `struct_size` | Must cover the structure compiled into the module |
| `context` | Borrowed `WkAppContext`: console services, arena and argument string |
| `host` | Opaque value to pass unchanged to `finish` |
| `finish(host, result)` | Report `WK_APP_OK`, `WK_APP_CANCEL` or `WK_APP_ERROR` |

The app checks version/size/context, runs, reports its status once, then
returns from entry. Returning without reporting a status is an application
error. The bootstrap returns NULL outside an active app entry call. Pointers
and arguments are borrowed only for that invocation; no MatmMax session
pointer is exported. All temporary app buffers must fit the declared arena.
Additional resources outside that contract are not automatically reclaimed.

E03 exports only the bootstrap and `strcmp` (used by the portable demo).
General SDK/libc symbol tables and dynamic shared-library loading are off.
An unavailable import causes linking to fail before entry runs. Adding host
services or changing layouts requires explicit ABI compatibility decisions;
the initial ABI is tied to matching 32-bit target/toolchain builds, not a
claim of arbitrary compiler/C++ compatibility.

Apps are trusted code sharing the firmware address space/main stack. The
loader/lifecycle handles ordinary returns and linking errors; this is not
process isolation or recovery from an app hard fault.

## MSAP package header

All integers are little-endian; no padding. Header length is **32 bytes**.

| Offset | Bytes | Value |
|---|---:|---|
| 0 | 4 | ASCII `MSAP` |
| 4 | 2 | Package format version, 1 |
| 6 | 2 | Header length, 32 |
| 8 | 4 | ELF machine, 94 (`EM_XTENSA`) |
| 12 | 4 | Workstation API major version, 1 |
| 16 | 4 | Required app arena bytes, 1..1048576 |
| 20 | 4 | Exact ELF payload length, at least 52 |
| 24 | 4 | CRC32 of ELF payload only |
| 28 | 4 | Workstation target identifier, 1 = ESP32-S3 |
| 32 | payload length | ELF image |

The complete package is capped at **262144 bytes**, including the header.
Trailing bytes and truncation are rejected. `package.c` checks format, machine,
specific target, API major, arena/length bounds and CRC32 without unaligned
integer loads. The target loader then validates ELF structure, relocation
bounds/imports and executable entry before admission. Metadata does not make
native code isolated or establish that an arbitrary program respects the API.

CRC32 uses the reflected polynomial `0xedb88320`, initial/final XOR of all
ones: the same result as Python `zlib.crc32(data)`. The incremental C API
accepts a previous completed CRC, starting with zero. This is an integrity
check, not an authenticity mechanism.

## Runtime ownership

`sample` loads the separately built A fixture embedded as **aligned package
bytes**, not statically linked executable app functions. The portable demo is
also linked into the OS as E02's `demo`, for comparison. Variant B's distinct
greeting/code is absent from firmware and is transferred as a new file.

For an external run, the existing foreground launcher allocates/zeroes the
declared arena. Espressif's loader relocates text/data into PSRAM, resolves
only the explicit imports and runs entry. The module's test `.bss` counter
must start at zero on every load. On return, the API context is cleared, the
ELF is deinitialised and its allocations released, and the launcher releases
the arena/restores console input. MatmMax's resident workspace is preserved.

The uploaded slot stores only the validated package; each run loads a fresh
executable image. Upload validates/links the candidate **without running it**
before replacing the previous package. `:unload` frees the slot; reboot loses
it. Persistent storage, SD and multiple installed filenames are later scopes.

References: [ABI header](../common/workstation/app_api.h),
[package parser](../common/workstation/package.c),
[serial protocol](../esp32/docs/SERIAL_UPLOAD.md),
[Espressif loader v2.1.0](https://components.espressif.com/components/espressif/elf_loader/versions/2.1.0/readme?language=en).
