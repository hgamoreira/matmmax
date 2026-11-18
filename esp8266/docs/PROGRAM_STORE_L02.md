# L02 — programs saved on the board (XD118)

18 November 2026. MatmMax Light on the Wemos D1 mini keeps programs in its
own flash. You can list, show, delete and run them like files on a disk. The
browser editor saves programs to the board and opens them from it. Nothing
runs by itself at power-up: there is deliberately no boot script (XD118).

**L02-r2 (same day) only renames the board's network identity to MatmMax:** `matmmax.local`, the `matmmax.TOKEN` handshake tag, the browser's `matmmax.access-key` slot and the `MatmMax access key` serial line. The old `mathsmith` names are refused, not aliased.

**Status: built and tested off the board; not hardware-validated.** Native
tests use a folder in place of LittleFS. They do not measure flash timing,
wear, heap, stack or behaviour on power loss. The procedure at the end is
what closes this step.

## Using it

| Where | Input | Effect |
|---|---|---|
| Console | `ls` or `dir` | Saved programs, sorted, with sizes and store use. |
| Console | `run NAME` or just `NAME` | Runs `NAME.m`, the same as the DOS and Linux builds. |
| Console | `type NAME` | Shows the program text. |
| Console | `rm NAME` or `delete NAME` | Deletes `NAME.m`. |
| Console | `:store` | Store status, size and limits. |
| Console | `:store format confirm` | Erases every saved program. `:store format` alone only warns. |
| Browser | **Save to board** | Saves the editor as `NAME.m`, taking NAME from the file-name field. |
| Browser | **Open from board** | Loads the selected board program into the editor. |
| Browser | **Run on board** | Runs the selected program (`run NAME`). |

- A file that starts with `function` defines functions. Calling `sq(3)` loads
  `sq.m`, the same as on DOS. Saving or deleting a file drops any cached
  copy, so a replaced function takes effect at once.
- The console commands work over serial and in the browser console. Saving
  needs the browser; serial has no editor.
- If a variable is called `ls`, `dir`, `type`, `rm` or `delete`, that word means
  the variable, not the command. Inside an open block (`..>`) these words are
  ordinary block text.

**Names and limits.** A name is a letter, then letters, digits or `_`, at
most 24 characters. Case is ignored: `Blink` and `blink` are the same
program. A program is printable ASCII lines and at most 16384 bytes. A
running program must also fit the engine: at most 128 statements and the
16 KiB engine payload. A file can be saved and still be too large to run;
`run` then reports an out-of-memory or statement-count error. The editor's
line-by-line **Run program** has no statement-count limit.

**Saving is atomic.** The board writes `save.tmp`, checks its size, and only
then renames it over `NAME.m`. LittleFS replaces the old file in one step. A
failed save, a Stop, a console command or a disconnect part-way removes the
temporary file and leaves the previous `NAME.m` unchanged. A power cut
during a save is not tested.

## Flash layout and first start

| Physical range | Use |
|---|---|
| `0x000000` onward | Firmware image (L02-r2: 507,920 bytes). |
| `0x300000..0x3F9FFF` | **Program store**: LittleFS, 1000 KiB, 125 blocks of 8 KiB, 256-byte pages. |
| `0x3FA000`, `0x3FB000` | Wi-Fi settings slots 0 and 1, unchanged (XD113). |
| `0x3FC000..0x3FFFFF` | SDK RF calibration/system sectors, unchanged. |

This is the core's own `eagle.flash.4m1m.ld` layout, used unmodified; the
build checks its values. The store ends exactly where settings slot 0
begins. The settings and store code each refuse to run unless the linked
addresses and the 4 MB flash size match.

On boot the store is mounted, and it is **never formatted silently**. If
both superblock blocks (`0x300000..0x303FFF`) read entirely `0xFF`, the
flash is erased: the board formats an empty store and says so. Anything
else that does not mount prints `Program store: not readable`, and only
`:store format confirm` erases it. Flashing the firmware writes from offset
0 and leaves the store and Wi-Fi settings in place. A full-flash restore
from a backup replaces the store too.

## Browser frames (additions to WEB_PROTOCOL)

Browser to board, each answered by one `R` frame (result 0 ok, 1 error,
2 cancelled). Errors also print one `error: ...` line in the console.

| Frame | Meaning |
|---|---|
| `L` | List: `F` frames carry `name size\n` lines, sorted. |
| `G` + name | Read: `F` frames carry the file's bytes. |
| `B` + name | Begin a save (validates the name, opens `save.tmp`). |
| `D` + text | Up to 191 bytes of program text; only here may `\n` (byte 10) appear. |
| `E` | End: verify, then rename into place. |

`F` frames (board to browser) use the existing 128-byte frame size. The
output ring is drained before and after a read, so console `O` bytes and
file `F` bytes never share a frame. Unknown kinds, or `L`/`E` with a
payload, close the connection with 1008, as malformed `C` frames already did.

## Measured cost (linked image, not runtime)

| | L01-r14 | L02-r2 |
|---|---:|---:|
| Image | 467,376 | 507,920 |
| Static DRAM (`.data`+`.rodata`+`.bss`) | 56,404 | 57,064 (+660) |
| IRAM code (`.text`+`.text1`) | 28,212 | 28,276 (+64; 4,492 left) |

`store.c` constants are placed in mapped flash like the console's. LittleFS
uses 64-byte read, program and lookahead caches. Per open file, the engine's
read stream adds a 128-byte C-library buffer, held only while the script's
text is read (it is closed before the program runs). Heap and stack while
saving and running are **not yet measured on the board**.

## Off-board tests

`esp8266/scripts/webrepl.sh test` (100 tests; 11 in `tests_web/test_store.py`)
runs the real firmware loop natively. The store tests cover:

- erased versus unreadable flash at boot, and format
- save, list, open, `run` and bare-name runs
- a 9,000-byte round trip
- replacing a function file, and the cache being dropped
- sorting and name rules
- variables shadowing the commands
- size limit, store full, rename failure, disconnect, Stop and a console command during a save
- frame validation and the layout check

The served page was also driven in headless Chrome (2026-11-18) against the
native board. Every flow passed with no page errors:

- list on connect
- Save to board, then Run on board
- Open from board
- an 8,780-byte save and reopen
- bad-name refusal
- the list refreshing after `rm`

L00's image is byte-identical to before (`ba8f87e0…`). The shared engine
sources are not edited; the L02 profile adds its hooks in the generated copy.

## Hardware-test procedure — report PASS/FAIL per step

Flashing writes only the firmware. Saved Wi-Fi stays. The store range may
hold old data from before this firmware, so read step 1's message.

1. **Flash and first boot.** `esp8266/scripts/webrepl.sh flash /dev/ttyUSB0`, then
   `monitor`. PASS: `MatmMax Light L02-r2` and one store line, either
   `erased flash found; formatted an empty store` or
   `Program store ready`. If it says `not readable`, type
   `:store format confirm` and record that you did. Wi-Fi reconnects as before.
2. **Status and memory.** `:store`, then `:mem`. PASS: `0 programs, ... of 1000 KiB
   used`. Record free heap, largest block and minimum free stack.
3. **Save from the browser.** Connect. File name `sumup`, editor text
   `s=0;`, `for k=1:8`, `    s=s+k;`, `end`, `s` (one per line).
   Click **Save to board**. PASS: `Saved sumup.m on the board (32 bytes)`,
   and the list shows `sumup (32 bytes)`.
4. **Run it three ways.** Click **Run on board**, then type `run sumup` and
   `sumup` in the console. PASS: `s = 36` each time. Also type `ls` and
   `type sumup`.
5. **Function file.** Save `sq` containing `function y=sq(x)`, `  y=x^2;`,
   `end`. Type `sq(7)`: 49. Change `^2` to `^3`, save again (accept Replace),
   then `sq(7)`: 343.
6. **Persistence.** `:reboot`; after Wi-Fi returns, `ls` and `sumup`. Then
   remove power, power from a USB charger, reconnect the browser and click
   **Open from board** on `sumup`. PASS: both programs listed, `s = 36`, and
   the text identical.
7. **Bigger program and timing.** Open `examples/solve.ms` from the computer.
   Repeat its lines (copy and paste) until the editor holds about 8,000 bytes.
   Save to board as `big` and time the save. PASS: saved, and Open from board
   shows the same text. Record the seconds taken.
8. **Delete and errors.** `rm big`, `ls`, `run big` (cannot find the file),
   `type nothing`, and a save named `9lives`, which the page refuses. PASS:
   messages as described, and the board stays responsive.
9. **Interrupted save.** Paste the large text again, click **Save to board**,
   and click **Stop** while it is saving. PASS: `Not saved`. `ls` shows no
   `big` and no `save.tmp`, and `run sumup` still works.
10. **Memory after use.** `:mem`. PASS: no crash or reset during the steps.
    Record heap, largest block and minimum stack, to compare with step 2.

Steps 1–6 alone validate the basic store. Steps 7–10 measure its margins.
Report each step as PASS or FAIL, with the recorded numbers.
