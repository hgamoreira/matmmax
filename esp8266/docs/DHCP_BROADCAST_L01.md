# R12 — request broadcast DHCP replies before acquiring an IP

**Hardware result — 17 November 2026: PASS for home-network connection and
browser access without a serial USB connection to this computer.** The user
reports it is working and subsequently confirms browser reconnection after a
command-line software reboot and after the requested complete power-loss/
USB-charger startup. One warm and one cold restart/browser cycle now PASS.
Keep R12 as the working baseline. The earlier short
reply's origin remains unknown. This report does not separately confirm R12
repeated-cycle stability, exact key retention, calculation/memory results or mDNS.
Exact user result (local hardware log omitted; see the validation summary).


17 November 2026. The R11 home report (received16 November) identifies actual incoming BOOTP replies:
source192.168.10.1, offered192.168.10.157, matching xid25184131, opcode2.
Four driver-accepted Discover frames and four rejected RX candidates. The last
has only174 Ethernet bytes available for an IP total length328/UDP308;342
Ethernet bytes would be needed. Allocation succeeded. Cookie/type/checksum are
unreached, not evidence of a zero cookie or an identified DHCP message type.
Exact hardware report (local hardware log omitted; see the validation summary).

## What is established

The pinned glue's `ethernet_input` allocates and copies `p->len` bytes and passes
that result to lwIP. Target disassembly independently confirms the same length
at each operation. Thus the short reply is not just rejected by our observer;
those same short bytes enter the regular IP stack. Where the missing bytes
were lost remains unknown. This does not prove the router transmitted a short
packet, an SDK bug, or a pbuf-chain problem. No over-the-air/router capture or
SDK chain metadata has established the upstream cause. The original R4 success
and later exact-R4 failure remain part of the record.

## Targeted comparison

R11's outgoing Discover has BOOTP broadcast flag0. RFC2131 section4.1 allows a
client unable to receive pre-address unicasts to request broadcast replies via
the BROADCAST flag. R12 sets this flag on station Discover and Request messages
only while `ciaddr` is zero. The user now confirms home Wi-Fi/browser operation with this delivery-path
workaround; it does not establish where the previous replies became short.
[DHCP reply delivery and broadcast flag](https://www.rfc-editor.org/rfc/rfc2131.html#section-4.1).

The wrapper runs at the existing lwIP options-construction hook, before UDP/IP
checksums. It calls the original hook, preserving client-ID; the caller still
adds the hostname. It changes only the high bit of the network-order BOOTP
flags byte. Renewals/rebinding with nonzero `ciaddr`, other DHCP types and
non-station interfaces are unchanged. The read-only packet observer never
modifies packets. Receive validation, radio settings, R8 recovery, engine,
stored network/key and fresh usable-IP requirement are unchanged. No guessed
static address or acceptance of a truncated reply.

## Hardware test — user PASS/FAIL

1. Close the monitor with Ctrl+], then run:

   ```sh
   esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
   esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
   ```

   PASS: `:info` shows L01-r12 and the prompt works. Saved settings/key remain
   compatible; keep the working hotspot fallback until a home save succeeds.
2. Use serial `:wifi off`, then guided `:wifi join` for EXAMPLE_HOME_WIFI. Keep router
   settings, board location and power unchanged. Wait30seconds, then `:wifi`
   and `:wifi diag`.
   PASS for the comparison: `Last DHCP TX` shows `broadcast=1`. If no IP, send
   all packet/rejection lines and `:mem`, omitting status URL/key lines. The
   existing guide explains [packet boundaries](DHCP_PACKETS_L01.md).
3. PASS for home connection: status3/SDK5, usable home IP and settings-saved
   message; browser opens and `2+2` returns4. A changed broadcast flag alone
   is not a connection PASS. If packets remain short, record FAIL and use the
   new lengths/reasons; further waiting alone may not help.
4. After a successful home save, reboot/cold-power from a USB charger. PASS:
   no credential entry, same browser key and browser2+2. Do not mark these
   steps passed until the user explicitly reports them.

## Implementation and validation

`dhcp_broadcast.c` holds the bounded BOOTP flag policy. `dhcp_broadcast.cpp`
wraps `lwip_hook_dhcp_amend_options` for station st0 only. L01 alone links with
`--wrap`; bundled vendor archives are not modified. Unit tests cover only-bit
change, initial Discover/Request, idempotence, renewal/other types/replies/short
buffers, and independently reconstructed valid UDP checksums. A reported174/
328/308 reply fixture stays rejected without reading unavailable bytes. Target
relocations/disassembly verify the stock hook is retained and the wrapper
executes before UDP transmission. Native fixtures do not emulate RF/DHCP.

Verified R11 bin/ELF/map/config/source/build script are retained at
`build/esp8266/history/l01-r11-before-broadcast-replies/`; the passing R6
hotspot recovery build is also retained. The working R12 bin/ELF/map/config/
source/build script and hardware report are preserved at
`build/esp8266/history/l01-r12-home-browser-pass/`. Full L01 validation remains open.
Pinned receive source: `tools/sdk/lwip2/builder/glue-esp/lwip-esp.c`;
construction hook: `tools/sdk/lwip2/builder/glue-lwip/lwip-git.c` in core3.1.2.
