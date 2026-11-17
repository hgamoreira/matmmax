# L01 browser protocol and ownership

16 November 2026. Implemented in `apps/esp8266/l01_webrepl/protocol.c` and
`firmware.cpp`. This is MatmMax's protocol, not MicroPython WebREPL.

## HTTP and authentication

The ESP8266 listens on TCP port 80 only after a successful station connection.
GET `/` serves the static flash page with a Content-Length and closes the HTTP
connection. No CDN, external assets, cookies, filesystem or URL-based commands.
The single accepted connection receives at most 1535 header bytes plus NUL and
has an absolute 5-second deadline for request/reply transfer. One pending
connection is requested from the pinned core's listen backlog; other accepted
connections are immediately closed while one is in use.

GET `/ws` upgrades using RFC6455 version 13. The request must have:

- `Host` exactly equal to the device's numeric IPv4 address or the fixed alias
  `mathsmith.local` (no `:80` alias).
- `Origin` exactly `http://` plus that Host.
- `Sec-WebSocket-Protocol: mathsmith.TOKEN`, where TOKEN is the serial-printed
  32-hex-digit random access key. R5 saves it with successful Wi-Fi setup and
  restores it at boot; `:wifi forget` rotates it. The response echoes it.
- Valid Upgrade, Connection, version and canonical 16-byte base64 key fields.

Relevant duplicate headers and request bodies/Transfer-Encoding are rejected.
Keys travel in the upgrade header, not the request URL. The native POSIX test
HAL uses `127.0.0.1:PORT` or `mathsmith.local:PORT` as Host, with matching Origin.
R5 advertises the hostname and HTTP service using the pinned core's mDNS library;
numeric-IP access remains usable if discovery fails. An upgrade during a serial evaluation
or while guided serial Wi-Fi setup is active is refused with 409;
bad authentication/Host/Origin is 403, malformed input
400. The SHA-1/base64 accept result is tested against RFC and Python references.
HTTP errors are empty responses and close the connection.

## WebSocket application messages

Client messages are masked, FIN=1 **text** frames, at most 192 payload bytes.
Noncanonical extended lengths, 64-bit lengths, reserved bits/opcodes, binary
input and fragmentation are rejected. A client can receive Close for unsupported
input; this minimal endpoint supports the browser's bounded individual messages.

| Direction | Payload | Meaning |
|---|---|---|
| Browser → board | ASCII `C` followed by 0..191 printable ASCII characters | Submit one input line; newline is supplied by the firmware. |
| Browser → board | ASCII `S` | Request cancellation; discard any queued command. |
| Board → browser | Binary `O` followed by up to 127 output bytes | Append console output literally as text. |
| Board → browser | Binary `R00` | Successful completion, ordinary prompt. |
| Board → browser | Binary `R10` | Successful line, collecting a multiline block. |
| Board → browser | Binary `R01` / `R11` | Engine/input validation error, ordinary/block prompt. |
| Board → browser | Binary `R02` | Cancellation completed, ordinary prompt. |

No output is HTML. Browser rendering uses `textContent`. Wait for `R` before
sending the next `C`; another command while evaluating or already queued
closes the connection. Ping is answered with Pong; Pong is ignored. Close
releases ownership. Server frames are unmasked. No compression extension.


R13 appends result byte0(success),1(engine/console-input error or quit),2(cancel)
to the existing `R` plus prompt byte. Older console clients can ignore the new
third byte. The new editor requires this capability and disables batch Run for
legacy two-byte readiness; the interactive console remains usable. A printed
`error:` is not used as a result signal. Handled platform commands have their
existing textual diagnostics; scripts disallow platform-control lines, so the
queue relies on actual MatmMax/error/cancellation results.

The browser prevalidates the whole script, retains at most32KiB/1024physical
lines locally and sends exactly one <=191printable-ASCII line for each ready
acknowledgment. It halts on a result error and uses `S` to discard an open block
at EOF. User Stop stops sending immediately and waits for cancellation result,
including the case of an earlier queued/finishing command acknowledgment. No
automatic resume/retry/rollback. Console entry is locked while batch execution
runs. Source/file UI stays local; HTTP/WebSocket adds no file-transfer endpoint.
See [editor](BROWSER_EDITOR_L01.md). R12 networking remains unchanged.


R14 leaves wire messages/ownership unchanged. Generated L01-only GPIO builtins
share the console's pin registry and return ordinary scalar values or engine
errors. delay(ms) polls network/Stop and yields to SDK in slices<=5ms; there is
no recursive evaluation or new input/file queue. Stop/drop/serial reclaim can
interrupt delay, with completed writes left latched. Argument errors emit the
normal engine error/result1; consumed cancellation emits result2. Read/write
access does not bypass D0..D8 labels or UART/flash restrictions.
[Script GPIO](SCRIPT_GPIO_L01.md).

## Execution and bounded resources

One console/engine instance, one queued 192-byte line, one 1024-byte output ring
(1023 usable) and a 193-byte frame payload buffer. Outgoing chunks coalesce up
to 127 text bytes, with small-output latency around one service interval plus
8 ms, and flush before readiness. The TCP SDK has its own additional buffers.
Use `availableForWrite` before bounded writes; failed/partial writes or output
overflow drop the connection and request cancellation instead of growing RAM.

Networking is explicitly serviced inside `ms_poll` and `ms_write`, as well as
the outer firmware loop. The service path only queues commands or sets Stop;
it never evaluates the engine recursively. Engine evaluation occurs in the
outer loop. Ordinary Wi-Fi SDK yielding alone is insufficient to decode Stop.

An authenticated browser takes exclusive input ownership. Its connection
discards existing incomplete serial input/blocks but keeps completed variables.
Serial output continues; printable serial input is ignored. Serial Ctrl+C/Esc
disconnects/reclaims and cancels. During evaluation, other serial typeahead is
discarded. A drop/close cancels active work, clears queued input and discards
unfinished blocks at the next safe outer-loop point. Reconnect keeps completed
workspace values; there is no transactional rollback or missed-output replay.

R5 Wi-Fi/key settings are application-persisted only after fresh matching-SSID
association and IP events; boot loading does not rewrite flash. Callbacks only
set state; saving occurs in the outer loop. Wi-Fi join/off/forget remain serial
commands. See [settings format](SETTINGS_L01.md). Workspace remains RAM-only.
The page can optionally remember an authenticated key in browser localStorage;
unchecking removes that browser copy. This does not erase board settings.
HTTP/WebSocket and stored flash credentials are unencrypted; use a trusted LAN.

Serial input decodes bounded CSI/SS3 key sequences before console delivery.
Those unsupported editing keys are ignored instead of being mistaken for
standalone Esc; Esc is disambiguated with a 75 ms inter-byte timeout. Ctrl+C
and internal/browser Stop remain immediate. `:wifi join` alone starts a serial
SSID/password prompt flow. Only password input is masked and credential input
buffers are cleared on submission/cancel. A web client cannot start or take
over that flow. Inline quoted Wi-Fi setup remains supported.

R7's `:wifi diag` is a read-only platform command allowed over either owned
console. It snapshots station/AP/interface/DHCP data before yielding output;
it does not expose passwords/keys, change readiness, reconnect or write flash.
The protocol/ownership and regular DHCP recovery policy are unchanged.

R10 observes DHCP at the pinned SDK/lwIP packet hook using fixed counters and
metadata. The hook does not allocate, print, yield, retain packet bodies or
change packets/results. `:wifi diag` prints a snapshot without rejoining,
resetting counters, evaluating expressions or writing settings. See
[packet boundaries](DHCP_PACKETS_L01.md). Browser ownership is unchanged. R11 adds separate rejected TX/RX counts and
last rejection metadata/reason; observer verdicts do not imply lwIP rejection
or invalid router data. This changes no packets, acceptance or settings.

R12 requests broadcast DHCP replies for station st0 Discover/Request with
zero `ciaddr`, using a wrapper at the stock options-construction hook before
checksums. The original client-ID and caller's hostname options remain. No
receive-validation/radio/record/key/browser-ownership change. The read-only
PHY observer remains separate from construction. See [R12](DHCP_BROADCAST_L01.md).

R9 uses the bundled standard low-memory feature1 IPv4 stack, including its
DHCP compatibility options. The browser wire protocol/ownership is unchanged.
Link-local169.254/16 fallback does not start the browser or save pending network
settings; a usable LAN address and the normal verification remain required.

R6 restores the R4 cold connection sequence and resets an already active
station before a replacement attempt. After15seconds associated without a
usable address, R6/R7 tried one DHCP stop/config restart per join/boot. R8
instead starts only a real stopped lwIP client, at most once, with SDK enabled,
interface/link up and no address. Active DHCP negotiations are left running.
It cannot start the browser or save settings without the usual address/event
checks. Wi-Fi event callbacks only update flags/time; no DHCP operations occur inside them.
`:wifi` reports DHCP state and recovery attempted. The exact R5 stall mechanism
is not fully established; R7's real-client OFF state motivates R8's targeted
start. Physical R8 recovery is still pending.

Network setup is serial-only. R5 persists SSID/password and browser key;
workspace remains RAM-only. SDK RF/calibration sectors are still used by
normal SDK operation. HTTP/WebSocket is unencrypted and scoped to a trusted
LAN. No TLS, remote provisioning, OTA or Internet publication here.

L01-r3 status diagnostics read the pinned SDK's raw station state and retain
the latest numeric disconnect reason through the core's event handler. The
callback only stores reason/count; it performs no console evaluation or writes.
`WiFi.status()==7` is ambiguous: SDK state1 is still connecting. Connection
setup remains asynchronous; no blocking 30-second wait is introduced. A
failed station-mode enable is reported instead of blindly claiming connection
has begun. Credentials are absent from diagnostic messages.

L01-r4 retains association/IP-assignment/DHCP-timeout handlers as well. They
only store flags/counters; evaluation and listening still happen in the outer
loop. A usable current station IP plus an IP-assignment event permits listening
even if `WiFi.status()` remains7, a reported SDK case. Normal SDK-connected
status remains a fallback when not invalidated by a disconnect/timeout. Mode
off, missing IP or an explicit link-down event prevents readiness. A retained
IP after disconnect cannot by itself restart the server. Event counters and
the independent IP are exposed by `:wifi` for diagnosis.

Source: [ESP8266 core issue7432](https://github.com/esp8266/Arduino/issues/7432).
This precedent motivates checking the actual board's IP/events; it is not a
confirmed diagnosis of the user's r3 stall.

References: [RFC6455](https://www.rfc-editor.org/rfc/rfc6455),
[pinned ESP8266 core](https://github.com/esp8266/Arduino/tree/3.1.2).
