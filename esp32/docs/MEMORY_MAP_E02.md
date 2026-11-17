# E02 memory map and application ownership

Recorded 2026-11-15 from ESP-IDF v5.5.6 and Espressif QEMU. **Emulator
measurements only; no physical board or timing test.** E02 inherits E01's
N16R8 configuration, 32 KiB main stack, internal SDK/UART buffers, 399-byte
math input, 2048-byte typeahead queue, and 4 MiB engine payload cap in PSRAM.
See [E01's map](MEMORY_MAP_E01.md) for that allocator and the numeric limits.

## Foreground program arena

The registry/lifecycle metadata and console callback pointers are static
internal data; program names/descriptions/code are in firmware flash.
MathSmith's session remains allocated in PSRAM while a program owns the
console. The demo's declared arena is **4096 bytes**, requested explicitly
from `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, **outside** the engine payload cap.
The capability allocator's metadata is additional. There is no automatic
fallback to internal RAM.

An app is admitted only after its full declared arena has been allocated.
The launcher zeroes it and passes a borrowed pointer, size, console callbacks
and literal argument text. Arguments point into the main input line on the
still-live main stack; no app may retain that pointer after returning.
The demo stores its 128-byte input buffer inside the arena and uses only
that arena and its small call stack. It does not allocate a separate heap.

The same cleanup path releases the original arena on success, cancellation
and a controlled error return, clears active ownership and resets queued
console input. Unknown programs and failed allocations do not run callbacks;
launches while active are refused. An app shares the firmware address space,
so this is a normal-return contract, not hard-fault isolation.

## Observed heaps and stack

At the fresh MathSmith prompt after software restart, `:mem` reported:

| Measurement | Bytes |
|---|---:|
| Engine payload used / peak | 11504 / 11504 |
| Internal heap free | 343648 |
| External heap free | 8369292 |
| Main stack minimum free at this early point | 30040 |

In the repeated-launch regression, after `x=41` and a numeric output warm-up:

| Measurement | Before and after 12 launches |
|---|---:|
| Engine payload used | 11552 bytes |
| External heap free | 8369220 bytes |

The 12 launches cover four normal exits, four Esc cancellations and four
controlled error returns. Tests compare live engine payload and external
free heap exactly. Peak payload and stack minimum are high-water counters,
so they need not return to their initial values. The minimum stack space
observed after those launches was **25144 bytes**; after the golden numeric
workload it was **25528 bytes**, out of the 32768-byte allocation. These are
specific workloads, not proof all recursion/apps fit.

Native tests inject failed arena allocation and nested launches, and check
zeroing/release/active-state ordering. The actual firmware also refuses app
allocation with PSRAM removed from the emulated machine. No physical PSRAM
signal-integrity or performance result follows from either test.

## Flash and linked sections

| Flash offset | Contents |
|---|---|
| `0x0` | Second-stage bootloader, 18640-byte binary |
| `0x8000` | Partition table, sector reserved |
| `0x9000`, 24 KiB | NVS partition, unused |
| `0xf000`, 4 KiB | PHY partition, unused |
| `0x10000`, 1 MiB factory partition | `mathsmith_os.bin`, **341024 bytes** |
| From `0x110000` to end of 16 MB flash | Unpartitioned, FF-filled |

The complete merged `flash.bin` is **16777216 bytes**, raw, starting at
flash offset **0x0**. `workstation.sh size` reports **340901 bytes** before
padding, **71471 bytes DIRAM** and **16384 bytes IRAM** used in its categories.
The new registry/backend adds 48 bytes to E01's linked BSS in this build.

| Linked section | Start | Size |
|---|---|---|
| Instruction vectors | `0x40374000` | `0x404` |
| Internal instruction text | `0x40374404` | `0xf7b3` |
| Initialised internal data | `0x3fc93c00` | `0x2ff0` |
| Zero-initialised internal data | `0x3fc96bf0` | `0x2b88` |
| Mapped flash instructions | `0x42000020` | `0x32f7e` |
| App description | `0x3c040020` | `0x100` |
| Mapped constants | `0x3c040120` | `0xd760` |

As in E01, linked windows/aliases are not independent physical capacities;
the SDK maps PSRAM at startup. The generated `mathsmith_os.map` and `sdkconfig`
are authoritative and are included in the candidate. QEMU uses a disposable
copy of the flash image, with no network device or physical serial port.

The existing QEMU calibration warning remains recorded. Hardware procedure:
[E02 README](../../apps/esp32/e02_launcher/README.md).
