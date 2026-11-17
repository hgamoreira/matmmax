# Compare the archived working R4 image

16 November 2026. R5 associated without IP through98seconds; R6 did the same
through52seconds after its one DHCP restart. Settings remain unsaved. These
reports do not establish a cause. R4 previously had user-confirmed browser
math and reconnect success; rerun that exact binary on the same network before
another speculative firmware change. Saved-network startup remains the goal.

**Comparison received:** R4 also reports association with no IP through41seconds,
GotIP0 and ready0. Heap free20752/largest20232, fragmentation3%, continuation
minimum-free15096/16384. Exact report (local hardware log omitted; see the validation summary).
The current failure is therefore also present in the older image; this does
not establish a router fault or rule out behaviour common to both versions.
R4 was kept installed for the following2.4GHz hotspot test.

**Hotspot result received:** R4 reports SDK5/status3, IP172.20.10.2, GotIP1 and
ready1. Association/IP/readiness PASS on the hotspot. Free heap20640/largest20232,
fragmentation2%, continuation minimum-free15096/16384. Status was read at67s;
actual address-acquisition latency is unknown. Browser is disconnected in this
snapshot, so no new browser calculation or persistence PASS is inferred.
Redacted report (local hardware log omitted; see the validation summary).
The network-dependent difference is established; the exact home-network cause
is not. The following R4 comparisons are retained as the test record.

## Next: saved-network startup on the working hotspot

**R6 join/save result received:** hotspot IP172.20.10.2, status3/SDK5/ready1,
saved=yes/pending=no and explicit saved message at9seconds. DHCP recovery was
not needed. Redacted report (local hardware log omitted; see the validation summary).
**Subsequent reboot PASS:** R6 loads saved settings, joins automatically by6s,
retains its key and still reports ready1 at13s. No credential entry or DHCP
recovery. Redacted reboot report (local hardware log omitted; see the validation summary).
Steps1–2 below now pass; continue with browser/cold power. No reflash/rejoin is
needed. The subsequent requested cold USB-charger/browser2+2 test also receives
user PASS: report (local hardware log omitted; see the validation summary).
Client mDNS resolution is unspecified. Next is the
[home-network investigation](HOME_NETWORK_DIAGNOSIS.md); no more hotspot setup
is required for this successful startup test.

Close the monitor with Ctrl+], then return to the existing R6 candidate:

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. Verify L01-r6 with `:info`, then guided `:wifi join` with the hotspot's
   credentials. Wait for both the explicit saved message and a browser URL.
   If absent after60seconds, record complete `:wifi`/`:mem`; no save is assumed.
2. After a successful save, issue `:reboot` on serial. Keep the hotspot enabled.
   Enter no credentials. PASS requires automatic connection, Saved network=yes,
   usable IP and the same access key. Math variables are expected to reset.
3. Connect the laptop to the hotspot too and open the newly printed numeric
   URL/key. Test `2+2`. Named-address discovery is a separate optional check.
4. Collect connected R6 `:mem`; later test USB charger power without the PC.

R6's home-network recovery failed; hotspot join/save and software-reboot
persistence and subsequent cold charger/browser use pass. Retain
the key privately; do not include it/passwords in reports. Saving the hotspot
does not fix home-network access. Later serial `:wifi join` can replace the
saved network after a successful connection.

## Next comparison: another 2.4GHz network

1. Enable the hotspot in2.4GHz/compatibility mode. Keep the board on its existing
   USB power and serial monitor. No new firmware is needed.
2. On serial enter `:wifi off`, then `:wifi join`. Enter the hotspot's actual
   SSID and password at the separate prompts. R4 keeps these only in RAM.
3. Wait30seconds, then capture the full `:wifi` output. The useful result is a
   usable IP/ready1 and printed URL, or the same no-address state. Do not share
   the password or URL access key.
4. For an optional browser check, connect the laptop to that hotspot too, open
   the newly printed numeric URL/key and try `2+2`. An IP assignment is the first
   diagnostic result; hotspot browser-client isolation is a separate question.

A working hotspot would show that this board/image can complete address setup
on another network. If it also fails, the Wi-Fi-only probe/common core/radio
path becomes the next comparison. Neither result alone identifies the exact
cause. Saved-network startup on the intended home network remains outstanding.

The archived R4 image is414640bytes, SHA256
`42d3600f8e243e53747c7fa3d9d2ef87c1c793356ce3229b96c4f09d9d48216c`.
It is at `build/esp8266/history/l01-r4-before-persistence/` with its matching
ELF/map/config. Its SHA256, image CRC, load segments and early IRAM placement
have been checked again. This is the previously built image, not a rebuild
from newer source. The wrapper validates these checks before calling esptool.

## Board comparison — user reports PASS/FAIL

Keep the board in the same place with the same USB supply/cable and router.
Close the monitor with Ctrl+], then from the repository root:

```sh
esp8266/scripts/webrepl.sh flash-r4 /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. `:info` must show L01-r4. This command flashes only the archived firmware
   range at offset0, not an erase-all or full-flash backup. No assistant opens
   the serial port; the user runs the command.
2. Enter `:wifi join` and the same SSID/password. R4 stores neither on flash.
   Wait up to60seconds for a URL to make this comparable to the R6 report.
3. If a URL appears, open the **new full printed URL/key** and connect. R4 uses
   a new per-boot key and numeric IP; R5/R6 remembered keys and mathsmith.local
   do not apply. Enter `2+2`; PASS requires4. Record complete `:wifi` and `:mem`.
4. If no URL appears, record full `:wifi`/`:mem` after60seconds. Report that as
   comparison FAIL. Do not keep waiting or change router settings to make the
   comparison pass.

If R4 works now, the regression comparison supports investigating the newer
firmware additions further. If R4 also fails now, the old success alone cannot
explain the current failure; investigate network/radio/address-exchange evidence
before assigning blame. Neither outcome proves an exact cause by itself.

The current R6 image remains at `build/esp8266/l01_webrepl/`; ordinary
`webrepl.sh flash PORT` still selects it. The comparison results are now recorded;
use the R6 hotspot save/reboot procedure above next. R6 is not marked fixed on
the home network by its native/browser tests or the R4 hotspot result.
