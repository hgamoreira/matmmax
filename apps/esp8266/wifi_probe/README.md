# WIFI-PROBE-r1 — independent ESP8266 Wi-Fi comparison

2026-11-16. **Diagnostic candidate; no physical probe result yet.** This is a
fallback inside the unresolved L01 Wi-Fi investigation, not a new workstation
milestone. It uses the same pinned core3.1.2 / GCC10.3 / SDK190703, CPU80 MHz,
26 MHz crystal and 4 MB DIO40 MHz flash profile as MatmMax Light.

Since L01-r9, the browser firmware selects the standard low-memory feature1
lwIP variant for DHCP compatibility; this retained probe uses feature0 like
the earlier L01 images. Account for that difference in any radio/engine
comparison. The rebuilt probe binary remains identical to its r1 baseline.

It omits MatmMax, matrices, the browser server, GPIO commands and workspace
allocation. Its dedicated continuation stack is **4096 bytes**, rather than
L01's16384. Only serial input, Wi-Fi station join/status and a disconnect-event
counter remain. The SDK still allocates radio/network memory at runtime.
There is no website at the printed IP address; this program tests association/IP.

## Use current MatmMax diagnostics first

The user reports no URL and persistent status7; r3 then reports SDK1 through
140seconds with no disconnect events. R3 memory readings do not show obvious
exhaustion. L01-r4 checks actual IP/association/IP-assignment/DHCP events,
including a documented SDK stale-status case. Try its `:wifi` and `:mem`
diagnostics before changing to this engine-free comparison image.
No password needs to be shared; do not change router settings blindly.

If comparison is still useful, leave access point, power supply, cable and
location unchanged and use the same credentials on both images. A different
outcome can guide memory/scheduling investigation; it alone does not prove a
particular cause. If both fail, this does not prove a defective radio or router:
both builds share the same core/SDK/toolchain/flash-calibration environment.

## Build, test and stage (no board opened)

From the repository root:

```sh
python3 esp8266/scripts/wifi_probe.py build
python3 esp8266/scripts/wifi_probe.py test
python3 esp8266/scripts/wifi_probe.py release
```

Output is isolated in `build/esp8266/wifi_probe/`; staged files are in
`releases/esp8266/wifi_probe/`. The image filename remains `mathsmith_light.bin`
to reuse the checked image converter, but **inside this probe folder it is
Wi-Fi-only firmware**. Neither the L00 nor L01 candidate is overwritten.
Keep the exact probe ELF/map/config for crash-address decoding.

Native tests use the existing POSIX adapter with injected SDK states. They
check input/masking/recovery, not actual Wi-Fi. Target checks verify CRC/load
segments/startup IRAM, input freshness, a 4136-byte continuation object and
the absence of the MatmMax entry points from the linked image.
The RAM comparison test also needs an existing L01 target build.

## Linked memory map

| Section / symbol | Address | Bytes |
|---|---|---:|
| `.data` | `0x3ffe8000` | 1504 |
| `.noinit` | `0x3ffe85e0` | 56 |
| `.rodata` | `0x3ffe8620` | 1772 |
| `.bss` | `0x3ffe8d10` | 29968 |
| `.text` / `.text1` | `0x40100000` / `0x401000e4` | 226 / 26877 |
| `.irom0.text` | `0x40201010` | 234764 |
| Dedicated continuation object | `0x3ffeea30` | 4136 |
| `_heap_start` | `0x3fff0220` | Dynamic SDK allocations follow |

Static `.data/.rodata/.bss` is33244bytes, **19768 bytes less than L01-r4**.
Occupied DRAM including noinit/alignment is33312bytes; IRAM code27103/32768.
The image is269296bytes. These are linked measurements, not free radio-active
heap. SDK allocations and the event-handler object are additional. Probe input
uses96bytes plus33bytes for the SSID; password text is cleared after use/cancel.
No16KiB engine budget, matrix values or1024-byte browser ring is allocated.

## Optional board comparison — report PASS/FAIL and diagnostics

1. Close the existing serial monitor with Ctrl+]. Retain the L01 image and a
   flash backup if needed; the normal `webrepl.sh backup PORT` command is
   available. Flash and open this distinct image only when ready to compare:

   ```sh
   python3 esp8266/scripts/wifi_probe.py flash /dev/ttyUSB0
   python3 esp8266/scripts/wifi_probe.py monitor /dev/ttyUSB0
   ```

   PASS: `WIFI-PROBE-r1` and `probe>` appear at115200baud, without a reset loop.
   Record startup heap/largest block/stack measurements.

2. Enter `:wifi join`, then the raw SSID and password as separate answers,
   each followed by Enter. No quotes. SSID is visible and password shows stars.
   Backspace edits; Ctrl+C cancels. Terminal arrows are ignored.
   PASS: input works and a join is requested without revealing the password.

3. Wait30seconds, enter `:wifi` and `:mem`, and record their full output.
   **Association/IP PASS** means `SDK=5 (got-ip)`, `status=3` and a real IP
   address. Otherwise report FAIL, with SDK state, reason, elapsed time and
   memory values. The probe deliberately has no browser URL/server.
   It prints the actual IP even if SDK status remains7: a nonzero address
   with a stale status is evidence worth reporting, not an automatic FAIL.

4. To restore MatmMax, close the probe monitor and run:

   ```sh
   esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
   esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
   ```

   PASS: L01-r4 returns. Its credentials/workspace are RAM-only, so enter them
   again. Do not call the MatmMax Wi-Fi problem solved solely because the
   probe connects. No probe has been flashed or radio tested by the assistant.
