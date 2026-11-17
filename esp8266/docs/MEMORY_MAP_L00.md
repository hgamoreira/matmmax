# L00 linked memory map and ownership

2026-11-15, Wemos D1 mini profile, ESP8266 Arduino core 3.1.2 / L106 GCC10.3.
**Linked measurements plus user hardware boot/eigenvalue readings below. No
firmware emulator run; full numbered hardware validation remains open.**

## Linked layout

| Section | Address | Size (bytes) |
|---|---|---:|
| `.data` | `0x3ffe8000` | 1508 |
| `.noinit` | `0x3ffe85e4` | 56 |
| `.rodata` in DRAM | `0x3ffe8620` | 1896 |
| `.bss` | `0x3ffe8d88` | 43848 |
| Instruction vectors/entry `.text` | `0x40100000` | 226 |
| Other IRAM `.text1` | `0x401000e4` | 27013 |
| Mapped flash `.irom0.text` | `0x40201010` | 347676 |
| Image length / CRC words | `0x40201010` / `0x40201014` | 4 each, fixed by core image format |
| Dedicated continuation object | `0x3ffef0d0` | 16424 |
| Heap init entry `umm_init` | `0x40100ca4` | 50, IRAM before cache is enabled |
| Heap init helper `umm_init_heap` | `0x40100c04` | 147, IRAM |
| Heap start symbol | `0x3fff38d0` | Dynamic SDK/application allocations follow |

The core size report totals `.data/.rodata/.bss` as **47252 / 80192 bytes**.
It omits the separate 56-byte `.noinit` and alignment gaps; the occupied DRAM
span from the section start to `_heap_start` is **47312 bytes**. IRAM code
is **27239 bytes**; the balanced setting reserves another **32768 bytes** for
instruction cache. These capacities are separate from the 4 MB flash.

`mathsmith_light.bin` is **382464 bytes**, includes the boot image and is
flashed at physical offset **0x0**. This is not a complete 4 MB erase/write
image. The pinned `eagle.flash.4m.ld` reserves an approximately 1 MB mapped
application window and no filesystem: FS start/end `0x3fb000`, EEPROM sector
`0x3fb000`, RF calibration `0x3fc000`, SDK Wi-Fi settings `0x3fd000` onward.
L00 does not mount a filesystem or implement persistent workspace storage.
SDK startup may use its reserved sectors; firmware-only flashing preserves
other sectors rather than promising that SDK execution never touches flash.

The generated `mathsmith_light.map`, configuration and source fingerprints
accompany the candidate and are authoritative for this build.

## Allocation policy

- The continuation stack is **16384 bytes plus 40 bytes of core metadata**,
  reserved in BSS. The core's dedicated-stack entry overrides its default
  approximately 4 KiB borrowed SYS stack. `CONT_STACKSIZE` is identical for
  all core/assembly/app compilation units. Link checks verify the BSS object.
- Engine session, values, pending statements and numerical scratch use the
  ordinary DRAM allocator through an accounting adapter. The **16 KiB cap**
  counts payload, not the 8-byte adapter header or SDK allocator overhead.
  It is an upper bound, not a reservation or guaranteed free capacity.
- The compiled 32-bit `MlSession` structure is **4832 bytes**, found in target
  debug information. Opening the engine also creates owned values; the first user workload
  readings are recorded below, not an empty-workspace baseline.
- Failed allocation/reallocation returns NULL, retaining any original realloc
  buffer and accounting. The scalar cache retains up to eight blocks; clear/
  reset need not return payload to zero. Repeated workloads are compared after
  warming the cache. App arenas and an external loader do not exist in L00.
- Console line: **192 static bytes**, with 191 characters usable. Calculation
  typeahead: **256 static bytes**, 255 usable. RX driver buffers/SDK metadata
  and runtime housekeeping allocations are additional to the engine cap.
- Generated profile lowers formatting buffers to 128/256 bytes, saves static
  RAM, and retains error bounds. Engine constants and builtin tables are
  mapped in flash; the pinned core's NON32XFER handler services byte reads.
  This has a performance/stack cost requiring target measurement.

## Stack and checks

Generated `parse_unary` checks the live stack pointer against the dedicated
stack base plus a **6144-byte reserve**, returning an engine error if low.
The fast scalar parser also checks it and falls back to the normal error path.
Input brackets/unary chains are bounded at eight; function depth/counts and
block statements are reduced. This is a normal-return defence, not a proof
that every possible expression/library path fits or recovery from CPU faults.

Compiler `.su` reports list individual frames, not complete call-chain maxima.
Largest engine frames in this profile include `format_text` **1328 bytes**,
`exec_simple` **944**, `ml_eval` **832**, `exec_range` **672** and
`parse_primary` **496**. Library calls, nested functions, exception handling
and watchdog yielding add stack use. `:mem` reports the core's painted-stack
high-water margin, which must be measured for the hardware workloads.

Nineteen automated checks cover native numeric/control/allocation behaviour and
the linked target's sections, stack reservation, handler placement, image
capacity and source freshness. Native failure injection proves those code
paths only; host pointers/layout, libc and timing differ from the target.
The earlier startup failures and both linker corrections are recorded in the
[L00 README](../../apps/esp8266/l00_mathsmith/README.md). After correcting
allocator placement, the user reported a usable prompt and correct `eig(A)`.

## First physical-board measurements, reported 2026-11-15

Transcribed user output (local hardware log omitted; see the validation summary)
confirms core 3.1.2, CPU 80 MHz, flash 4194304 bytes and Wi-Fi off. Measurements:

| Reading | Startup, before engine allocation | After matrix entry and eig(A) |
|---|---:|---:|
| Free heap | 33264 | 27888 |
| Largest free heap block | 33264 | 27304 |
| Fragmentation | 0% | 3% |
| Engine payload used / peak | 0 / 0 | 5072 / 5664 |
| Minimum free continuation stack | 15536 / 16384 | 11596 / 16384 |

Startup diagnostics run **before ml_open**; the 33264-byte heap reading is not
an initialised empty-workspace baseline. The subsequent workload retains the
4x4 matrix, its eigenvalue result and scalar cache. Stack high-water use is
4788 bytes for this workload. These readings establish this calculation fits;
they do not measure maximum capacity, long-run heap recovery, cancellation or
speed. Repeated cleanup and restart/cold-boot checks remain unreported. No
firmware changes were made after receiving this successful transcript.

Hardware steps and limits: [L00 README](../../apps/esp8266/l00_mathsmith/README.md).
Platform/setup: [ESP8266 README](../README.md).
