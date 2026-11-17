# L01 — ESP8266 browser MathSmith and direct GPIO

**Current candidate: L01-r14 — scriptable GPIO.** The user-selected pinmode,
digitalwrite, digitalread, analogread and cancellable delay(ms) functions are
implemented for console expressions and programs. Console commands share pin
configuration. Label/argument checks and reserved pins remain enforced.
**R14 physical blink PASS:** the numbered retest gives five visible flashes,
printed counts through5 and LED off on unchanged firmware. Original read1 and
connected :mem: heap15128/largest14312/frag6%, stack free-min10808/16384,
engine4928/5379 of16384. Delay cancellation/input/ADC/stability remain open. R13 core editor tests
now have hardware PASS, including active Stop; recovery images are preserved.
All88 L01 checks,19 L00/6 probe regressions and Chrome checks PASS off-board.
[GPIO guide and numbered hardware test](../../../esp8266/docs/SCRIPT_GPIO_L01.md).

**Previous editor step: L01-r13 — browser program editor.** User-approved editor,
local Open/Save and bounded Run Program/Stop are implemented. Source stays in
the browser; only one acknowledged line is sent at a time. Real engine errors
halt the remaining queue; disconnect never replays a program. R12 Wi-Fi/key
behavior remains. **R13 hardware partial PASS:** the user's editor loop returns
36 and connected :mem remains usable (17 November). Heap15256/largest14376,
fragmentation6%, stack minimum-free12288/16384, engine4904/5292 of16384.
The user clarifies that this run used a saved/reopened file: Save/Open/Run
also PASS. Runtime error halts the tail (a remains1, a=99 skipped): PASS.
Stop on an unfinished console block restores the prompt and2+2 works: PASS.
Active execution Stop preserves a=1/skips999 and2+2 works: PASS.
Other robustness/repeated-use hardware checks remain open.
All80 L01 checks and Chrome editor/console checks PASS off-board.
[Editor manual and numbered board test](../../../esp8266/docs/BROWSER_EDITOR_L01.md).

**Working baseline: L01-r12 — home Wi-Fi/browser PASS (17 November 2026).**
The user confirms it works and the browser is accessible without a serial USB
connection to this computer. A further user report confirms browser
reconnection after a command-line software reboot and after complete power
removal/USB-charger startup. R12 requests broadcast replies before
acquiring an address. Keep this validated workaround; the earlier short-reply
origin remains unresolved. Full numbered L01 validation remains open.
All77 L01 checks,19 L00/6 probe regressions and Chrome check PASS off-board.
[R12 behavior and test record](../../../esp8266/docs/DHCP_BROADCAST_L01.md).

**Earlier R6 results (2026-11-16):** R6 hotspot connection and reported settings save
PASS at172.20.10.2 within9seconds, without DHCP recovery. R4 also passes on the
hotspot. R4/R6 currently fail to obtain an address at home; cause unconfirmed.
The next reported software reboot **passes saved-record load, automatic join
by6seconds and unchanged key**. Next: cold power/USB charger and browser check;
no reflash needed. [Comparison guide](../../../esp8266/docs/R4_COMPARISON.md).
The next user report confirms **cold power/charger startup and browser2+2 PASS**
on the hotspot. Client hostname resolution is unspecified. The earlier diagnostic history is
the [home-network investigation](../../../esp8266/docs/HOME_NETWORK_DIAGNOSIS.md).
R6 is not a confirmed home-network fix.
R4 has user-confirmed browser access at 192.168.10.157, arithmetic, a small
matrix solve and browser reconnect with variables retained. R6 saved Wi-Fi/key
reload after software reboot now passes on the hotspot; the rest remains open.
Native/socket/linked-image checks and a real headless Chrome UI check cover
the candidate; current results are recorded in `PROGRESS.md`.
All 19 L00 regression tests PASS; its rebuilt firmware remains byte-identical
to the working candidate. These are off-board results only.
Scope approved by the user's instruction to start browser work and expose pins
(XD112). L00's working serial candidate is retained separately. L01 uses the
same bounded double-precision engine and adds a flash-hosted browser console,
Wi-Fi station mode and direct digital/ADC commands. R5 also links the pinned
core's mDNS library.

The user's first L01 report reached the serial prompt but could not complete
Wi-Fi setup: masking both fields was confusing, and arrow-key Esc prefixes
cancelled the line and left `A`/`[B` text. This candidate adds guided visible
SSID/hidden password entry and consumes unsupported serial editing sequences.
That first report did not yet establish Wi-Fi association or browser use.

**Earlier corrected user report:** on L01-r2 guided prompts work, but no browser
URL appears and status remains7. The earlier selected answer saying a URL
appeared was superseded by that explicit correction; Wi-Fi/IP/browser success
is unconfirmed. L01-r3 exposes SDK state/reason/elapsed diagnostics. Install r3,
join once, wait30seconds, then record `:wifi` and `:mem`. No credential or router
fault is established from status7 alone. The next actual r3 report remained
SDK1/status7 at21,114,140seconds, with no disconnect events. Idle free heap21288,
largest20768 and stack minimum-free15096 do not show obvious exhaustion.
[Recorded report (local hardware log omitted; see the validation summary).

**L01-r4:** observe association, IP-assignment and DHCP-timeout events and the
actual station IP. The browser can start after a usable IP assignment even
when SDK status is stale. This addresses a documented core/SDK status-handling
case, with native regression coverage. An explicit disconnect/timeout
invalidates readiness.

**R4 hardware reports:** after r4 was supplied, the user explicitly reports
connecting in a browser and that it worked at **192.168.10.157**.
Recorded report (local hardware log omitted; see the validation summary). This confirms
browser reachability. The follow-up memory report (local hardware log omitted; see the validation summary)
confirms **L01-r4**: free heap18696, largest block13824, fragmentation23%,
minimum free stack11708/16384 and engine live/peak4984/5329 of16384. These
readings show headroom for that workload. The user subsequently explicitly
confirms `2+2` gives4 and the 2x2 solve gives column[2;3]. Repeated-workload
stability, Stop and physical GPIO checks remain individually unreported.
Browser disconnect/reconnect retaining `x=41` and returning42 is now confirmed.
Event counters were not pasted, so the precise SDK cause of the earlier stall
is not established.

**L01-r5 (XD113):** configure once over serial. A successful matching association
and fresh IP assignment save SSID/password and the browser key. Later boots
join automatically without a PC attached. Serial `:wifi join` replaces settings
only after success; `:wifi forget` deletes them and rotates the key. `:wifi off`
is session-only. Optional browser key storage and best-effort `mathsmith.local`
discovery support everyday use. Variables remain RAM-only. The pinned core's
mDNS library is now linked; no new external dependency is downloaded.

**L01-r6:** the user's R5 report shows association but no address/GotIP event
after98seconds, with no obvious heap/stack exhaustion. Settings remain pending.
Exact report (local hardware log omitted; see the validation summary). R6 restores the R4 cold
`mode(STA)`/`begin` sequence, removing R5's disconnect of a newly enabled station.
A replacement attempt on an existing station resets the interface before
enabling it again. SDK reconnect policy is enabled after readiness. After15
seconds associated without an address, one outer-loop DHCP stop/config restart
is attempted and reported. No fixed IP or indefinite restart loop. `:wifi` adds
DHCP state/recovery diagnostics. This policy is tested off-board; the exact
cause and physical fix remain unconfirmed.

Everyday instructions: [L01 user manual](../../../esp8266/docs/USER_MANUAL_L01.md).
Wire protocol: [HTTP/WebSocket and ownership](../../../esp8266/docs/WEB_PROTOCOL_L01.md).
Memory: [L01 layout and runtime measurements required](../../../esp8266/docs/MEMORY_MAP_L01.md).
Persistence: [flash format and interruption handling](../../../esp8266/docs/SETTINGS_L01.md).

## Browser programs (R13)

Use the new Program panel for multiline source and computer-side Open/Save.
Run Program feeds the existing engine one acknowledged line at a time; Stop,
engine failure or disconnect ends the remaining queue. The editor preserves
local text on connection loss and asks before replacing unsaved edits. Script
limits32KiB/1024lines/191printable characters per transmitted line; existing
engine limits remain. Platform-control commands stay in the console. No board
filesystem is introduced in R13; R14 now adds the separately approved GPIO
expression functions documented above.

The release includes `sum.ms`, `solve.ms`, `square.ms` and BROWSER_EDITOR.md.
[Complete procedure](../../../esp8266/docs/BROWSER_EDITOR_L01.md) covers file
round trips, error halt, Stop/disconnect, incomplete blocks and actual :mem.
The older whole-L01 procedure below remains applicable; only its user-reported
steps are passed. No R13 firmware hardware PASS is inferred from R12 results.

## Build and stage

From the repository root, after the existing ESP8266 toolchain setup:

```sh
esp8266/scripts/webrepl.sh build
esp8266/scripts/webrepl.sh test
esp8266/scripts/webrepl.sh release
```

Output: `build/esp8266/l01_webrepl/mathsmith_light.{bin,elf,map}`.
Staged candidate: `releases/esp8266/l01_webrepl/`, including manuals and SHA256SUMS.
The plain `mathsmith.sh` commands still select L00. Build/stage/test commands
do not open the board. All candidates use the same target: Wemos D1 mini,
ESP8266EX / 4 MB flash / 26 MHz crystal, core 3.1.2, CPU 80 MHz, UART 115200.

The existing source/profile/CRC/load-segment/startup-IRAM checks apply to L01.
Host tests compile the actual firmware loop against a POSIX Wi-Fi/serial/GPIO
adapter and connect real localhost sockets. Parser and GPIO policy checks do
not establish RF behaviour, SDK memory use or physical pin voltages.

An optional real Chrome check uses an existing Node Playwright installation:

```sh
PLAYWRIGHT_NODE_MODULE=/path/to/playwright/package node esp8266/scripts/check_browser.js
```

`CHROME` may override `/usr/bin/google-chrome`. This needs local socket access
and a rebuilt L01 native harness (`webrepl.sh test` builds it). It exercises the
real page's input, history, safe text rendering, Stop, transcript, reconnect,
remembered-key reload and saved-settings process restart without serial setup.
Native fault tests check interrupted writes and settings replacement/deletion.
`webrepl.sh native` is the persistent POSIX transport harness, not the L00
line-only harness: Ctrl+C exits its process; it is not a firmware emulator.

## Focused R6 board retest

The user's run of this procedure did not restore an IP. See the
R6 report (local hardware log omitted; see the validation summary) and perform the R4 comparison
above before another firmware change. The following remains the R6 test record.

Close the monitor with Ctrl+], flash and reopen using the commands below.
Check `:info` for L01-r6, run guided `:wifi join` once and allow30seconds.
If no IP arrives after15seconds associated, one DHCP restart message should
appear. Connection PASS requires a URL and usable browser; saving PASS requires
the explicit saved message and `Saved network=yes`. Then reboot and confirm
automatic join with the same key. Finally test charger power without the PC.
If no URL appears, paste full `:wifi` and `:mem` including DHCP/recovery fields.
Collect new connected `:mem` even if recovery succeeds. Discovery, Stop and
electrical checks remain separate items in the full procedure.

## Hardware-test procedure — report PASS/FAIL per step

Keep the known working L00 candidate and its matching ELF. Disconnect external
GPIO circuitry for the first boot/LED test. No peripheral purchase or wiring
is required to check the onboard LED. The assistant has not flashed this board.

1. **Back up and flash.** Close the old monitor. If you have no flash backup
   you want to retain, create one before replacing the firmware:

   ```sh
   esp8266/scripts/webrepl.sh backup /dev/ttyUSB0
   esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
   esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
   ```

   PASS: flash completes and `MathSmith Light L01-r14` / `Ready. Type help.` appears.
   `:info` must also show `Build: L01-r14`. R4 does not retain Wi-Fi credentials.
   A brief ROM burst at the other baud rate is normal; continuous resets are not.

2. **Serial baseline.** Enter `2+2`, `help`, `:mem`, `:pins`.
   PASS: 4, L01 help, diagnostics and the D0..D8 GPIO map. Save these diagnostics.
   Startup diagnostics precede engine allocation; use the explicit `:mem` result.

3. **Wi-Fi setup.** On serial enter `:wifi join` and press Enter. At the visible
   SSID prompt enter your network name, then at the hidden password prompt
   enter your password. Do not quote answers to these separate prompts.
   Use a reachable 2.4 GHz network. PASS: SSID stays visible, password shows `*`;
   a `Browser URL: http://ADDRESS/#KEY` appears after connection. `:wifi` prints
   status and repeats the URL over serial. Also require **Wi-Fi and browser key
   saved. Future boots reconnect automatically.** SSID/password/key are now
   stored in application flash after a verified join. If saving fails, retain
   the diagnostics and retry; do not count persistence as passed.
   Allow up to 30 seconds: `status=7` can be normal while connecting. In r3,
   `SDK=1 (connecting)` explains that state; `SDK=5 (got-ip)` means ready.
   In r4 also record `IP`, association/IP event counts, DHCP timeouts and `ready`.
   A usable IP can enable the browser despite a stale SDK status. Do not keep
   waiting indefinitely if `ready=0` persists; capture `:wifi` and `:mem`.
   If connection fails, serial math must remain usable; check `:wifi` and retry
   with correct credentials rather than reflashing. Network/SDK status is not
   validated by the native adapter.
   Backspace corrects the end of an answer; serial arrows are ignored, with no
   `[A`/`[B` suffix or accidental cancellation. Standalone Esc (about 75 ms)
   or Ctrl+C cancels setup. The quoted single-line form
   `:wifi join "YOUR_SSID" "YOUR_PASSWORD"` remains supported.

4. **Browser connection.** Open that full URL in Chrome/Firefox on the same LAN.
   Click Connect. PASS: Ready, usable command field, `2+2` returns 4. No Internet,
   CDN or PC workstation server is required. Try the printed Named URL at
   `mathsmith.local`; numeric IP is the fallback. A bare URL needs its serial
   key entered unless remembered. Check **Remember key on this browser** and
   reload: the key remains available and Connect works. Uncheck/reload: no
   remembered key. Check again and connect for the later power-cycle test.

5. **Shared workspace.** In the browser enter `x=41;`, then `x+1`.
   PASS: 42. Disconnect and reconnect; `x+1` still gives 42. A second browser
   cannot take over while the first owns the connection.

6. **Math limits and diagnostics.** Enter the following in the browser:

   ```text
   format long
   sqrt(2)
   A=[1 2 3 4;2 3 4 1;3 4 1 2;4 1 2 3];
   eig(A)
   zeros(9,9)
   x+1
   :mem
   ```

   PASS: 1.41421356, eigenvalues approximately -2.82842712, -2, 2.82842712, 10;
   oversized matrix error; then 42 and usable diagnostics. Record free heap,
   largest block, engine peak and minimum free stack **with Wi-Fi active**.

7. **Typed blocks.** Enter `s=0;`, `for k=1:8`, `s=s+k;`, `end`, `s` as separate
   lines. PASS: `..>` during entry, then `s = 36` and ordinary `>>`.

8. **Stop while running.** Enter `for k=1:100000000`, `s=s+k;`, `end`, then click
   Stop (or press Esc). PASS: interrupted error and a ready prompt; `x+1` still
   returns 42. Start an unfinished `while 1` block and use Stop to discard it.

9. **Disconnect while running.** Repeat a long loop and disconnect the browser.
   PASS: it cancels; reconnect works with `x` intact. Repeat a few connect/close
   cycles and compare `:mem` after warming. A slow client may be disconnected
   deliberately when the bounded output buffer fills; serial recovery must work.

10. **Pin access and onboard LED.** With external circuitry disconnected:

    ```text
    :pin D4 output 1
    :write D4 0
    :write D4 1
    :read D4
    ```

    PASS: LED off, on, off and readback 1 (standard active-low D1 mini LED).
    D4/GPIO2 is also a boot strap: do not attach a circuit that holds it low
    during reset. If this board's LED differs, report the observed mapping.

11. **Pin policy.** Try `:read GPIO6`, `:read TX`, `:pin D0 pullup`,
    `:pin D1 output 2`, `:write D1 1` before configuring D1 as output.
    PASS: clear errors and the console stays usable. `:analog A0` returns a raw
    0..1023 count; with a floating input no fixed value is expected. Do not apply
    an external A0 voltage until the module's divider/rating is verified.

12. **Digital loopback, if desired.** With no other circuitry on D1/D2, jumper
    D1 to D2. Enter `:pin D2 input`, `:pin D1 output 0`, `:read D2`,
    `:write D1 1`, `:read D2`, `:pin D1 input`. PASS: D2 follows 0 then 1.
    Use only 3.3 V logic; never join two outputs. Remove the jumper afterwards.
    This optional external-input test can be reported NOT RUN separately.

13. **Serial recovery.** While the browser is connected, press Ctrl+C in the
    serial monitor. PASS: browser disconnects, serial prompt responds, `x+1`
    still returns 42. Reconnect the browser; `:wifi off` there must be refused.
    After serial reclaim, `:wifi off` disables networking and retains serial math.

14. **Browser controls.** Check Up/Down history, clear display and Save transcript.
    PASS: history recalls commands, clearing the display preserves variables,
    downloaded text includes results. Check that error messages stay readable.

15. **Saved boot and independent power.** Rejoin if step13 turned Wi-Fi off and
    wait for the saved message. Enter `x=41;`, then `:reboot` over serial.
    PASS: saved-network auto-join with no credential entry; the same browser
    key works, `2+2` returns4 and `x` is undefined (RAM workspace). Save `:mem`
    with the browser connected. Then close the monitor, unplug the computer
    and power the board from a USB charger/power bank. Allow30seconds and open
    `http://mathsmith.local/` with the remembered/retained key. PASS: browser
    math works with no PC/serial connection. Report named-address discovery
    separately if only the router-reported numeric IP works. No app pin modes
    are restored; external wiring must still respect boot straps.

16. **Replace, retain and forget.** Reconnect serial; Ctrl+C reclaims it.
    Use `:wifi join` with a deliberately nonexistent SSID, then reboot.
    PASS: the previously saved working network/key return. With another
    reachable network available, a successful `:wifi join` must replace it and
    survive reboot with the same key (otherwise report this subtest NOT RUN).
    `:wifi off`, then reboot: saved auto-join returns. Finally `:wifi forget`,
    then reboot: no auto-join; the old key is no longer current. Configure the
    desired network again, wait for saved and update the browser's remembered
    key. Do not share any passwords/keys in the test report. Keep L00 and R4
    recovery images and the matching current ELF for decoding any crash.

Full L01 validation requires the user's report; host/browser passes alone do
not close this milestone. Credentials and access keys need not be pasted into
the report. Preserve the exact candidate ELF for decoding any crash address.
