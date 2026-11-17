# MatmMax working rules

Read `PROGRESS.md` first. Work on one agreed milestone at a time, with the
existing MatmMax engine as the behavior reference. Keep platform services
separate from portable logic. Do not rewrite validated code for style.

Test behavior on the host and in the applicable emulator before requesting a
board test. An emulator/native pass is not a hardware pass. Record only the
hardware results explicitly reported by the user; retain open checks.

Keep decisions, commands, limits and numbered hardware procedures in the
repository. Use one clean commit per working milestone step. Downloaded SDKs,
generated firmware, personal flash backups, Wi-Fi passwords and browser access
keys must stay out of Git. Preserve third-party notices and the pinned tools.

`apps/m01_mathsmith/` is the shared mathematical engine, retained at its original
path for reproducible builds. `esp32/` and `esp8266/` hold platform scripts,
tests and manuals; `website/` contains the static project site. The RP2350
folder is a draft, not a validated target.
