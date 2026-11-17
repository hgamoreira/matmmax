# DHCP observer and R11 rejection details

**Current candidate R12:** [broadcast reply comparison](DHCP_BROADCAST_L01.md).
R11's actual rejected RX sample has only174 Ethernet bytes for declared328 IP
bytes, matching xid and offered192.168.10.157. The observer stays read-only;
R12 changes the pre-address broadcast-request flag at packet construction.
The earlier R11 procedure below describes the diagnostic implementation.

16 November 2026. R9's home client remains SELECTING with five tries and no
accepted offer/IP through47seconds. Free heap18736/largest18216 and minimum-free
continuation stack11868/16384 show no obvious exhaustion in that workload.
Exact R9 report (local hardware log omitted; see the validation summary).
No further compatibility/static-IP/router change follows just from this state.

**R10 hardware result:** still no home IP at111seconds. Six Discover frames
accepted by the driver, no classified RX, **six observer-parser rejections**.
R10 did not retain their direction/reason. This does not establish absence of
replies, invalid router data, or lwIP's rejection reason. Free heap18192,
largest17672 and stack minimum-free14888/16384 show no obvious exhaustion.
Exact R10 report (local hardware log omitted; see the validation summary).

**R11** adds rejected TX/RX counts and the last rejection's direction, boundary
success, reason, lengths and partial BOOTP/option metadata. The observer's
`malformed` label becomes `parser-rejected` to avoid claiming a network defect.
No DHCP/network/settings behavior change is made before those facts arrive.

R10 uses the pinned core's existing **phy_capture** hook (the same hook used by
its Netdump library) to observe this board's DHCP frames in both directions.
It retains fixed-size counters and metadata, not packet bodies, passwords or
browser keys. The firmware does not need privileged laptop capture.

## Hardware test

Close the monitor with Ctrl+], then:

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. Confirm **L01-r11** using `:info`. Saved settings/key are compatible.
   If the saved hotspot is enabled and auto-joins, a `:wifi diag` snapshot there
   provides a useful known-working packet baseline before the home test.
2. Serial `:wifi off`, then guided `:wifi join` with home credentials. After30s
   enter `:wifi` and `:wifi diag`; send the whole diagnostic including the new
   DHCP packet lines, omitting Browser/Named URL/key lines from status.
3. After20seconds more without restarting, run `:wifi diag` again to compare
   counters/stage. `:mem` measures the new observer's actual board margin.
4. If home IP/saved message appears, test browser2+2 and home reboot without
   credential entry/same key. Otherwise select the next correction from the
   actual transmit/receive evidence below; do not claim a physical fix yet.

## Interpretation

| Output | Meaning and next evidence |
|---|---|
| TX discover rises, driver-failed0 | The SDK driver accepted those frames; this does not prove router receipt. |
| TX driver-failed rises | Transmission is failing at the driver boundary; inspect board/driver resources. |
| SELECTING retries rise but TX stays0 | No matching transmit observed at that boundary. Confirm the hook on the working hotspot, then inspect earlier transmit path. |
| TX rises, RX offer0, rejected RX0 | No classified offer or rejected candidate observed; earlier filtering/server delivery remain possibilities. Not proof the router sent none. |
| Rejected RX rises | Own-MAC DHCP-port candidates reached the hook but failed observer parsing. Inspect the last reason/header before judging replies or the router. |
| RX offer rises but client stays SELECTING | A candidate offer arrived; compare checksum, transaction ID, fields and eventual acceptance. |
| alloc-failed rises | SDK received a matching packet but allocation for lwIP failed. |
| wrong-xid rises | Received reply transaction differs from the last observed transmit; stale replies may be involved. |
| Bad checksum/parser-rejected rises | Observer checks failed; inspect reason and direction. This does not establish lwIP rejection or a router fault. |

Types are1 Discover,2 Offer,3 Request,4 Decline,5 ACK,6 NAK,7 Release,8 Inform.
Last TX shows type/xid/broadcast flag and whether Ethernet-MAC client identifier
and hostname options are present. Last RX shows type/xid/source/offered/server
and checksum result. Options content/hostname text and complete payloads are not
stored. Counters reset for each explicit/boot join, before STA is enabled.
`:wifi diag` only reads/prints a snapshot; it does not reset counters or rejoin.

## Rejection details (R11)

`DHCP parser rejected TX=... RX=...` splits the previous aggregate. Last rejected
metadata is retained even if parsing ends before DHCP type can be determined.
`boundary-success` means driver acceptance on TX or available lwIP allocation
on RX. It does not mean parser/DHCP acceptance. Header fields not reached yet
are zero; `checksum=unchecked` is distinct from `bad` after a length failure.

| Reason | Observer check that failed |
|---|---|
| ip-length | IP total length is inconsistent with captured bytes/header. |
| udp-length | UDP length is too short for the inspected DHCP fields or exceeds IP payload. |
| bootp-opcode | BOOTP opcode does not match the UDP direction. |
| cookie | DHCP magic cookie does not match. |
| option-length-missing / option-truncated | Option header/value runs past the declared UDP payload. |
| message-type-length / duplicate / value / missing | This observer cannot classify DHCP message-type option53. |

The option line identifies the offset from the DHCP options start, code,
declared length and remaining bytes. Header details include lengths, opcode,
xid, cookie, type if reached, source/offered IP and checksum result. They do not
retain arbitrary option contents. A parser limitation or representation issue
may cause a rejection; these details guide correction rather than bypassing
lwIP validation. A last sample does not describe every rejected frame.

## Capture boundary and limits

Pinned `glue-esp/lwip-esp.c` invokes this hook after the SDK linkoutput call for
TX, with success meaning ERR_OK, and on RX before copying into allocated lwIP
storage, with success meaning the allocation exists. RX can be observed before
lwIP accepts/rejects the DHCP packet. This is not an over-the-air capture or
evidence of router receipt. It cannot see frames discarded earlier by radio/
decryption/driver code.

The bounded C decoder handles Ethernet, up to two VLAN tags, IPv4 options,
UDP/BOOTP magic/client MAC and basic DHCP options, IPv4/optional UDP checksums.
It skips IPv6 and fragmented IPv4; it does not reassemble or decode DHCP option
overload fields. Other-client/control/browser traffic is ignored. Thus counters
are observations of this filter, not a complete network capture. No callback
allocates, prints, yields, retains buffers or changes packets/return values.
R9 network variant, R8 recovery, saved record/key and engine remain unchanged.

## Validation

Tests exercise actual C parsing/counters with independently constructed DHCP
wire frames: request/reply types, VLAN/IP options/checksums, all truncation
lengths, malformed options, wrong client/interface/transaction, driver/allocation
errors, reset and input immutability/metadata-only retention. Address/undefined
sanitizers check mutation/truncation cases. Target relocation verifies wiring
the existing bidirectional hook; host fixtures still cannot prove real RF.

R9 bin/ELF/map/config/source are preserved at
`build/esp8266/history/l01-r9-before-dhcp-packets/`, with passing R6 recovery also
retained. Verified R10 bin/ELF/map/config/source are also preserved at
`build/esp8266/history/l01-r10-before-reject-details/`. R11 physical rejection
details identify short RX replies; see the current R12 guide above. A home fix
and fresh R12 memory readings remain pending.
Sources under pinned core3.1.2: `tools/sdk/lwip2/include/gluedebug.h`,
`tools/sdk/lwip2/builder/glue-esp/lwip-esp.c`, and `libraries/Netdump/src/Netdump.cpp`.
[Pinned hook implementation](https://github.com/esp8266/Arduino/blob/3.1.2/tools/sdk/lwip2/builder/glue-esp/lwip-esp.c).
