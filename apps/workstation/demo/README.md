# Demo — foreground program for the MatmMax launcher

This portable C89 test program proves the first application's lifecycle before
porting WordSmith or other production apps. It is compiled into E02, not loaded
from a program file. It uses only `common/workstation/launcher.h` services and
its **4096-byte arena**, so it does not depend on ESP-IDF or MatmMax's engine.

From MatmMax E02:

```text
>> :app demo hello
Demo program. Text is echoed here, not evaluated.
...
Arguments: hello
demo> x=999
Demo echo: x=999
demo> exit
Returned to MatmMax; workspace kept.
>>
```

The demo echoes printable text, supports backspace/Delete, CR/LF/CRLF, and
discards lines above 127 characters. `exit` returns normally, Esc (or a Ctrl+C
byte if forwarded by the terminal) cancels, and `fail` requests a **controlled
error return** to exercise cleanup. `fail` does not crash the CPU. No text is
evaluated as MatmMax code and nothing is saved between runs.

Native tests cover its input, arguments, editing, overflow and exit statuses;
the ESP32 firmware tests cover console ownership and returning to live math
variables/matrices/functions. Commands and the numbered physical-board procedure
are in [E02's README](../../esp32/e02_launcher/README.md). No physical board or
RP2350 run is claimed for this program.
