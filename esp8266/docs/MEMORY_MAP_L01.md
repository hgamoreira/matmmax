# L01 memory map — browser console and GPIO

**L02-r1 (XD118):** image 507,936 bytes; static DRAM 57,072 (+668 over R14);
IRAM code 28,276 (+64, 4,492 left); LittleFS at physical `0x300000..0x3F9FFF`.
Details and what is still unmeasured: [PROGRAM_STORE_L02.md](PROGRAM_STORE_L02.md).

17 November 2026. Linked target measurements for the Wemos D1 mini, core 3.1.2,
CPU 80 MHz, 32 KiB instruction cache plus 32 KiB IRAM. **R14 linked layout below;
runtime readings include the user's R13 post-editor-sum snapshot below.
R12 standalone startup/browser passes; R14 post-blink snapshot is now recorded; sustained-use tests remain open.** Host and Chrome checks use
a POSIX HAL, not an ESP8266 emulator.

## Linked layout

| Section / symbol | Address | Bytes |
|---|---|---:|
| `.data` | `0x3ffe8000` | 1516 |
| `.noinit` | `0x3ffe85ec` | 56 |
| `.rodata` in DRAM | `0x3ffe8630` | 7320 |
| `.bss` | `0x3ffea2c8` | 47568 |
| `.text` vectors/entry | `0x40100000` | 363 |
| `.text1` IRAM | `0x4010016c` | 27849 |
| `.irom0.text` mapped flash | `0x40201010` | 426180 |
| CRC length / value | `0x40201010` / `0x40201014` | 4 each |
| Dedicated continuation object | `0x3fff15c0` | 16424 |
| `umm_init` / `umm_init_heap` | `0x40101040` / `0x40100fa0` | 50 / 147 |
| `_heap_start` | `0x3fff5c98` | Dynamic allocations follow |

Static `.data/.rodata/.bss` totals **56404 / 80192 bytes**. Including `.noinit`
and alignment, occupied DRAM is **56472 bytes**, 208 more than R13's 56264.
IRAM code is **28212 / 32768 bytes**, leaving 4556 bytes before any future
drivers. Instruction cache is an additional separate 32768-byte reservation.
The L01-r14 image is **467376 bytes**, flashed at offset 0; this is not a full 4 MB
flash backup. The actual ELF/map and input fingerprints accompany the release.

## Ownership

R14 adds208bytes rodata/static occupancy, no BSS/IRAM growth and2256bytes image
vs R13. GPIO builtin code/table/string helpers are included only in the L01
generated engine and use existing engine allocation accounting. Typed return
scalars are allocated before device access. The shared console/builtin registry
replaces duplicate pin state rather than reserving a new hardware arena. Delay
uses fixed local scalars/polling and SDK yielding, with no buffer/heap reserve.
R13's measured heap/stack snapshot below does not establish R14 margins. Use
[GPIO hardware procedure](SCRIPT_GPIO_L01.md) for connected blink/Stop/math :mem.


R13 adds16bytes static BSS for result/cancellation state, no extra IRAM, and
7680bytes image/IROM over the working R12 image. The larger editor page remains
in flash and streams using the existing128byte page scratch buffer; no whole-
page RAM copy. Browser source up to32KiB/1024physical lines and its queue live
on the browser device. The ESP8266 still queues one192byte command; engine
loop/function block storage retains its16KiB payload cap. No board file or
source arena is added. Actual R13 connected/repeated-program heap/stack margins
remain pending; use [editor hardware test](BROWSER_EDITOR_L01.md).

R10 adds a fixed DHCP metadata/counter observer at the existing SDK/lwIP hook.
Static occupancy increases544bytes over R9, including160 BSS bytes; image
increases2256bytes. Callback storage is bounded, with no heap allocation,
printing, yielding or retained packet buffers. Formatting/snapshot occurs
in `:wifi diag` on the continuation stack. R10's measured idle/join margin is recorded below. R11 adds104bytes static BSS
and556bytes rodata (656bytes occupied after alignment), and1040bytes image.
The observer trace is236bytes; the diagnostic snapshot and packet local grow
within the existing continuation stack. No capture buffer/heap reservation.
R12 adds144bytes mapped flash/image and no static RAM over R11. Its outgoing
construction hook has no heap allocation or capture buffer. Stack usage has
not been measured on R11/R12 hardware. [R12 procedure](DHCP_BROADCAST_L01.md).
[Boundary/validation](DHCP_PACKETS_L01.md).

R9 selects `lwip2-536-feat` with matching feature1/IPv4/MSS536 flags for all
core/app code. Diagnostics use the same structure configuration as the library.
The change adds10224bytes to the image and68bytes static RAM (+72 occupied
including alignment). Feature/AutoIP/DHCP runtime allocations need new board
measurements; native fixtures cannot measure those. A169.254/16 AutoIP fallback
does not count as LAN readiness or save a pending profile.

R7 diagnostics use a bounded local snapshot/format buffer on the continuation
stack. Association metadata adds16bytes of static BSS with alignment. Reads
finish before output yields; no growable buffers or capture queue. R7 records
BSSID/channel from the association event and reads a temporary SDK config for
SSID, avoiding the stock BSSID getter's static config structure. No transport
buffer/engine budget/settings layout change. Measure physical stack/heap with
R8; native diagnostics cannot establish its runtime margin. R8's guarded real
client start uses the stack's normal runtime DHCP allocations and reports
allocation/start errors. It adds no separate reserved DHCP arena.

- The inherited continuation object includes a dedicated **16384-byte stack**.
  Core and app use the same stack-size definition; early heap startup remains
  in IRAM. CRC/load-segment/source-freshness checks remain in build/release.
- Engine allocation is capped at **16384 payload bytes**, excluding metadata.
  It is a cap rather than a reservation or promise that this much SDK heap is
  available. Values, session, functions, stored blocks and solver temporaries
  share that budget. Limits remain 16 variables / 64 elements provisionally.
- Console input: **192 bytes**, 191 printable characters. L01 has a separate
  one-line command queue of **192 bytes**; it rejects multiple outstanding
  commands. Serial typeahead during evaluation is discarded except cancellation.
  Guided serial Wi-Fi entry retains a separate 33-byte SSID buffer until the
  password is submitted. Password and console buffers are cleared on completion
  or cancel. CSI/SS3 decoding uses fixed state and a 75 ms Esc timeout.
- HTTP request/reply scratch: **1536 bytes**, 1535 usable plus terminator.
  The buffer is reused for headers. Page data remains in flash and streams
  through a 128-byte stack buffer; no framebuffer or RAM copy of the page.
- Output ring: **1024 bytes**, 1023 usable. Full output or a partial transport
  write disconnects/cancels rather than growing memory. Frames contain up to
  127 output bytes plus a type byte, coalescing small writes over roughly 8 ms.
- Frame parser: 193-byte payload storage, 4-byte mask and four unsigned fields;
  fixed 192-byte inbound-message maximum, including its type byte. SHA-1
  handshake scratch is on the continuation stack. No external WebSocket library.
- Access key is 32 hex digits plus NUL, restored from saved settings at boot
  or generated if unconfigured. A bounded pending-settings object stages a
  manual join until verified. SDK persistence is off; the app owns two raw
  4 KiB flash sectors at physical `0x3fa000`/`0x3fb000`. The empty filesystem
  boundary moves to mapped `0x405fa000`; EEPROM stays at `0x405fb000` and
  RF/SDK sectors remain from physical `0x3fc000`. There is no mounted filesystem.
  [Record format and flash checks](SETTINGS_L01.md).
  L01 retains four small core event-handler objects for association, IP,
  disconnect and DHCP timeout. Their runtime allocations are additional to
  static RAM; the callbacks only update flags/counters.
- R5 links the pinned core's mDNS responder for `mathsmith.local`. Its UDP,
  service and hostname allocations consume runtime heap in addition to the
  static increase. R4's free-heap reading is not an R5 budget measurement.
- Wi-Fi/TCP objects, SDK receive/send buffers, Serial buffers and temporary IP
  strings allocate additional runtime heap. Listen backlog is requested as 1;
  that does not eliminate SDK per-connection allocations.
- Browser page history (100 commands) and scrollback (65536 string characters)
  belong to the browser computer, not the ESP8266 heap. Display fonts, SD,
  OLED/PS2 buffers, AI responses and external-app arenas are absent.

## Runtime measurements required

### R4 browser-workload report

After reporting browser access at192.168.10.157, the user provides these
actual **L01-r4** measurements:

| Metric | Bytes / value | Approximate KiB |
|---|---:|---:|
| Free heap | 18696 | 18.26 |
| Largest free block | 13824 | 13.50 |
| Heap fragmentation | 23% | — |
| Minimum free continuation stack | 11708 / 16384 | 11.43 / 16 |
| Engine live / peak payload | 4984 / 5329 | 4.87 / 5.20 |
| Engine payload cap | 16384 | 16 |

Exact report (local hardware log omitted; see the validation summary).
This confirms current firmware identity and connected-workload headroom.
It does not establish maximum-matrix fit or long-run stability. Engine payload
already consumes SDK heap; do not add it to free heap as another pool. The
payload cap has11400bytes left at this snapshot, subject also to actual heap,
contiguous blocks, metadata and competing network allocations. The largest
block, rather than total free heap, limits an individual contiguous allocation.

Fragmentation has increased relative to the earlier stalled-r3 snapshot, but
the versions, workspace and connection states differ. This is not a controlled
leak comparison. Compare repeated workloads after warming engine caches with
the browser connected; look for a continuing decline rather than treating one
fragmentation percentage as failure. Stop and larger workloads remain
to be reported. Browser disconnect/reconnect with variables retained is now
user-confirmed. The user also explicitly confirms browser arithmetic
(`2+2` gives4) and the 2x2 linear solve (column[2;3]) matched the expected
results. Those two numerical cases now have hardware confirmation.

### R5 association-without-IP report

After98seconds: associated=1/events=1, no IP/GotIP event, saved=no/pending=yes.
Free heap19688, largest19168, fragmentation3%, continuation minimum-free15064
of16384; engine live/peak4832/4832 of16384. No obvious exhaustion in this idle
workload. mDNS and browser were not active, so this is not a browser measurement
or evidence of the exact address-assignment failure. Exact report (local hardware log omitted; see the validation summary).
R6 adds a bounded DHCP recovery path and diagnostics; measure on hardware again.

### Earlier R3 stalled-join report

The user reports these physical **R3** readings during the stalled join
(mode1, SDK1, no confirmed IP/browser): free heap21288, largest block20768,
fragmentation3%, continuation stack minimum free15096/16384, engine live/peak
4832/4832 out of16384. Recorded transcript (local hardware log omitted; see the validation summary).
They show no obvious exhaustion in that idle workload; they are not R4/browser
measurements or a guarantee about all SDK/engine allocations. The reported
continuation-stack margin does not measure the separate SDK system stack.

L00's reported 27888 free heap after a 4×4 eigensolver run was **Wi-Fi off**.
Do not subtract the static difference and call that a measured L01 budget:
SDK Wi-Fi/TCP allocations and fragmentation must be measured on the board.

Record `:mem` after serial startup, station association, browser upgrade,
representative matrix work, cancellation and warmed reconnect cycles. Report
free/largest heap block, fragmentation, engine live/peak payload and minimum
free stack. Check radio drop/recovery, watchdog stability, Stop latency and pin
read/write timing on actual hardware. If margins are inadequate, revise the
engine cap/limits or buffer sizes based on these results.

Host tests can validate protocol bounds, cancellations, stalled transport,
ownership, mock GPIO policy and linked capacity; they cannot prove this combined
firmware's radio/heap/stack/electrical behaviour. See the
[numbered hardware guide](../../apps/esp8266/l01_webrepl/README.md).

## R9 home join snapshot — 16 November 2026

User-reported after47seconds without an IP; no browser attached:

| Measurement | Bytes / result |
|---|---:|
| Free heap | 18736 |
| Largest block | 18216 |
| Fragmentation | 3% |
| Dedicated stack allocated / minimum free | 16384 / 11868 |
| Engine payload used / peak / limit | 4832 / 5043 / 16384 |

DHCP SELECTING/tries5/PCB1, offer/server0.0.0.0. No obvious exhaustion in this
workload; this does not establish the reason replies are absent or a margin for
future peripherals. This is R9, not R10 or browser-connected memory data.
Exact report (local hardware log omitted; see the validation summary).

## R10 home join snapshot — 16 November 2026

User-reported after111seconds without an IP; no browser attached:

| Measurement | Bytes / result |
|---|---:|
| Free heap / largest block | 18192 / 17672 |
| Fragmentation | 3% |
| Dedicated stack allocated / minimum free | 16384 / 14888 |
| Engine payload used / peak / limit | 4832 / 4832 / 16384 |

SELECTING/tries6/PCB1; TX6 Discover accepted by driver, classified RX0,
observer malformed6. Rejection direction/reason unmeasured in R10. No obvious
exhaustion in this workload; not R11 or browser/maximum-matrix data.
Exact report (local hardware log omitted; see the validation summary).

## R11 short home reply / R12 comparison

R11's report contains only diagnostics, no fresh :mem reading. The last rejected
RX has frame174/IP328/UDP308, matching xid and offered192.168.10.157. Allocation
succeeded; no actual checksum/type result is available. Target glue allocates
and copies the same174bytes to lwIP. R12 requests pre-address broadcast replies
as a delivery-path comparison; it neither accepts short packets nor reserves
memory for missing bytes. R12 home Wi-Fi/browser access now has user PASS (17 November); fresh R12
runtime margin and the origin of short replies remain unmeasured.

## R13 browser editor sum snapshot — 17 November2026

User runs saved/reopened example.ms with s=0, for k=1:8, s=s+k, end, s and
obtains36, then :mem. The later user clarification identifies this same run as
the successful Save/Open/Run test, not a second memory measurement:

| Measurement | Bytes / result |
|---|---:|
| Free heap | 15256 |
| Largest block | 14376 |
| Fragmentation | 6% |
| Dedicated stack allocated / minimum free | 16384 / 12288 |
| Engine payload used / peak / limit | 4904 / 5292 / 16384 |

The stack watermark corresponds to4096bytes used,12288bytes still free at its
lowest observed point. No obvious exhaustion in this connected small-program
workload; no maximum matrix/stress/repeated-cycle/peripheral fit guarantee.
Engine payload is already included in heap accounting. No pre-run snapshot
was supplied, so a leak/allocation delta cannot be calculated from this alone.
Exact report (local hardware log omitted; see the validation summary).

## R14 post-blink snapshot — 17 November2026

User reports4visible flashes/final read1 after the requested5blink test. Count
mismatch remains open; console :mem stays usable:

| Measurement | Bytes / result |
|---|---:|
| Free heap | 15128 |
| Largest block | 14312 |
| Fragmentation | 6% |
| Dedicated stack allocated / minimum free | 16384 / 10808 |
| Engine payload used / peak / limit | 4928 / 5379 / 16384 |

Stack watermark is5576bytes used; no obvious exhaustion in this workload.
This is not a controlled R13/R14 allocation comparison or sustained memory/
peripheral-fit result. No leak conclusion from one reading. The earlier R13
sample used a different script. Exact report (local hardware log omitted; see the validation summary).

R14 numbered follow-up: user confirms five visible flashes, counts through5
and LED off with the slower blink_count.ms script on unchanged firmware.
The blink-count check passes; no new :mem was supplied, so the table above
remains the original post-blink snapshot rather than a measurement of this run.
