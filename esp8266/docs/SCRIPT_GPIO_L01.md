# L01-r14 — GPIO in MathSmith programs

17 November2026. User-selected next stage after R13 editor Run/Save/Open,
error halt, unfinished-block Stop and active-execution Stop passed on hardware.
R14 adds pin functions and a cooperative millisecond delay. **First hardware
result: four visible flashes and final read1**, with usable connected :mem:
heap15128/largest14312/frag6%, stack minimum-free10808/16384, engine4928/5379
of16384. Pin/LED activity is confirmed. The follow-up resolves the count: **five flashes, printed counts through5, LED off**
on the unchanged R14 firmware. No root cause is assigned to the earlier count.
Other electrical/watchdog/stability tests remain pending.
Exact report (local hardware log omitted; see the validation summary). R13 and
R12 recovery images remain preserved; the network workaround/key format remain.

## Functions

Use these in either the console or the browser Program editor. Pin/mode strings
may use the MathSmith language's supported quotes. Setters and delay return
numeric0 on success; use a semicolon to suppress that result.

| Function | Meaning |
|---|---|
| `pinmode('D4','output',1)` | Set digital output with initial HIGH level. |
| `pinmode('D4','output')` | Set output initially LOW. |
| `pinmode('D2','input')` | Set ordinary input. |
| `pinmode('D2','pullup')` | Set input with internal pullup; D0 rejects this. |
| `digitalwrite('D4',0)` | Write LOW to an already configured output. |
| `digitalread('D4')` | Return current digital level as numeric0 or1. |
| `analogread('A0')` or `analogread()` | Return raw ADC count0..1023. |
| `delay(250)` | Wait250ms, continuing to service Wi-Fi and cancellation. |

Digital pins accept **D0 through D8 labels only**, matching `:pins`. Raw GPIO
numbers, TX/RX and flash pins are not exposed. Levels must be finite scalar
integers0/1. Delay accepts finite integer milliseconds0..60000; use loops for
longer schedules. Strings/vectors/NaN/Inf/fractional/out-of-range values fail
without performing that operation. An initial level is accepted only for
output mode. Argument expressions may have their own completed side effects;
there is no rollback. Return storage is reserved before the hardware operation.

Console `:pin`/`:write`/`:read` and these functions share configuration. For
example, `pinmode('D1','output',0);` permits later console `:write D1 1`, and
`:pin D1 output 0` permits later `digitalwrite('D1',1)`. `:reset`, Stop, errors
and browser disconnect do not undo completed pin writes or reset pin modes.
The last output can remain latched. Power loss/reboot resets hardware state;
there is no saved pin setup. Future peripheral ownership is not implemented.

## Blink the board LED

With external circuitry removed from D4, open staged `blink.ms` or enter:

```text
pinmode('D4','output',1);
for k=1:5
    digitalwrite('D4',0);
    delay(250);
    digitalwrite('D4',1);
    delay(250);
end
digitalread('D4')
```

The standard D1 mini LED is on D4/GPIO2 and active low:0 on,1 off. Expected:
five visible blinks, final read1 and Program finished. A clone may have a
different LED mapping; report observed behavior instead of assuming success.
D4 is also a boot-strap pin; avoid external circuitry holding it LOW at reset.

The board uses3.3V digital signals. Do not connect5V directly. A0 board dividers
vary; do not apply an external analog voltage until this module's input rating
is verified. Reading floating A0 needs no external voltage and has no fixed
expected count. The current manufacturer page describes the standard module,
not proof of the exact user's board revision or clone:
[LOLIN D1 mini documentation](https://www.wemos.cc/en/latest/d1/d1_mini.html).

## Numbered blink-count retest

For the reported four-versus-five observation, use staged `blink_count.ms` on
the same R14 firmware. It holds the LED off for1second, prints1through5 before
each LOW/HIGH pair, and lengthens each on/off phase to500ms. Require both the
printed1..5 and five visible flashes, final read1, and report whether the LED
ends on/off. Do not mark a count PASS from readback alone. The native trace
verifies five issued pairs for the original program. The user now confirms
five visible flashes, counts through5 and final OFF in the numbered board test:
**blink-count PASS**. No reflash or root-cause assumption is needed.

## Stop during a delay

`delay` yields in slices of at most5ms and calls the existing poll path for
network/Stop/serial recovery. It never recursively evaluates another command.
Delays are cooperative minimum waits, not precision timing or a hard real-time
scheduler; network activity and other SDK work can extend elapsed time.
For example, run `delay(60000);` and click Stop: it should interrupt promptly,
not require waiting a minute. Native tests verify cancellation and recovery,
but radio/watchdog responsiveness needs the physical board test.

When stopping a blink loop, the LED may stay on or off according to its last
write. Explicitly use `digitalwrite('D4',1);` afterward to turn it off.

## Hardware test — user PASS/FAIL

1. **Boot/access:** close the monitor with Ctrl+], flash and reconnect through
   the usual browser URL/key; fully reload the page after flashing:

   ```sh
   esp8266/scripts/webrepl.sh flash /dev/ttyUSB0
   ```

   PASS: :info shows L01-r14, the home-network browser works,2+2 gives4.
   Capture connected `:mem` before GPIO work.
2. **Physical LED:** run blink.ms above. PASS: five visible blinks, readback1,
   normal prompt. Describe a missing/inverted/differently mapped LED as observed.
   Readback alone does not establish physical blinking.
3. **Shared configuration:** console `:write D4 0` should turn LED on after the
   program's output setup. `digitalread('D4')` returns0. Then
   `digitalwrite('D4',1);` turns it off; `:read D4` returns1. PASS: both interfaces
   control the same state. No external wiring is needed.
4. **Delay Stop/tail halt:** run this editor program, wait for the delay, Stop:

   ```text
   a=1;
   digitalwrite('D4',0);
   delay(60000);
   a=999;
   ```

   PASS: prompt/cancellation returns promptly, console a is1 and2+2 gives4.
   The completed LOW write remains; turn LED off explicitly with
   `digitalwrite('D4',1);`. Record any watchdog reset, hang or lost Wi-Fi.
5. **Argument/policy errors:** try `digitalwrite('D4',2)`, `digitalread('TX')`,
   `pinmode('D0','pullup')`, `delay(-1)` and `delay(60001)`. PASS: clear errors,
   usable console and no unintended D4 change. No external pin hookup needed.
6. **ADC read:** `v=analogread();`, then `v`. PASS: numeric raw count0..1023,
   no specific value required for floating input. Do not connect an unverified
   external voltage. This is not a calibrated ADC accuracy test.
7. **Optional input loopback:** only if D1/D2 have no other circuitry, jumper
   D1 to D2 and run:

   ```text
   pinmode('D2','input');
   pinmode('D1','output',0);
   digitalread('D2')
   digitalwrite('D1',1);
   digitalread('D2')
   pinmode('D1','input');
   ```

   PASS: D2 follows0 then1. Never join two outputs; remove the jumper afterward.
   Report NOT RUN separately if no external-input test is desired.
8. **Repeated editor/math/network use:** repeat blink/Stop, Save/Open/Run and
   ordinary sum/solve programs; capture `:mem` again. PASS: math and browser
   remain usable without reset, continuing heap collapse or rising stack use.
   Reboot/cold USB-charger startup should retain network/key, not pin setup.
   Physical RAM/stack/time margins are not inferred from native tests.

## Implementation and recovery

One `pins.c` registry backs both frontends. Counted profile anchors insert the
L01-only `gpio_engine.h` definitions and builtin table entries into generated
engine.c. Shared validated engine/DOS/Linux sources are untouched. Native
GPIO/ADC is a stub; tests do not simulate electrical levels or RF behavior.

R13 exact recovery bin/ELF/map/config/source/profile/build script:
`build/esp8266/history/l01-r13-before-script-gpio/`,465120bytes SHA256
`327364b230c79118537c471d95769d363769834c882ae0c6005bab61688dcf24`.
R12 home-network recovery also remains. The assistant does not open/flash the board.
