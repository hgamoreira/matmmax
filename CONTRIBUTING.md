# Contributing

Use small changes with a reproducible example and the affected target named.
Keep numerical behavior consistent across platforms. Include the relevant
native/emulator checks and distinguish these from physical hardware reports.
The current validation record is `PROGRESS.md`.

The setup/build commands are in `docs/GETTING_STARTED.md` in the dedicated
MathSmith OS export. In the workstation repository, use
`sharing/GETTING_STARTED.md` instead. After building the relevant candidates:

```sh
esp8266/scripts/webrepl.sh test
esp8266/scripts/mathsmith.sh test
python3 esp8266/scripts/wifi_probe.py test
esp32/scripts/external.sh test
python3 esp32/scripts/test_elf_upstream.py
python3 -m unittest discover -s website/tests -v
```

The full firmware suites require their pinned SDK/compiler, built images and
permission to use local sockets. Optional real Chrome checks use
`esp8266/scripts/check_browser.js` with an existing Playwright installation;
`PLAYWRIGHT_NODE_MODULE` and `CHROME` can override local paths. They exercise
the native HAL, not the physical Wi-Fi radio.

Report hardware PASS/FAIL against each app's numbered procedure, with the
build identifier and relevant `:mem` output. Omit passwords and access keys.
Do not attach personal flash dumps: they can contain saved network settings.
Original contributions use the project's MIT license; preserve upstream
licenses and notices for third-party code.
