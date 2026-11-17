# E03 memory map and external program ownership

Recorded 2026-11-15, ESP-IDF v5.5.6 / Espressif QEMU; **emulator measurements
only, no physical validation or timing claim**. Inherits E01/E02's N16R8
configuration, 32768-byte main stack, internal SDK/UART buffers, 399-character
math line, 2048-byte typeahead queue, and 4 MiB PSRAM engine payload cap.
See [E01](MEMORY_MAP_E01.md) and [E02](MEMORY_MAP_E02.md) for those contracts.

## Allocations and lifetime

| Allocation | Location | Lifetime / bound |
|---|---|---|
| MathSmith session/values | PSRAM | Resident, engine payload cap 4194304 bytes |
| Embedded A package | Mapped firmware constants | 2260 bytes, permanent fixture |
| Uploaded package | PSRAM capability heap | Until replace/unload/reboot; at most 262144 bytes |
| Upload candidate | PSRAM capability heap | Alongside old package until checked/committed/rejected |
| Relocated ELF text/data | PSRAM, upstream loader | Temporary validation or foreground run; deinitialised afterwards |
| App scratch arena | PSRAM capability heap | Zeroed on admission, freed after return; 4096 bytes for demo, cap 1048576 |
| Loader/API metadata | Internal static data/main stack | OS lifetime or synchronous load call |

The engine cap counts only engine payload. App arenas, stored packages,
relocated images and capability-allocator overhead are additional. An input
size cap is not a guarantee that every valid ELF fits: relocated section
sizes/metadata need memory too. Failed allocation/linking returns an error;
there is no automatic fallback of app arenas/packages to internal RAM.

During upload, keep the previous package while allocating the candidate and
its temporary validation image. Validate/link without entry execution, free
the temporary ELF, then replace the old package only on success. Failures
release the candidate and preserve the previous slot. On launch, allocate the
arena, relocate a fresh image, expose the borrowed ABI context, enter the app,
clear the context, deinitialise ELF and release the arena on normal return.
No relocated image stays cached between runs; module `.bss` starts fresh.

All apps use the main call stack/shared address space. CPU faults, retained
pointers and arbitrary extra resources are outside the normal-return cleanup
contract. No task/process isolation or hard-fault recovery is supplied.

## Observed heap and stack

In the interactive B-upload session, with no math variables assigned:

| Measurement | B installed, after run/exit | After `:unload` |
|---|---:|---:|
| Engine payload used / peak | 11504 / 11504 | 11504 / 11504 |
| Internal heap free | 341624 | 341624 |
| External heap free | 8366984 | 8369292 |
| Main stack minimum free | 29704 | 29704 |

The 2260-byte stored package consumes 2308 bytes including allocator costs
in this run. App arena and relocated image were already freed at both prompts.

The automated repeated-load test warms numeric output and the sample loader,
sets `x=41`, then performs nine upload/run/unload cycles, mixing normal exit,
Esc and controlled error. Before/after values match exactly: engine payload
**11552**, internal free **341404**, external free **8369220** bytes; stack
minimum **25144/32768** bytes. Every module run also checks its `.bss` starts
fresh. The full golden numeric workload observed minimum free stack **25528**
bytes. These are workload observations, not bounds on arbitrary app stack use.

Native tests cover allocation admission/cleanup and package bounds. E03 with
emulated PSRAM absent refuses external app allocation/upload and keeps the
diagnostic shell usable. Physical PSRAM execution/cache behaviour still needs
the board test; QEMU does not establish its speed or signal integrity.

## Flash and linked sections

| Flash offset | Contents |
|---|---|
| `0x0` | Second-stage bootloader, 18640-byte binary |
| `0x8000` | Partition table, sector reserved |
| `0x9000`, 24 KiB | NVS, unused |
| `0xf000`, 4 KiB | PHY, unused |
| `0x10000`, 1 MiB factory partition | `mathsmith_load.bin`, **355968 bytes** |
| `0x110000` to end of 16 MB | Unpartitioned, FF-filled |

Merged `flash.bin`: **16777216 bytes**, raw, offset **0x0**. IDF size reports
**355857 bytes** before binary padding; categories include **73403 DIRAM**,
**16384 IRAM**, **217318 flash code** and **60064 flash data** bytes.

| Linked section | Start | Size |
|---|---|---|
| Instruction vectors | `0x40374000` | `0x404` |
| Internal instruction text | `0x40374404` | `0xfc03` |
| Initialised internal data | `0x3fc94100` | `0x3264` |
| Zero-initialised internal data | `0x3fc97368` | `0x2c50` |
| Mapped flash instructions | `0x42000020` | `0x350e6` |
| App description | `0x3c040020` | `0x100` |
| Mapped constants | `0x3c040120` | `0xe9a0` |

Aliased mapped windows are not independent physical capacities. PSRAM is
mapped at startup; the generated `mathsmith_load.map` and `sdkconfig` are
authoritative and accompany the candidate. QEMU uses a disposable flash copy
and no physical serial port; a virtual PTY additionally tests host termios I/O.

Protocol: [SERIAL_UPLOAD.md](SERIAL_UPLOAD.md). ABI:
[WORKSTATION_APP_ABI.md](../../docs/WORKSTATION_APP_ABI.md). Hardware procedure:
[E03 README](../../apps/esp32/e03_external/README.md).
