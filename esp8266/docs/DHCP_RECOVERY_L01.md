# R8 recovery of a stopped lwIP DHCP client

**R8 result received:** client is now active SELECTING/tries6/PCB1; no accepted
offer/IP through92seconds. The guard correctly leaves it running. Next is the
[R9 network-variant comparison](DHCP_COMPATIBILITY_L01.md). The R8 record below
does not claim a home connection PASS.

16 November 2026. R7's actual home-network diagnostic shows station/link up,
RSSI-52dBm, SDK DHCP enabled, but real lwIP client OFF(0)/tries0/PCB0 with no
address, offer or server. Exact report (local hardware log omitted; see the validation summary).
This supplies a specific board-side recovery condition. It does not establish
why the client originally stopped, or prove the home router is working/faulty.

R8 replaces the earlier SDK stop/config restart. The pinned Arduino core's
`WiFi.config(0U,0U,0U)` sets DHCP mode, calls `wifi_station_dhcpc_start()` and
returns true without checking that call's result. SDK enabled status therefore
does not confirm a running lwIP client. The pinned glue itself calls the real
`dhcp_start` after preparing the interface and updates SDK IP state when lwIP
binds. R8 starts that real client only when the interface is already ready.

## Exact recovery policy

After15seconds associated without a usable IP, at most once per second inspect
the actual station interface, matched by station MAC. A recovery attempt requires
SDK DHCP enabled, interface up/link-up, no SDK or interface IPv4 address, and a
DHCP client absent or OFF with no allocated PCB. Start using `dhcp_start(n)`;
the pinned header maps it to **dhcp_start_LWIP2**, not the SDK lwIP1 wrapper.
Report the real result and before/after state/PCB/tries. A return of0 means
the start API accepted the operation, not that an IP has already arrived.
Negative values are real lwIP errors (for example-1 is ERR_MEM).

Attempt once per explicit join/boot, including when the attempt returns an error.
If a client is SELECTING, REQUESTING, BOUND or has an allocated PCB, leave it
alone and continue observing. The earlier unconditional stop/config recovery
is removed. No guessed/static address, forced link-up, readiness override,
router change or unverified settings save. SDK/lwIP IP propagation and the
usual fresh matching-SSID/IP events still govern browser readiness and saving.
The diagnostic command itself remains read-only.

## Hardware test

Passing R6 recovery remains at `build/esp8266/history/l01-r6-before-network-diag/`;
R7 exact binary/ELF/map/config/source is at
`build/esp8266/history/l01-r7-before-lwip-start/`. Record format/sectors/key stay
compatible. Close the monitor with Ctrl+], then:

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. Confirm **L01-r8** with `:info`. If hotspot is on, the saved record should
   still auto-join it. Serial Ctrl+C reclaims the console if a browser owns it.
2. `:wifi off`, then guided `:wifi join` with the home credentials. Keep this
   one attempt running; no app-entry deletion/router reset is required.
3. Allow30seconds. If actual DHCP was stopped, expect a message beginning
   `Stopped lwIP DHCP client; start once: result=...`. Capture it, `:wifi`,
   `:wifi diag` and `:mem`, omitting URL/key lines from shared output.
4. Connection PASS requires a usable IP/ready1 and browser2+2=4. Save PASS
   requires the explicit saved message/pending=no for the home join. Then
   `:reboot` with no credentials and verify home auto-join/same key. Keep keys
   private. If the fallback starts but still no IP, repeat only `:wifi diag`
   after20seconds to compare client progress; do not keep restarting the join.
5. Preserve the exact ELF for any crash. Report actual results separately;
   native/API-return success is not a hardware connection PASS.

## Validation and sources

Host fixtures reproduce SDK-enabled/lwIP-OFF plus successful/failed/no-address
start results, one-attempt bounds, initial grace and interface/link/SDK/active
client guards. Existing math/persistence/protocol/browser tests still run.
A target relocation check verifies the real LWIP2 start symbol and absence of
the old SDK stop wrapper. Fixtures do not exercise actual RF, DHCP packets or
the SDK/lwIP callbacks on hardware.

Primary pinned sources: `libraries/ESP8266WiFi/src/ESP8266WiFiSTA.cpp`,
`tools/sdk/lwip2/builder/glue-lwip/lwip-git.c`,
`tools/sdk/lwip2/include/arch/cc.h`, `lwip/dhcp.h` and `lwip/err.h` under core3.1.2.
[Core source](https://github.com/esp8266/Arduino/blob/3.1.2/libraries/ESP8266WiFi/src/ESP8266WiFiSTA.cpp),
[pinned glue](https://github.com/esp8266/Arduino/blob/3.1.2/tools/sdk/lwip2/builder/glue-lwip/lwip-git.c).
