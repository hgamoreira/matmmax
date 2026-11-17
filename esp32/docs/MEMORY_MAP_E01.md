# E01 memory map and ownership

Recorded 2026-11-15 from ESP-IDF v5.5.6, the generated linker map and the
ESP32-S3 QEMU model. **No physical-board validation or speed measurement.**
E01 is a separate image from E00; the board target stays N16R8, UART0 at
115200 baud, 16 MB DIO flash and 8 MB octal PSRAM, both at 40 MHz. The SDK's
default CPU setting is 160 MHz.

## Allocation policy

| Owner | Placement/size | Lifetime |
|---|---|---|
| Main task stack | Internal, 32768 bytes | Firmware run |
| Current input line | 400 bytes on main stack | One input line |
| UART RX driver ring | Internal, 4096 bytes | Firmware run |
| Calculation typeahead queue | Internal static, 2048 bytes (2047 usable) | Between polling and input processing |
| Engine session, values, statements, FFT/solver scratch | Explicit byte-addressable PSRAM | As owned by the existing engine |
| Engine payload cap | 4194304 bytes (4 MiB) | Fixed cap, reported by `:mem` |
| Allocation headers | 8 bytes per adapter allocation | Same lifetime as payload; additional to the cap |

The ESP-IDF capability allocator has its own metadata/reservations too; the
payload count is not the physical heap footprint. SDK allocations and default
malloc stay internal. The wrapper provides malloc/calloc/realloc/free only for
this build of the five shared source files. It does not interpose SDK libc.
Each engine allocation uses `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, with an
8-byte-aligned header so returned double arrays stay aligned. No internal-RAM
fallback. Overflow/budget/backend failure returns NULL; failed realloc leaves
the original block and accounting intact. Allocation and accounting happen on
the single foreground main task; this is not a general multitasking allocator.

There is no filesystem or display buffer. Plot data may be allocated by the
engine, but no plot bitmap is rendered by this front end. The engine retains
its bounded pool of up to 48 scalar matrix blocks: `clear` or `:reset` need
not reduce used payload to zero. Peak usage is a high-water value since boot.

## Limits and observed boot state

Default small-engine limits: **40 variables, 4096 elements/matrix**. A 64x64
numeric matrix has 32768 bytes of elements, plus Mat and allocation headers.
FFT output has two columns, limiting the initial build to 2048 samples. The
4 MiB cap is a development policy, not a measured maximum or reservation for
future apps; the remaining physical heap is still owned by the SDK allocator.

At a fresh ready prompt, `:mem` in QEMU reports:

| Measurement | Bytes |
|---|---:|
| Engine payload used / peak | 11504 / 11504 |
| Internal heap free | 343784 |
| External heap free | 8369292 |
| Main stack minimum free, at this early point | 30040 |

Values depend on prior work and SDK configuration. Repeated matrix
allocate/clear cycles are tested for equal live engine payload after warming
the scalar pool. The golden test also checks that the 32 KiB stack retains
more than 4096 bytes of unused high-water margin after its workload:
**25528 bytes** were observed free at that high-water mark. This is
not a proof that all possible recursion paths fit. Firmware logs remain in
`build/esp32/e01_mathsmith/test-logs/`.

Native tests inject allocation failures with the same adapter/accounting and
libc as backend, checking workspace preservation. The target firmware does
not expose the injection knob. Its PSRAM placement follows the capability
allocator; E00 remains the actual target write/read and over-capacity test.

## Flash and linked windows

| Offset/size | Contents |
|---|---|
| Flash `0x0`, bootloader 18640 bytes | Second-stage bootloader |
| `0x8000`, sector reserved | Partition table |
| `0x9000`, 24 KiB | NVS partition, unused |
| `0xf000`, 4 KiB | PHY partition, unused |
| `0x10000`, 1 MiB factory partition | `mathsmith_s3.bin`, **338240 bytes** |
| Remaining flash from `0x110000` | Unpartitioned, FF-filled |

The merged image is **16777216 bytes**, raw, flashed at offset **0x0**.
Its QEMU copy is disposable; the build/candidate images are not opened by
QEMU directly. `mathsmith.sh size` reports **338125 bytes** before binary
padding, **71423 bytes DIRAM** and **16384 bytes IRAM** in its region categories.

| Linked section | Start | Size |
|---|---|---|
| Instruction vectors | `0x40374000` | `0x404` |
| Internal instruction text | `0x40374404` | `0xf7b3` |
| Initialised internal data | `0x3fc93c00` | `0x2ff0` |
| Zero-initialised internal data | `0x3fc96bf0` | `0x2b58` |
| Mapped flash instructions | `0x42000020` | `0x328e6` |
| Application description | `0x3c040020` | `0x100` |
| Mapped constants | `0x3c040120` | `0xd320` |

These addresses describe sections, not independent physical capacities.
D/IRAM aliases memory; flash data and PSRAM share a virtual window beginning
at `0x3c000020` of length `0x1ffffe0`. The SDK maps physical PSRAM during boot;
the window size is not the fitted memory capacity. See the generated
`mathsmith_s3.map` and `sdkconfig` for the authoritative complete map.

The `rtcinit: o_code calibration fail` QEMU warning is still present. Nothing
here validates real-board clock/analog calibration, timings, Wi-Fi, USB host
or the physical PSRAM interface. Physical procedures:
[E01 README](../../apps/esp32/e01_mathsmith/README.md).
