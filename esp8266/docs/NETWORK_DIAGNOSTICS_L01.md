# R7 read-only station/DHCP diagnostics

R11 retains this diagnostic and adds direction/reason details for packets
rejected by the observer, following R10's aggregate malformed6.
Current procedure: [R12 broadcast reply comparison](DHCP_BROADCAST_L01.md);
[R11 packet interpretation](DHCP_PACKETS_L01.md).
The earlier R9 comparison procedure:
[standard low-memory DHCP compatibility](DHCP_COMPATIBILITY_L01.md).

**R8 update:** R7 hardware readings confirm an OFF client on an up home link
despite SDK DHCP enabled. R8 retains this read-only command and adds a guarded
start of the stopped client. Use the [R8 test](DHCP_RECOVERY_L01.md) for current
flash instructions; the R7 procedure below is retained as the diagnostic record.

16 November 2026. R6 is hardware-validated for saved-hotspot reboot, retained
key, cold USB-charger startup and browser math. Home joins still have no IP
after362seconds while Altibox shows esp-40573A at192.168.10.157. Laptop ARP,
ping and HTTP to that address fail. Local capture requires sudo authentication
unavailable to the assistant. R7 adds evidence from the board without needing
a privileged packet capture. It does not claim to fix the connection.

## Board procedure

The tested R6 image/ELF/map/config/source are retained at
`build/esp8266/history/l01-r6-before-network-diag/`. R7 uses the same saved-record
format and flash sectors; flashing the ordinary firmware image does not erase
them. No erase-all or `:wifi forget` is needed. Close the old monitor with Ctrl+].

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. Verify L01-r7 in the banner/`:info`. Saved-hotspot boot should still work if
   that hotspot is enabled; this is a useful baseline, not a home-network fix.
2. Serial `:wifi off`, then `:wifi join` with home SSID/password at the separate
   prompts. If a browser owns input, serial Ctrl+C reclaims it first.
3. After30seconds, enter `:wifi` and `:wifi diag`. After another20seconds without
   rejoining, enter `:wifi diag` again. Supply both diagnostics, omitting URL/key
   lines from `:wifi`. No passwords or browser keys are printed by `:wifi diag`.
4. If the home connection succeeds, verify browser2+2 and capture connected
   `:mem`; a successful verified home join intentionally replaces the hotspot.
   A failed attempt retains the previously saved hotspot for the next boot.

Report whether station MAC matches the Altibox entry, AP BSSID/channel/RSSI,
interface up/link-up, DHCP state/tries/timer and offered/server addresses. The
second reading shows whether the same client stage is progressing. Tries is
for the current DHCP stage, not a total of all packets or proof of transmission.

## What the output means

| Observation | What to investigate next |
|---|---|
| No station interface found | Confirm MAC and interface availability; do not infer a DHCP/router fault. |
| Interface/link-up0 despite an association event | Link/authentication/driver state before address exchange. |
| SELECTING(6), no offered IP | Client is selecting a lease; check offers/link path. It does not prove no offer was sent. |
| REQUESTING(1), offered IP present | Client has reached the request stage; inspect ACK/NAK delivery and acceptance. |
| BOUND(10), interface IP present but SDK IP unset | Compare SDK/lwIP address propagation; readiness is not overridden by this command. |
| DHCP client absent or OFF(0) | Client/interface lifecycle rather than merely the SDK enabled flag. |
| Tries/timer differ between snapshots | Client state/timers are changing; they are not packet-capture counters. |

DHCP server/offered fields are the stored client snapshot. They alone do not
prove a current active lease, successful packet delivery or address ownership.
Default names such as esp-40573A can persist in the router's device history.
The app entry must be compared with the actual board MAC/state.

## Implementation and validation

`wifi_diag.cpp` snapshots the station MAC, bounded SSID/hostname, channel/RSSI,
and the lwIP interface found by matching the station MAC (not a guessed index).
AP BSSID/channel come from the actual association event, not the SDK's configured
BSSID field. The walk is bounded; missing interface/client and unknown states
are reported. SSID/hostname control characters are replaced for terminal output.

Collection finishes before output can yield to networking. The snapshot reads
flags, IPv4/netmask/gateway, DHCP state/tries/PCB/timer/xid and offered/server IP.
It performs no DHCP start/stop, reconnect, readiness change, settings write or
engine evaluation. R6 join, bounded recovery, saved settings/key and browser
ownership policies remain intact. The regular outer loop may still perform
its already-existing15second recovery during a pending join.

The target uses pinned core3.1.2 headers and its matching lwip2-536/IPv4 library.
Definitions: `tools/sdk/lwip2/include/lwip/dhcp.h`, `lwip/prot/dhcp.h` and
`lwip/netif.h` under the pinned core. These are client-state observations, not
DHCP packet tracing. Native fixtures test formatting, missing/unknown state,
bounded/control-character SSID, operation before an address exists, workspace,
authentication privacy and no saved-record write. All56 automated checks and
the Chrome UI/saved-key restart test pass off-board. Actual R7 diagnostics and
home-network behaviour still require the user's report. No physical fix or
full L01 hardware PASS is claimed.
