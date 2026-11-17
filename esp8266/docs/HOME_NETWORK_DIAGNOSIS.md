# Home-network connection investigation

**Current outcome — 17 November 2026: R12 home Wi-Fi/browser PASS.** The user
confirms working browser access without a serial USB connection to this PC.
A subsequent report also confirms browser reconnection after a command-line
software reboot, followed by a successful complete power-loss/USB-charger
startup/browser reconnection. One warm and one cold restart cycle now PASS.
The broadcast-reply policy resolves the practical home-connection problem in
this test. Retain R12 as the working baseline; where the earlier reply became
short is still unproven. The remaining sections retain the investigation history.
Exact report (local hardware log omitted; see the validation summary).


16 November 2026. Current firmware R6 works on a2.4GHz hotspot, including saved
settings, reboot with retained key, cold startup on USB charger and browser2+2.
The user requests fixing the home-network connection. Keep this passing build
as the baseline; no additional firmware or router change is established yet.

**Current action after the user's instruction to continue:** R7 adds read-only
`:wifi diag` to obtain station/AP/link and DHCP-client state directly, since a
privileged laptop capture was unavailable. It keeps R6 connection/settings
policies and the passing R6 recovery image. See the
[R7 board procedure](NETWORK_DIAGNOSTICS_L01.md).

**R7 evidence received:** station MAC matches, home AP on channel11/RSSI-52dBm,
station/link up, but actual lwIP DHCP OFF/tries0/PCB0 and no IP/offer/server.
The SDK enabled flag did not establish client operation. R8 replaces SDK
stop/config recovery with one guarded real-client start; see the
[R8 procedure](DHCP_RECOVERY_L01.md). A physical home fix remains unconfirmed.

**R8 result:** actual client now stays SELECTING/tries6/PCB1, no accepted offer
or home address through92seconds. The current failure is no longer OFF client.
Auditing the custom build finds feature0/no-features lwIP, which omits the
core's DHCP compatibility hook. R9 compares the bundled standard low-memory
feature1 variant, with matching headers/library, client identifier and hostname
in DISCOVER. [R9 procedure](DHCP_COMPATIBILITY_L01.md). This is supported by
pinned core source and upstream router-compatibility reports, not a confirmed
Altibox cause or a current packet trace.

**R9 result:** SELECTING/tries5/PCB1, no accepted offer/IP at47seconds.
Free heap18736/largest18216, stack minimum-free11868/16384 show no obvious
exhaustion. **Current R10 step:** observe fixed-size DHCP packet metadata using
the pinned core's existing bidirectional hook. No further compatibility guess,
router change or static address is justified by the client state alone.
[Packet procedure and capture limits](DHCP_PACKETS_L01.md).

**R10 result and current R11 step:** home still has no IP at111seconds,
SELECTING/tries6/PCB1. TX6 Discover/driver-failed0, classified RX0, **malformed6**.
The aggregate does not reveal direction/reason, so RX0 cannot prove the router
sent no reply. R11 adds rejection direction, exact reason and bounded header/
option metadata, labeling these as observer verdicts. It changes no network
policy. R10 free heap18192/largest17672/stack minimum-free14888/16384 show no
obvious exhaustion. Exact report (local hardware log omitted; see the validation summary).
R11 hardware rejection details and a home fix remain pending.

**R11 result / current R12 comparison:** rejected RX4, TX Discover4, matching
xid25184131. Last BOOTP reply source192.168.10.1, offered192.168.10.157,
opcode2, only174 Ethernet bytes versus declared328 IP/308 UDP bytes. The stock
glue passes those same short bytes to lwIP, confirmed in target disassembly.
Cookie/type/checksum are unreached; do not infer a zero cookie or classify a
complete DHCP Offer. The router does reply. Where bytes were lost, including
possible SDK representation, is unproven. No new memory report.
Exact report (local hardware log omitted; see the validation summary).
R12 requests broadcast replies only on pre-address Discover/Request, preserving
normal validation. [R12 procedure](DHCP_BROADCAST_L01.md). Home result pending.

## Evidence

| Test | Observed result |
|---|---|
| Earlier R4 at home | Browser access/calculations/reconnect passed at192.168.10.157. |
| Current R4 at home | One association, no IP/GotIP through41s; no obvious idle exhaustion. |
| R5 at home | One association, no IP/GotIP through98s; no save. |
| R6 at home | One association, no IP/GotIP through52s despite one DHCP restart. |
| R4 on hotspot | SDK5/GotIP/ready1 at172.20.10.2. |
| R6 on hotspot | Join/save by9s, software-reboot auto-join by6s, same key. |
| R6 independent power | User confirms requested charger/cold-start/browser test works. |

This isolates a network-dependent difference. It does not prove a DHCP-server,
security, access-control, radio or core cause. An association event does not
alone prove completion of every later authentication/address-setup step.
SDK DHCP enabled state is not a capture of packets sent/received. Client mDNS
resolution was not identified in the standalone report. Current home failures
also occur in the archived R4 image without persistence/mDNS additions.

## Next evidence: router model and device/lease entry

The user reports the app lists **esp-40573A** as connected. The suffix matches
the recorded MAC's final bytes40:57:3a; confirm the full MAC in device details.
An app entry alone does not demonstrate a current DHCP lease. IP, paused/blocked
status, model and whether the entry is current are still requested. No firmware
or router change follows just from this displayed name.

Ask which router/access point supplies EXAMPLE_HOME_WIFI, including any separate
mesh/repeater. While the board attempts that network, inspect the router's
connected devices and DHCP list for station MAC **02:00:00:40:57:3a** (from the
user's original chip report). Record whether it is present, blocked/paused,
associated only or assigned an IP, and whether any shown lease is current.
Do not request Wi-Fi passwords or browser access keys.

Use the existing R6 serial monitor. If a browser owns the console, Ctrl+C
reclaims serial. Run `:wifi off`, then guided `:wifi join` for the home SSID.
After30seconds capture full `:wifi` and `:mem` alongside the router observation.
A failed home join leaves the saved working hotspot settings intact. A
successful verified home join replaces them as intended. No forget/reflash
is needed for this observation.

Altibox's primary documentation provides
[Wifi/home-network settings](https://kundeservice.altibox.no/hc/no/articles/11723628975633-Innstillinger-for-Wifi-og-hjemmenettverk):
Mine sider has Wifi settings and network settings, with DHCP range/lease time
in the latter. Its
[equipment catalogue](https://kundeservice.altibox.no/hc/no/articles/11740366686609-Hjemmesentraler-og-wifi-utstyr)
links model-specific guides for VMG/FMG/FT1/AX7501 and older equipment. Exact
device-list controls depend on the user's model/app; do not invent menu paths.
No bridge-mode/security/band/channel/DHCP-pool change has been requested yet.

## App address differs from board state: capture the exchange

Latest app detail shows192.168.10.157, matching the earlier working home address.
The contemporaneous R6 report still has IP unset/GotIP0/ready0 after362seconds,
DHCP enabled and recovery already attempted. Saved=yes is the prior hotspot
record; home join/save remains pending. Do not assign or claim that app address
is active on the board from the app entry alone. The local PC uses wlp2s0 with
source192.168.10.143; ping/HTTP fail and address resolution to.157 is FAILED.
No router model or DHCP trace is yet available. Record (local hardware log omitted; see the validation summary).

Assistant capture is unavailable: raw packet permission is absent and sudo
requires local interactive authentication. The user can run this in a second
local terminal while leaving the serial monitor open:

```sh
sudo timeout --signal=INT 60 tcpdump -p -ni wlp2s0 -e -vvv -s 0 \
  'udp and (port 67 or port 68) and udp[36:4] = 0x345f4540 and udp[40:2] = 0x573a'
```

Enter any sudo password locally, never into chat. The filter checks the DHCP
client hardware-address field, selecting this board's requests and replies
including broadcast offers. It excludes other clients and WebSocket/browser
keys; DHCP has no Wi-Fi password. A four-frame offline pcap test checked board
request/reply selection and exclusion of another MAC's request/reply.

Once tcpdump says `listening on wlp2s0`, use the serial terminal:

```text
:wifi off
:wifi join
```

Enter home-network credentials at the prompts. After the capture ends in60s,
provide the filtered output and full `:wifi` without URL/key lines. Timeout
ending the command is expected. Look for Discover, Offer, Request and ACK/NAK;
record which are actually visible rather than inferring unseen exchanges.

Normal laptop Wi-Fi capture may miss AP-to-board unicast packets or client
broadcasts not forwarded by the AP. An empty/incomplete trace is inconclusive,
not proof the board/router sent nothing. If so, use router-side capture/logs or
read-only board interface/DHCP state diagnostics. No packet capture has yet been
received; firmware stays the known working R6 hotspot baseline.

## Choose a correction from the result

The user offers deleting the esp-40573A app entry. If its confirmation specifies
device-only remove/forget, turn board Wi-Fi off, remove that entry, then retry
guided home join and compare status. This may only clear device history; it is
not verified to clear DHCP leases. No result is reported yet. Altibox documents
[Block device](https://kundeservice.altibox.no/hc/no/articles/41861818669329-Blokkering-av-enheter-i-Altibox-Hjem)
as a separate action which keeps Wi-Fi connected but stops Internet access;
do not substitute blocking for removal. Board :wifi forget is not needed.

- A current lease/offer on the router with no device address points toward
  tracing address replies and the board's client state; it is not sufficient
  to assign that address statically without confirming ownership/configuration.
- A device explicitly paused/blocked calls for inspecting that specific rule.
- No router-visible association/lease needs AP/mesh identity, signal/security
  and then address-exchange evidence, rather than another guessed DHCP restart.
- If firmware diagnostics are needed, add read-only station MAC/AP identity,
  channel/RSSI and pinned lwIP interface/DHCP state/retry counters, preserving
  saved settings. Those counters show client state, not packets on the wire.

Router model/full app MAC/lease freshness and the DHCP trace remain pending.
Current home retry confirms no IP through362seconds despite the app entry.
The home fix is not complete; full L01 hardware validation has outstanding items.
