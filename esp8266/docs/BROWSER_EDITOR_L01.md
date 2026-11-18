# L01-r13 browser program editor

**Current R14 firmware** includes this editor and adds
[scriptable GPIO](SCRIPT_GPIO_L01.md). R13 editor results below remain historical
hardware evidence; do not infer R14 electrical behavior from them.


17 November2026. User-approved next step after R12 home Wi-Fi, warm reboot and
cold USB-charger startup passed. This adds an editor and computer-side program
files to the existing L01 firmware. **R13 hardware partial PASS:** the user
runs example.ms and gets s=36, then connected :mem succeeds. Heap15256,
largest14376/fragmentation6%, stack minimum-free12288/16384; engine live4904,
peak5292/limit16384. No obvious exhaustion in that workload; repeated use and
other robustness paths still need user validation. The user clarifies the reported
run came from a saved file opened in the editor: **Save -> Open -> Run PASS**.
No repeat of that round-trip test is needed. Runtime-error tail halt also
PASS: undefined missing_editor_name stops before a=99 and console a returns1.
The user also reports success cancelling an unfinished console for block with
Stop, returning to >> and calculating2+2=4. Active execution Stop also PASS: the user confirms cancellation, a remains1
instead of999 and subsequent2+2 works.
Exact hardware report (local hardware log omitted; see the validation summary).
The working R12 broadcast DHCP policy, saved network/key and math limits remain.

## Everyday use

Open the board's normal browser URL and connect with the same access key.
The Program panel contains a plain text editor and a small sum example.

- **New** clears the editor. It asks before replacing unsaved edits.
- **Open file** reads a `.ms`, `.m` or `.txt` file from this computer/phone.
  It asks before replacing edits. Opening a file does not execute it.
- **Save file** requests a text download to this computer/phone. The File name
  field chooses its name; `.ms` is added if no supported extension is present.
  Keep the downloaded file before closing the page. The board has no filesystem.
- **Run program**, or Ctrl+Enter while editing, runs source from the beginning
  in the board's existing workspace. Results appear in the console below.
- **Stop**, either button, or Esc stops the remaining queue and requests board
  cancellation. Completed assignments remain; there is no rollback.

The editor works offline from the board for Open/Save. Files/source are held
in the browser, not persistently saved by this page. Unsaved edits prompt when
leaving/reloading; browser behavior varies. A connection loss leaves editor
text available for saving. Reconnection never resumes or replays a program.
Save transcript still downloads console output separately from source files.

## Limits and behavior

Programs are MatmMax text, not separately loaded machine-code applications.
The ESP8266 retains its 16-variable,64-element matrix,64-bit double and16KiB
engine payload limits. Source is held and queued in the browser; the board
still receives one <=191-character input line at a time. Loops/functions use
the existing engine's block storage and allocation budget.

The editor validates the whole source before sending anything:32KiB after
normalization, up to1024 physical lines (including blank lines),191 printable
ASCII characters per line. A final newline creates a final blank physical
line. BOM/CRLF/CR become ordinary UTF-8-without-BOM/LF text; tab indentation
becomes four spaces. This also expands literal tabs inside quoted text; use
printable text for strings. Open rejects files larger than32KiB without replacing
the editor. Other invalid source can be opened and corrected; Run reports it.

Each source line waits for an explicit board result. Actual engine errors stop
later lines; printing the word `error:` is ordinary output. The status identifies
the source line being processed. Errors in a loop/function body can surface
when its closing `end` executes; inspect the engine message in the console.
Validation is not a whole-program syntax check and assignments before a runtime
error remain. A missing final `end` is detected at EOF and its unfinished block
is cancelled; it is not silently left open for later console commands.

Start a program at the ordinary `>>` prompt. Finish an existing console block
or press Stop before Run Program. Console input and New/Open/editing are locked
while a program runs; Save remains available. Lines beginning with `:` and
standalone abort/quit/exit control commands are rejected before execution.
Use platform commands in the console. R14 adds GPIO expression functions inside programs; see the separate GPIO
guide. Existing colon pin commands still work in the console.

## Firmware compatibility and resources

R13 appends a result byte to readiness: `R00`/`R10` success with ordinary/block
prompt, `R01` error, `R02` cancelled. Existing console clients may ignore the
third byte. The new editor disables batch Run on firmware lacking this result
byte; interactive console use remains available. No output-text error guessing.
Transport remains one shared engine/one queued line. No script upload buffer,
board file write, recursive evaluator, timer-driven retry or reconnect replay.

The page is streamed from flash using existing bounded buffers. Browser source,
file operations and the program queue consume browser memory; engine blocks
still consume board memory. See [memory map](MEMORY_MAP_L01.md). Only physical
measurements establish watchdog/network/heap/stack margins during editor use.

## Hardware test — user PASS/FAIL

No re-entry of Wi-Fi credentials should be needed. Retain your normal browser
URL/key. Close a serial monitor with Ctrl+], then:

```sh
esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
esp8266/scripts/webrepl.sh monitor /dev/ttyUSB0
```

1. **Boot/home access:** confirm L01-r13 in `:info` and a home-browser connection.
   Reload the page fully after flashing. PASS: Program panel appears and the
   existing key works. Record `:mem` from the connected browser before running.
2. **Run the initial example:** click Run program. PASS: `s = 36` in output,
   Program finished, and ordinary console remains usable. Run again: still36.
3. **Save/load:** Save file, then New and Open the downloaded file. PASS: text
   round-trips and Run still produces36. Try declining replacement of unsaved
   edits; the current text must remain. Opening must never execute by itself.
   The staged release also includes `sum.ms`, `solve.ms`, `square.ms` (36,
   column[2;3],49 respectively). Each can be opened/run from the computer.
4. **Runtime error stops the tail:** run this program:

   ```text
   a=1;
   missing_editor_name
   a=99;
   ```

   PASS: failure reported while processing source line2. Console `a` shows1.
   Then replace the editor with `2+2`: it runs and returns4.
5. **Validate before sending:** place `a=99;` before a line longer than191chars
   or containing non-ASCII text. PASS: rejected with line number and `a` stays1.
6. **Unfinished block:** run `for k=1:8` without an end. PASS: incomplete-block
   message, normal `>>` prompt, console2+2 works afterward.
7. **Stop and disconnect:** run the following, then use editor Stop (repeat
   with Esc, then with Disconnect):

   ```text
   a=0;
   while 1
       a=a+1;
   end
   s=999;
   ```

   PASS: prompt/connection can be recovered, trailing `s=999` does not run,
   editor text remains and `s` stays36 from step2. Reconnecting must not resume.
   Completed changes to `a` remain. A native pass does not prove RF watchdog
   responsiveness; report any reset/hang on the actual board.
8. **Repeated use/memory:** run sum/solve/square and repeat Run/Stop/reconnect
   a few times. Capture `:mem` again. PASS: calculations and console remain
   usable, no reset or continuing heap collapse. Distinguish normal retained
   variables/function/block allocations from a persistent leak; report free/
   largest heap, fragmentation, engine used/peak and minimum-free stack.
9. **Independent power:** software reboot, then cold USB-charger startup and
   browser reconnect. PASS: saved home Wi-Fi/key still usable. Editor source
   is kept only in the browser/downloaded files; board workspace clears on boot.

The assistant does not flash the board. Full older L01 hardware checks (including
physical GPIO/ADC) remain separately open. R12 is preserved at
`build/esp8266/history/l01-r12-home-browser-pass/` and
`build/esp8266/history/l01-r12-before-browser-editor/`, SHA256
`73f2416ba78debb7e61bb2603a8ef925e3ba6c20268c9554da9f121fa3e772ad`.
