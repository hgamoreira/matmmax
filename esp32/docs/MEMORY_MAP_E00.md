# E00 memory and flash map

Recorded 2026-11-15 from ESP-IDF **v5.5.6**, the generated linker map and E00
running in Espressif QEMU `esp_develop_9.2.2_20260417`. **Emulator measurements
only; the physical N16R8 board has not been tested.**

## Configuration and ownership

- ESP32-S3, SDK default CPU frequency **160 MHz**. No timing benchmark claimed.
- **16 MB flash**, DIO at **40 MHz**; **8 MB octal PSRAM**, **40 MHz**.
- Default malloc, SDK services and the main task's **8192-byte stack** use
  internal RAM. External allocations explicitly request
  `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`.
- The command line is a **128-byte array on the main stack**. UART RX has a
  **1024-byte internal driver ring**. TX uses the blocking driver path.
- The test owns **4096 internal bytes**, frees them, then owns **1048576
  external bytes**, frees them, checks oversize rejection and recovers with
  another **4096 external bytes**. No retained app workspace/framebuffer yet.
- Flash holds code/constants, not a filesystem. PSRAM is working memory.
  E00 does not initialise/use the SDK's default NVS and PHY data partitions.

## Flash offsets in the merged image

| Offset | Size/reservation | Contents |
|---|---|---|
| `0x000000` | Binary 18640 bytes | Second-stage bootloader |
| `0x008000` | 4 KiB sector reserved | Partition table |
| `0x009000` | 24 KiB | NVS partition (unused) |
| `0x00f000` | 4 KiB | PHY partition (unused) |
| `0x010000` | 1 MiB partition | Factory app, binary 209520 bytes |
| `0x110000`–`0xffffff` | Remaining flash | Unpartitioned, FF-filled |

`flash.bin` is **16777216 bytes**, starting at flash offset **0x0**, including
the bootloader/table/app with FF-filled gaps. QEMU receives a temporary copy;
physical flashing is a separate user action in the application's README.

## Linked virtual addresses

These are **linker windows and section addresses**, not independent physical
capacities to add together. D/IRAM aliases shared memory; flash data and PSRAM
share a virtual address window. The SDK chooses the external mapping at boot.

| Section/window | Start | Extent from this map |
|---|---|---|
| Internal instruction window | `0x40374000` | `0x57700` bytes reserved |
| Instruction vectors | `0x40374000` | `0x404` bytes |
| Internal instruction text | `0x40374404` | `0xf77b` bytes |
| Internal data window | `0x3fc88000` | `0x53700` bytes reserved |
| Initialised data | `0x3fc93c00` | `0x2ff0` bytes |
| Zero-initialised data | `0x3fc96bf0` | `0x938` bytes |
| Mapped flash instructions | `0x42000020` | `0x16f72` bytes |
| App description | `0x3c020020` | `0x100` bytes |
| Mapped constants | `0x3c020120` | `0x95f4` bytes |
| Flash-data/PSRAM window | `0x3c000020` | `0x1ffffe0` bytes, not physical PSRAM capacity |
| RTC fast window | `0x600fe000` | `0x1fe8` bytes plus 24 reserved bytes |
| RTC slow window | `0x50000000` | `0x2000` bytes |

Authoritative generated files: `build/esp32/e00_console/ms_console.map` and
`sdkconfig`; the hardware candidate includes copies. `console.sh size` reports
**209397 bytes image size** before padding, **62631 bytes DIRAM** and **16384
bytes IRAM** used. The size tool's IRAM category is a specific reserved region;
its 100% figure does not mean all chip SRAM is full.

## Runtime heaps observed in QEMU

At the ready prompt and after **three** `test` commands:

| Heap (byte-addressable capability) | Total | Free | Largest free block |
|---|---:|---:|---:|
| Internal | 399604 | 377608 | 311296 |
| External | 8388608 | 8383888 | 8257536 |

These are allocator measurements after startup, not nominal chip capacities.
Metadata/reservations and fragmentation affect free space/largest block.
Automated tests compare the external heap before/after repeated tests.

QEMU startup prints `rtcinit: o_code calibration fail`. All nine tests pass
with that warning present. Real-board analog/clock calibration, timing, PSRAM
signal integrity and throughput are not verified. Record physical warnings
separately.

References: [E00 source/procedure](../../apps/esp32/e00_console/README.md),
[SDK external RAM](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-guides/external-ram.html),
[SDK heap allocator](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/mem_alloc.html).
