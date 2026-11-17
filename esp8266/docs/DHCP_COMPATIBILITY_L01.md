# R9 standard low-memory DHCP compatibility build

16 November 2026. R8's home client is running/SELECTING, tries6/PCB1, but no
IP or accepted offer/server is recorded through92seconds. Its policy correctly
leaves that active negotiation alone. Hardware report (local hardware log omitted; see the validation summary).
The previous stopped-client symptom is not the current failure stage. A packet
trace has not established where requests/replies are lost or rejected.

## Concrete build difference

The custom L01 build through R8 selected bundled `liblwip2-536.a` with
`LWIP_FEATURES=0`: the core's lower-memory **no-features** variant. Pinned
`lwipopts.h` guards its DHCP compatibility append hook with `LWIP_FEATURES`.
That hook adds Ethernet client identifier option61, and includes the hostname
in DISCOVER as well as the later request. Upstream issues document router
compatibility involving those options; they are not proof of this Altibox
router's cause. These options are not universally required for a valid lease.

R9 selects the pinned core's standard **v2 Lower Memory** IPv4 variant:
`liblwip2-536-feat.a`, `TCP_MSS=536`, `LWIP_FEATURES=1`, `LWIP_IPV6=0`.
All application/core headers are rebuilt with the matching flags so lwIP
structures match the selected library. No downloaded/new core/SDK version or
handwritten DHCP replacement. L00 and the Wi-Fi-only probe retain feature0;
their rebuilt binaries remain byte-identical to recorded baselines.

R8's guarded stopped-client recovery, R7 read-only diagnostics, existing
authentication, engine and saved-record/key policy remain. The feature variant
can fall back to link-local AutoIP; R9 does not count169.254/16 as LAN readiness
or save a pending network based on that fallback. DHCP may continue toward an
ordinary LAN address. No static address/router settings are changed.

## Hardware comparison

R8 bin/ELF/map/config/source/build script are preserved at
`build/esp8266/history/l01-r8-before-dhcp-options/`. R6 working hotspot recovery
also remains available. R9 uses the same saved-record format/sectors/key.
Close monitor with Ctrl+], then from the repository root:

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. Confirm L01-r9 using `:info`. Keep router settings and board location/power
   as before to compare the network variant.
2. Serial `:wifi off`, then guided `:wifi join` with home credentials. Wait30s.
3. Record full `:wifi`, `:wifi diag` and `:mem`, omitting Browser/Named URL lines
   from shared reports. No network password/browser key is needed in chat.
   If no IP, repeat only diag after20s to compare stage/tries/offer.
4. Connection PASS requires usable IP/ready1 and browser2+2=4. Save PASS requires
   saved message/pending=no for this home join. Then test `:reboot` without
   re-entering credentials and verify home auto-join with retained key.
5. If it still selects without an offer, obtain router/AP model and DHCP
   packet evidence rather than calling this a confirmed firmware/Altibox fix.

## Verification and sources

62 native/linked-target checks pass, including matching feature1 header/library
configuration, linked DHCP compatibility hook, and link-local non-readiness/
no-save. L00 regression19 and Wi-Fi-probe6 pass with identical binary hashes.
These do not demonstrate real DHCP packet contents or radio interoperability.
Physical R9 report: no home IP at47seconds, SELECTING/tries5/PCB1;
heap18736/largest18216, stack minimum-free11868/16384. This compatibility
comparison did not resolve that attempt. [R10](DHCP_PACKETS_L01.md) now adds
on-board packet observations; the home cause remains open.

Pinned sources: `boards.txt` (`generic.menu.ip.lm2f`/`lm2n`),
`tools/sdk/lwip2/include/lwipopts.h` (DHCP append hook) and
`tools/sdk/lwip2/builder/glue-lwip/lwip-git.c` (client identifier encoding).
[Core issue8223](https://github.com/esp8266/Arduino/issues/8223),
[core issue8247](https://github.com/esp8266/Arduino/issues/8247),
[pinned board variants](https://github.com/esp8266/Arduino/blob/3.1.2/boards.txt).
