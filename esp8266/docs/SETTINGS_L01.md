# L01-r6 saved Wi-Fi settings

17 November2026: the user confirms R12 home Wi-Fi/browser access without serial
USB to this PC. This establishes independent browser access; this sentence alone
does not separately measure repeated cold boot or unchanged browser key.
A follow-up confirms one command-line software reboot followed by browser
reconnection on the home network. A further "also worked" confirms the requested
complete power removal, USB-charger startup and browser reconnection. Exact
key identity and repeated-cycle stability were not separately measured.
[R12 hardware result](DHCP_BROADCAST_L01.md).


R7/R8/R9/R10/R11/R12 retain this record format, sectors and policy. `:wifi diag` performs no
settings writes; no saved-network re-entry is required merely for the upgrade.
R6 save, software reboot/key retention and cold standalone browser tests pass
on the hotspot. R9 still has an active selecting client without a home IP;
R10/R11 add packet observation; R12 requests pre-address broadcast replies. Neither changes persistence. Link-local AutoIP
alone does not trigger save.

16 November 2026. Implements XD113. Native fault/restart tests pass; physical
flash persistence and mDNS discovery still require the user's hardware test.
See [everyday setup](USER_MANUAL_L01.md) and the
[numbered hardware procedure](../../apps/esp8266/l01_webrepl/README.md).

## Behaviour

Serial `:wifi join` stages a replacement. Save only after a fresh association
event matches the requested SSID, a fresh IP event arrives and the address is
usable. R6 cold startup uses `mode(STA)`/`begin`, as in working R4. Replacing an
active attempt first turns the interface off, yields briefly and enables it
again, so repeated joins obtain fresh events. It removes R5's unconditional
disconnect immediately after enabling STA. SDK reconnect policy is requested
after readiness. R8 replaces the earlier stop/config restart: after15seconds,
inspect the real client and start it at most once if OFF/no PCB on an up link.
Active DHCP is left running. Save criteria stay intact.
A failed or incomplete join leaves the previous committed settings
available for the next boot. Repeating identical settings does not write flash.
On boot, restore SSID/password and browser key, then join without serial input.
Boot loading never saves again. SDK automatic reconnect is enabled; actual
radio recovery still needs hardware testing. `WiFi.persistent(false)` remains
in force: the application owns this record instead of SDK credential storage.

Serial `:wifi off` stops networking for this session and retains saved settings.
Serial `:wifi forget` commits a deletion record before erasing the previous live
record, disables networking and rotates the current key. An interrupted old-slot
cleanup cannot reactivate the older credentials. If cleanup fails, the command
reports it and asks for another `:wifi forget`. A failure before the deletion
commit reports that the old saved network may remain. Configure again with
`:wifi join`. These commands never save MathSmith variables, functions or pins.

Passwords and access keys are **unencrypted on flash**. A backup of the board
can contain them, including an older record retained for interrupted-write
recovery. Successful forgetting erases the previous live record. Network changes
keep the browser key; forgetting replaces it. The browser's optional localStorage
entry is separate: uncheck **Remember key on this browser** to remove that copy.

## Flash layout

Target: ESP8266EX with exactly 4 MB flash. Physical offsets:

| Range | Use |
|---|---|
| `0x000000` onward | Linked firmware image; current image is below 1 MB. |
| `0x3fa000..0x3fafff` | Settings slot 0, independently erased 4 KiB sector. |
| `0x3fb000..0x3fbfff` | Settings slot 1, former unused EEPROM sector. |
| `0x3fc000..0x3fffff` | Core RF calibration / SDK system sectors, unchanged. |

The generated L01 linker script sets `_FS_start == _FS_end == 0x405fa000`
(physical `0x3fa000`) and leaves `_EEPROM_start == 0x405fb000`. Empty filesystem
and temporary-sketch bounds exclude both sectors. No filesystem is mounted and
no EEPROM or OTA API is active. Future filesystem/EEPROM/OTA work must revise
this reservation explicitly. The target raw adapter refuses reads/writes unless
these linker addresses and the actual flash size match this layout. L00 and the
Wi-Fi probe retain their original linker layouts.

Each slot starts with a 160-byte record. All multibyte fields are little endian.
The rest of the sector is unused.

| Offset | Bytes | Encoding |
|---:|---:|---|
| 0 | 4 | Magic `MSF5`. |
| 4 | 1 | Format version 1. |
| 5, 6 | 1 each | SSID and password lengths, excluding NUL. |
| 7 | 1 | Deletion flag: 0 live, 1 tombstone. |
| 8 | 4 | Generation counter, compared with wraparound. |
| 12 | 33 | NUL-terminated SSID, 1..32 printable ASCII characters. |
| 45 | 64 | NUL-terminated password, 0..63 printable ASCII characters. |
| 109 | 33 | NUL-terminated 32 lowercase hex digits, browser key. |
| 142 | 10 | Zero padding. |
| 152 | 4 | CRC32/IEEE over bytes 0..151. |
| 156 | 4 | Commit word `0x4d534f4b`, written last. |

A tombstone has zero lengths and zero credential/key fields. Loading validates
magic, version, lengths, character bounds, checksum and commit word, then picks
the newest valid generation. If the newest slot is corrupt, an older valid
record can be used; a valid newer tombstone deliberately suppresses it. If
both are invalid/erased, boot stays in serial setup mode. Read errors are
reported and block overwriting settings based on an uncertain previous state.

Saving erases the inactive sector, writes bytes 0..155, writes the final commit
word and reads back the complete record for verification. The previous slot
remains intact. Flash operations run in the outer firmware loop, never inside
Wi-Fi callbacks or recursive engine evaluation. They use aligned buffers and
the core's flash API.

## Off-board checks and their limits

`webrepl.sh test` includes the actual C record implementation with simulated
flash callbacks. It checks every interrupted programming-byte boundary,
partial erases, corrupt records, read/write failures, generation wrap, deletion
cleanup and zero writes for unchanged settings. CRC is checked against Python
`zlib`. These are persistence-logic checks, not measurements of real power loss.

The POSIX transport harness supports `MS_SETTINGS_FILE=/path/to/test-records`.
It uses two 160-byte slots in that temporary test file, with flushed writes,
to test process restart, retained authentication and automatic join without
serial setup. `MS_NATIVE_WIFI=0` disables the fixture's forced initial join;
saved-settings boot still runs. `MS_TEST_SETTINGS_FAIL=1` injects save failure.
These are developer fixtures and do not contain the user's network credentials.

Real Chrome checks exercise optional key storage, removal, page reload and a
native-device process restart. They map `mathsmith.local` to localhost; they do
not prove physical multicast DNS discovery. R5 adds mDNS runtime allocations,
so obtain a fresh browser-connected `:mem` report on the board.

Recovery uses the pinned core's `WiFi.config(0U,0U,0U)` DHCP path after
`wifi_station_dhcpc_stop()`. See the
[core station implementation](https://github.com/esp8266/Arduino/blob/3.1.2/libraries/ESP8266WiFi/src/ESP8266WiFiSTA.cpp)
and [station documentation](https://arduino-esp8266.readthedocs.io/en/3.1.2/esp8266wifi/station-class.html#config).
R5's association-without-address report motivates this recovery; it does not
prove that DHCP was stopped or that the router/core is at fault. Native delayed
IP, failed recovery and once-only tests check the policy, not real DHCP packets.
