# Proposed MathSmith browser console

2026-11-16. The user suggested a network browser REPL, like MicroPython's
WebREPL, then explicitly selected starting browser work and direct pin commands.
The resulting separate [L01 candidate](../../apps/esp8266/l01_webrepl/README.md)
implements this direction; its hardware test remains open. L00 remains available
as a serial-only, Wi-Fi-off recovery build. The proposal below is retained as
design context; the [L01 manual](USER_MANUAL_L01.md) describes implemented behaviour.

## Proposed first scope

- Join the existing Wi-Fi network, with setup through serial and the IP printed
  there. Keep serial recovery available if connection fails.
- Serve a small self-contained HTML/JavaScript page from firmware flash, with
  command input, output, history and a Stop control. No CDN or Internet required.
- Use a bounded WebSocket connection to feed the existing MathSmith console
  and stream its output. One controlling browser, one shared RAM workspace;
  serial and browser must not interleave partial commands or evaluations.
- Preserve the existing engine, double precision and provisional limits until
  combined runtime measurements show whether they need adjustment.
- Keep browser history/scrollback and downloadable transcripts on the computer;
  no board filesystem, program upload, AI or graphics in this first scope.
- Target a trusted local network initially. Authentication and exposure policy
  must be explicit; a password on plain HTTP/WebSocket does not encrypt traffic.

This would be a MathSmith frontend using a similar transport, not a claim of
compatibility with MicroPython's WebREPL protocol/client or file-transfer mode.
[MicroPython WebREPL documentation](https://docs.micropython.org/en/latest/esp8266/quickref.html#webrepl-web-browser-interactive-prompt)
describes its WebSocket/browser approach.

## Fit and execution constraints

The reported L00 matrix/eigensolver session left 27888 bytes free heap with
Wi-Fi off, while using only 5072 of the 16384-byte engine payload cap. This
supports investigating the feature, not promising unchanged math capacity.
Wi-Fi/TCP, socket buffers and connection handling all need additional RAM.
Measure free/largest heap block, engine peak, IRAM and stack during connection,
representative calculations, disconnect/reconnect and repeated use.

Current `ms_write` and `ms_poll` are serial-only adapter points. A network
adapter must stream bounded chunks, handle a slow/disconnected browser without
unbounded buffering, and service incoming Stop messages during calculation
polls. Yielding to the SDK alone does not process application WebSocket frames.
Never recursively evaluate commands while servicing networking; queue bounded
input for the existing single engine and explicitly arbitrate ownership.

A future browser UI could also render plots using browser resources. That
would need a separately scoped plot-data interface; the proposed text REPL
does not add plot support.

Native transport/parser tests and real-browser interaction can check framing,
input bounds, cancellation and reconnect behaviour. Only a real-board run can
establish Wi-Fi coexistence, memory margins, watchdog behaviour and responsiveness.
The initial design discussion did not change firmware. L01 implementation is
now scoped by XD112; no user network credentials or physical board flashing
were performed by the assistant.


## Approved browser editor continuation (2026-11-17)

After R12 home/cold startup PASS, user accepts computer-side script Open/Save
and browser editor Run/Stop. Implemented as R13 in the same app, XD114; no
filesystem/native binary loader or GPIO expression extension. Whole-source
prevalidation, acknowledged single-line queue, explicit results and Stop/error/
disconnect halt keep device buffers unchanged. [Current editor manual and
hardware test](BROWSER_EDITOR_L01.md). Hardware pass remains pending.
