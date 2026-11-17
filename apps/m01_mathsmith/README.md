# Shared MatmMax engine

Portable numerical engine retained at its original workstation path.
ESP32-S3 compiles these sources directly; ESP8266 generates a bounded
profile without modifying this source. See the platform manuals for
supported functions and limits. `repl.c` and `tests/golden.txt` provide
the host reference used by the ESP32-S3 regression suite.
