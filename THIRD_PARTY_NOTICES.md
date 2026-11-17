# Licensing and third-party software

The root [MIT license](LICENSE) covers this project's original source and
documentation. It does not replace licenses or ownership of third-party
software, downloaded SDKs, tools, fonts, operating systems or firmware.
MathSmith originated in Hugo Moreira's workstation projects; the portable
engine and platform code are included as source, rather than as a proprietary
binary dependency. Existing third-party notices must be retained.

The dedicated `mathsmith-os` export is **source-only**. Its export manifest
records the workstation source commit. SDKs and build products are deliberately
absent: the installers fetch pinned upstream dependencies separately.

| Dependency | Pinned version / source | License information |
|---|---|---|
| ESP8266 Arduino core | [3.1.2](https://github.com/esp8266/Arduino/tree/3.1.2), installed by `tools/setup_esp8266.py` | Core: LGPL 2.1; bundled libraries, SDK and tools have additional licenses. See its [LICENSE](https://github.com/esp8266/Arduino/blob/3.1.2/LICENSE) and [credits](https://github.com/esp8266/Arduino/tree/3.1.2#license-and-credits). |
| ESP8266 compiler | GCC 10.3, esp-quick-toolchain release `3.1.0-gcc10.3`, checksum in installer | GCC GPL and runtime exceptions; other toolchain components retain their notices. |
| ESP-IDF | [v5.5.6](https://github.com/espressif/esp-idf/tree/v5.5.6), commit `880a203ae603d08a94d06aa01e0d6761958e5e9e` | Primarily Apache 2.0; component licenses vary. Preserve the SDK's LICENSE, NOTICE and component notices. |
| Espressif ELF loader | [2.1.0](https://components.espressif.com/components/espressif/elf_loader/versions/2.1.0), installer `tools/setup_elf_loader.sh` | Apache 2.0; downloaded `elf_loader/license.txt`. |
| Espressif QEMU | `esp_develop_9.2.2_20260417`, installer `tools/setup_esp_qemu.sh` | GPL 2.0 with component-specific notices. QEMU is a host emulator, not firmware linked into MathSmith. |
| esptool / pyserial | Installed separately for flashing / serial monitoring | esptool: GPL 2.0; pyserial: BSD. These host tools are not included in the source export. |

## Documentation photographs

The Wemos D1 mini photo by Tpkull and ESP32-S3 development-board photo by
VectorVoyager are **CC BY-SA 4.0**, separate from the project's MIT license.
Their source pages, photographer credits, license links, image versions and
checksums are listed in the [image credits](https://github.com/hgamoreira/mathsmith-os/blob/main/docs/IMAGE_CREDITS.md).
Retain these credits and the images' license when sharing the photographs.

## Binary releases

A locally generated `releases/` folder is a development candidate, not a
complete public redistribution package. Do not describe a linked firmware
image as "MIT only". Before attaching binaries to a public release, prepare
the corresponding source, notices, upstream licenses and rebuild/relink
materials for the actual linked dependencies. The ESP8266 core's
[LGPL 2.1 section 6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html#section6)
sets out the requirements for distributing a linked executable, including
allowing users to modify the library and relink. The build scripts expose
the core compilation, generated linker scripts and final link command.

The source-first release avoids bundling SDK archives, proprietary DOS tools,
personal flash backups or unreviewed binary dependency packages. Original
MIT source can coexist with these dependencies under their respective terms;
this notice does not grant additional rights to upstream software.
