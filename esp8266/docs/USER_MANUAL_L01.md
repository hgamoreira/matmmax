# MathSmith Light L01 — browser and pin commands

**Current candidate: L01-r14, GPIO functions in programs.** Use pinmode,
digitalwrite, digitalread, analogread and delay(ms) in expressions/loops.
Console commands and functions share pin configuration. The first hardware
run reports4flashes/final read1 and a usable memory snapshot. The numbered
retest confirms five visible flashes/counts through5/LED off: blink PASS.
Delay/other pin/watchdog/stability hardware checks remain pending. [GPIO guide and board test](SCRIPT_GPIO_L01.md).


**Previous editor step: L01-r13, browser program editor.** Open/Save files on your
computer and Run/Stop multiline MathSmith programs in the new Program panel.
The R12 home-network workaround and saved Wi-Fi/key behavior remain.
The first user R13 editor loop returns36 and :mem remains usable: heap15256,
largest14376, fragmentation6%, minimum-free stack12288/16384. This is a partial
hardware PASS. The user clarifies this was a saved/reopened file, so Save/Open/Run
also PASS. An undefined-name error stops before a=99, leaving a=1: PASS.
Stop on an unfinished console block returns >> and2+2=4: PASS.
Active execution Stop preserves a=1/skips999 and2+2 works: PASS.
Other robustness and sustained-use hardware checks remain open.
[Editor guide and board test](BROWSER_EDITOR_L01.md).


17 November 2026. **Browser access is confirmed; full L01 hardware validation
remains open.** After the r4 candidate was supplied, the user reports a working
browser connection at **192.168.10.157**. The user also confirms `2+2` gives4,
the small linear solve gives column[2;3], and provides browser-workload memory
readings. Browser disconnect/reconnect with variables retained is also confirmed.
Stop, repeated-use stability and physical GPIO/ADC still need hardware checks.
**Working baseline: L01-r12.** The user confirms home-network connection and
browser access without a serial USB connection to the development computer. A later
report also confirms browser reconnection after a command-line software reboot
and after complete power removal followed by startup from a USB charger.
R12 requests broadcast DHCP replies before acquiring an IP; this is a working
home-network workaround. The origin of earlier short replies remains unknown.
This report does not separately establish repeated cold boots, key retention,
a specific R12 calculation result or fresh memory readings.
See [R12 result and procedure](DHCP_BROADCAST_L01.md) and
[packet interpretation](DHCP_PACKETS_L01.md).
The R5 board report reached association but no address after98seconds. R6
changes the initial join sequence and adds one bounded DHCP recovery attempt.
L00 remains serial recovery.

**Latest R6 result:** hotspot join/save and saved-record load after software
reboot pass. It reconnects automatically at172.20.10.2 by6seconds with the same
key and no DHCP recovery. The subsequent user report confirms cold USB-charger
startup and browser2+2 on the hotspot. Client hostname discovery is unspecified.
Current home-network joins fail in both R4 and R6; the cause remains unresolved.
R12 now passes home browser access; the [investigation history](HOME_NETWORK_DIAGNOSIS.md)
records the preceding failures. See the
[comparison history and current procedure](R4_COMPARISON.md).

L01 adds a local browser console and direct GPIO access to the same MathSmith
calculator/programming engine. The [math language manual](USER_MANUAL.md)
continues to describe arithmetic, matrices, functions and programs. Its L00
statements about absent Wi-Fi/browser/pin support apply to L00 only.
In a standalone release, that language guide is `MATH_LANGUAGE.md`.

## Set up Wi-Fi once

After flashing L01 using its [build/hardware guide](../../apps/esp8266/l01_webrepl/README.md),
open the serial monitor at 115200 8N1:

```sh
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

Use **L01-r14**, shown in the boot banner or `:info`. Close an existing monitor
with Ctrl+] before flashing:

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

R4 did not store credentials, so enter them once after this upgrade.

Then enter:

```text
:wifi join
```

Answer the separate prompts:

```text
Wi-Fi SSID (visible): YOUR_SSID
Wi-Fi password (hidden; Enter for open network): ********
```

Type your actual SSID, press Enter, then type your actual password and press
Enter. The stars show that password characters were received: do not type the
stars themselves. Do not add quotes around either answer in guided entry;
spaces are accepted as part of the answer. An empty password selects an open
network. SSIDs are limited to 32 printable ASCII characters and passwords to 63.

The single-line form is also supported:

```text
:wifi join "YOUR_SSID" "YOUR_PASSWORD"
```

Replace both placeholders;
quote fields containing spaces and escape a quote/backslash with a backslash.
An open network uses `""` as its second argument. The SSID stays visible and
only the password is masked. Keep terminal logs and pasted credentials private.
The ESP8266 uses 2.4 GHz Wi-Fi.

On serial, **use Backspace to correct the end of the line**. Arrow, Home, End
and terminal Delete-key escape sequences are ignored; they do not move the
cursor or recall history. This prevents their Esc prefixes from cancelling
setup or inserting `[A`, `[B`, `[C`, `[D`. Standalone Esc cancels after about
75 ms; Ctrl+C cancels immediately. Restart `:wifi join` to re-enter both fields.
The browser's Up/Down history is separate and still works.

Allow up to 30 seconds for Wi-Fi association and IP setup. Status `7` can
appear during this normal wait; it does not by itself mean joining failed.
Wait for these serial messages:

```text
Wi-Fi and browser key saved. Future boots reconnect automatically.
Browser URL: http://DEVICE_IP/#ACCESS_KEY
Named URL: http://mathsmith.local/#ACCESS_KEY
```

Open the complete **Named URL** in a browser on the same network. Check
**Remember key on this browser** if desired, then click **Connect**. Once
authenticated, the checked option stores the key in that browser's local
storage. It also works if checked after connecting. Unchecking removes the
stored copy. Retain a private copy of the key for another browser or computer.
The page removes the URL fragment from the address bar; it is not an HTTP path.

`mathsmith.local` uses local multicast DNS. If it does not resolve on your
network/computer, use the printed numeric **Browser URL**. A bare URL opens
the page too: supply the 32-character key in **Access key** unless the browser
already remembers it. `:wifi` repeats URLs/key on serial.

The user's board was reached at `http://192.168.10.157/` in the reported test.
This is an observed DHCP address, not a fixed firmware default; use the current
serial URL or the router's DHCP client list if the address changes and the
named address is unavailable. No fixed IP address is configured.

The page is served by the ESP8266 itself. No Internet connection, Python,
MicroPython installation, workstation server or browser extension is needed.
The calculator still runs on the ESP8266. Its USB cable can provide power;
serial is needed initially for Wi-Fi configuration and for recovery.

## Later use without a computer attached

After the **saved** message, close the monitor and disconnect the board from
the computer. Power it from a suitable USB charger or power bank. It joins
the saved network automatically; allow up to 30 seconds. Open
`http://mathsmith.local/` on a device on that network, supply the retained key
or use the remembered one, then click **Connect**. A numeric URL remains the
fallback if multicast discovery is unavailable. A phone/tablet browser can be
the console; the ESP8266 runs the calculations.

SSID, password and browser key survive power loss. **Variables and functions
remain in RAM and are cleared by reboot.** Browser Disconnect/reconnect keeps
them because it does not restart the board. Disconnecting the USB cable also
removes power unless the board has another power source.

## Change or forget the saved network

Attach USB and open the serial monitor. If a browser owns the console, reclaim
it with serial Ctrl+C. Enter `:wifi join` and answer the prompts again.
Only a successful connection to the requested network replaces the saved
settings; a failed attempt leaves the previous saved network for the next
boot. The browser key stays the same. If saving reports an error, retry and
wait for the saved message before relying on automatic boot.

- `:wifi off` stops Wi-Fi for the current session. The saved network returns
  on the next reboot.
- `:wifi forget` removes saved Wi-Fi and key, turns Wi-Fi off and changes the
  current key. Another reboot will stay in serial setup mode. Use `:wifi join`
  to configure again, then replace any remembered browser key with the new one.
- If forgetting reports failed flash cleanup, repeat `:wifi forget`.

These setup commands are serial-only. Credentials and the key are stored
unencrypted in the board's flash. The HTTP/WebSocket connection is
unencrypted and intended for a trusted LAN, without router port forwarding.
The key controls access; it does not provide transport encryption. Optional
browser key storage belongs to that browser/site origin; it does not save Wi-Fi
credentials, workspace or history. See [settings details](SETTINGS_L01.md).

## Working in the browser

Type a command and click **Run** or press Enter. Each input line supports up
to 191 printable ASCII characters. Use one line at a time, including `end` on
its own line for multiline blocks. The input field is disabled while a command
runs. `>>` means ordinary input; `..>` means the engine is collecting a block.

- **Up/Down** recalls up to 100 commands in this browser page.
- **Stop** or **Esc** cancels an unfinished block or requests cancellation of
  a calculation. Already completed assignments are retained.
- **Clear display** clears only browser text, preserving the board's workspace.
- **Save transcript** downloads the currently retained text to the computer.
- **Disconnect** releases the connection and cancels active work. Reconnecting
  keeps completed variables/functions but discards partial input or blocks.

Scrollback is limited to the latest 65536 JavaScript string characters in the
page. History and scrollback do not survive a page reload; save transcripts or
program text on the computer. The board has no file storage in L01. R13 adds computer-side source Open/Save
in the separate Program editor; the source stays in this browser until you
save a downloaded file. [Editor use and limits](BROWSER_EDITOR_L01.md).

One browser controls one shared RAM workspace. Other connections cannot take
over until it disconnects. Serial still displays output, but printable serial
input is ignored while the browser owns the console. **Serial Ctrl+C or Esc
disconnects the browser, cancels work and returns control to serial.** During
running calculations, other serial typeahead is discarded; wait for a prompt.

If a browser stops reading and the device's bounded output queue fills, the
device disconnects it and requests cancellation. Reconnect or use serial;
there is no automatic replay of missed output.

## Direct access to the pins

R14 also adds **expression functions** for use inside math programs:

```text
pinmode('D4','output',1);
digitalwrite('D4',0);
v=digitalread('D4');
a=analogread();
delay(250);
digitalwrite('D4',1);
```

See [argument limits, shared pin state and hardware tests](SCRIPT_GPIO_L01.md).
Delay is in integer milliseconds0..60000 and services Wi-Fi/Stop; it is not a
hard real-time scheduler. Outputs retain the last written level on Stop/error.

The colon forms below are **platform prompt commands**, usable from either console. They are
not MathSmith expression functions: you cannot assign `v=:read D1`, use them
inside engine loops or combine several with semicolons. Enter each command
on its own line. Pin reads print values; they do not set `ans`.

```text
:pins
:pin D1 output 0
:write D1 1
:read D1
:pin D1 input
:pin D2 pullup
:read D2
:analog A0
```

| Command | Effect |
|---|---|
| `:pins` | Print the board label/GPIO map and restrictions. |
| `:pin Dn input` | Configure digital input. |
| `:pin Dn pullup` | Configure input with internal pullup; D0 does not support it. |
| `:pin Dn output 0` | Set output with an initial LOW level. |
| `:pin Dn output 1` | Set output with an initial HIGH level. |
| `:pin Dn output` | Set output, initially LOW. |
| `:write Dn 0`, `:write Dn 1` | Set an already configured output LOW/HIGH. |
| `:read Dn` | Read the current digital signal, printing 0 or 1. |
| `:analog A0` | Read and print the raw ADC count, 0..1023. |

Only exact D1 mini labels `D0` through `D8` and ADC label `A0` are accepted:

| Board label | GPIO | Note |
|---|---:|---|
| D0 | 16 | No input pullup in this interface. |
| D1 | 5 | Also proposed for future OLED SCL. |
| D2 | 4 | Also proposed for future OLED SDA. |
| D3 | 0 | Boot strap: must be HIGH for ordinary flash boot. |
| D4 | 2 | Boot strap HIGH; usual onboard LED is active LOW. |
| D5 | 14 | Also proposed for future PS/2 clock. |
| D6 | 12 | Also proposed for future PS/2 data. |
| D7 | 13 | General digital pin. |
| D8 | 15 | Boot strap: must be LOW at boot. |

GPIO1/3 stay reserved for UART, and GPIO6..11 for flash. Bare GPIO numbers,
`TX`/`RX` and names outside this table are rejected. Future driver pin
reservations will need explicit ownership; no OLED or PS/2 driver is active now.

All digital pins use **3.3 V logic**. Do not apply 5 V or drive motors/relays
directly. External circuits must respect boot straps, even if an output mode
works after startup. The allowed voltage at A0 depends on the particular
board's divider: the bare ESP8266 ADC is 0..1 V, while D1 mini revisions can
include a divider. Verify the module before applying an external voltage.
No fixed count is expected from an unconnected A0.

The app leaves pin modes untouched at startup. A MathSmith `:reset`, browser
disconnect, Stop or math error **does not undo GPIO changes**; an output remains
at its configured level until changed or the board resets. Use `:pin Dn input`
to release it. Pin-mode state is not workspace variable state.

For an ordinary D1 mini with no external circuit on D4, try the onboard LED:

```text
:pin D4 output 1
:write D4 0
:write D4 1
```

The LED should be off, then on, then off. Keep it HIGH for the final state.
Do not attach a circuit that holds GPIO2 low during reset.

## Other commands and limits

`help`, `:help`, `who`, `clear`, `format short`, `format long`, `:mem`, `:info`,
`:reset` and `:reboot` retain their MathSmith meanings. `exit`/`quit` keep the
firmware's main prompt. Ctrl+] closes the supplied serial monitor.

`:wifi` and `:web` report status. With a connection established, they repeat
the URL/key **over serial only**. Wi-Fi setup, `:wifi off` and `:wifi forget` are serial-only;
reclaim serial control first if a browser is attached. `:wifi off` leaves
serial MathSmith and its existing workspace available.

`:wifi diag` prints a read-only station/interface/DHCP snapshot from either
console, including before an IP is assigned. It does not print passwords or
browser keys. Two readings20seconds apart help compare state/tries/timers;
the ordinary15second recovery policy still runs independently. Station MAC,
AP BSSID/channel, RSSI, hostname, interface flags/IP, DHCP stage and offered IP
are useful when the router's device list disagrees with the board's `:wifi`.
R10 also prints observed DHCP transmit/receive counts, driver/allocation errors,
and last-packet metadata. Counters reset on join. Driver acceptance does not
prove router receipt; received candidates may still be rejected by lwIP.
R11 also splits parser-rejected TX/RX and shows the last reason, lengths,
BOOTP opcode/cookie/xid/IPs and failing option metadata. These are observer
verdicts, not proof of invalid router data or lwIP rejection.
See [packet boundaries and interpretation](DHCP_PACKETS_L01.md).

Wi-Fi status codes from the pinned core are 0 idle, 1 network unavailable,
3 connected, 4 connection failed, 5 connection lost, 6 wrong password and
7 disconnected. **The core also returns 7 while the SDK is connecting.**
If the browser URL appears after a short wait, the board has joined and
obtained an address; you can use the browser without reflashing.

L01-r3 adds mode, raw SDK state, immediate `begin` return, elapsed time and
last disconnect reason to `:wifi`. SDK states are 0 idle, 1 connecting,
2 wrong password, 3 no AP found, 4 connection failed and 5 got IP. Mode 1
means station mode; mode 0 means off. A line such as
`Mode=1; SDK=1 (connecting); begin=7` describes a pending connection.
Disconnect reasons are distinct SDK numbers: 201 no AP, 202 authentication
failure, 203 association failure, 204 handshake timeout. A timeout reason
alone does not prove an incorrect password. Diagnostics never print it.

**L01-r4 checks IP assignment independently of the SDK status number.** It
adds `IP`, association state/event count, `got-ip-events`, `dhcp-timeouts`
and `ready`. The core has reported cases of SDK status remaining in
"connecting" after a real IP assignment. R4 can start the browser after an
IP-assignment event and a usable address even if status remains7. Explicit
disconnect/DHCP-timeout events invalidate readiness; a leftover address alone
does not override those events. This is a tested software correction, not
proof of the exact cause of the earlier physical stall. The later R4 browser
connection and numerical results have been confirmed by the user.

If `IP=0.0.0.0`, `associated=1` and `dhcp-timeouts` increases, association
has been observed but address setup is failing. If no association/IP event
appears, this points to an earlier stage; it does not by itself identify the
radio, network, credentials or SDK as faulty. Preserve the full diagnostic
output. After30seconds with no usable IP, R4 asks for diagnostics instead of
repeating the initial wait instruction indefinitely.

If no URL appears after 30 seconds, use `:wifi` and `:mem` and report those
results. Check 2.4 GHz availability and network security compatibility before
changing settings; no router reconfiguration is needed when joining succeeds.
The serial calculator remains usable while the SDK tries to connect.

**R8 address recovery:** after15seconds associated without an address, inspect
the actual client. If SDK DHCP is enabled, interface/link are up and the real
client is OFF with no PCB (or absent), start it once and report the real API
result. Active negotiations are left running. `DHCP=1` in `:wifi` is the SDK
enabled flag; `:wifi diag` shows actual client state. `recovery-attempted=yes`
means the guarded real start was tried, not that an address arrived. Result0
means the start API succeeded; negative values are errors. No unverified
credentials are saved. If still stalled, capture status/diag and repeat only
diag after20seconds. The earlier R6/R7 stop/config restart has been replaced.

The inherited provisional limits are 16 variables including `ans`, 64 elements
per matrix, four anonymous and four named functions, a 16 KiB engine-payload
cap and a dedicated 16 KiB stack. **Wi-Fi and TCP need additional memory**:
these limits are not a promise that every maximum-size operation fits. Use
`:mem` after connecting, after representative calculations and after repeated
disconnect/reconnect cycles. Report free/largest heap, engine peak and minimum
free stack using the numbered hardware procedure.

The first reported **L01-r4 browser workload** leaves18696bytes free heap,
with a13824-byte largest block,23% fragmentation and11708/16384 minimum-free
continuation stack. Engine payload is4984 live /5329 peak of16384. That shows
headroom for the observed small workload; it does not guarantee all maximum
operations or long sessions. Do not add engine payload to free heap as separate
available memory. See the [measured memory map](MEMORY_MAP_L01.md) for the exact
report and planned repeat-workload checks.

R13 adds source-file Open/Save on the computer and multiline editor execution
on the ESP8266. No board filesystem, FFT, AI client, graphs, external binaries
or OLED/PS2 driver is added. R14 now adds expression-level pin functions; browser plot rendering remains
a later extension.

Further references: [hardware test](../../apps/esp8266/l01_webrepl/README.md),
[memory map](MEMORY_MAP_L01.md), [wire protocol](WEB_PROTOCOL_L01.md).
The release bundles these as `README.md`, `MEMORY_MAP.md`, `WEB_PROTOCOL.md`
and the persistence reference as `SETTINGS.md`.
