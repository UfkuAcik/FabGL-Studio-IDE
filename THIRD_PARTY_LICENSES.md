# Third-party software and content

This inventory covers declared build/runtime dependencies and managed tools. A release SBOM
must refine it to the exact files actually shipped. Merely documenting a dependency does not
change its license.

| Component | Pinned version/source | License | Use and distribution requirement |
|---|---|---|---|
| Olimex FabGL fork | `04f328a10573297dd554f13be7f369cdee0f7a2b` (`1.0.9+olimex.04f328a`) | GPL-3.0-or-later | Statically linked into firmware. Distribute complete corresponding source, notices, and build recipe with firmware. |
| Arduino-ESP32 | `2.0.11`, `ae9dae4a6c063d95786d95b6770b70693e132a7d` | Core LGPL-2.1-or-later; bundled package has mixed component licenses | Managed firmware toolchain. Preserve notices and supply relink/source obligations for shipped binaries; audit the package SBOM. |
| Xtensa ESP32 GCC | `esp-2021r2-patch5-8.4.0` | GPL family with GCC Runtime Library Exception plus bundled licenses | External compiler. Offline redistribution must include its license/source notices; generated object code is governed by the runtime exception. |
| Arduino CLI | `1.5.1`, `01f3d4f2ba7c2eaafb5dc710c8a1903af7762fea` | GPL-3.0-only | Separate executable process, not linked into Studio. If redistributed offline, include license and corresponding source offer/bundle. |
| esptool | `4.5.1` | GPL-2.0-or-later | Separate upload tool. Same redistribution obligations apply to offline bundles. |
| Qt Core/Gui/Widgets | `6.8.3` desktop profile | LGPL-3.0-only or commercial/GPL alternatives, module dependent | Dynamically linked desktop UI. Ship LGPL text/notices, permit replacement/relinking, and do not apply anti-reverse-engineering terms. No GPL-only Qt modules are selected. |
| SDL (optional PC adapter) | selected by desktop lock manifest when enabled | zlib | Preserve copyright/license notice in source/binary distributions. |
| CMake | host 3.24+ | BSD-3-Clause | Build tool; not linked. Preserve notices if bundled. |
| Ninja | host 1.10+ | Apache-2.0 | Build tool; not linked. Preserve license/NOTICE if bundled. |
| NSIS | packaging host tool | zlib | Installer generator; generated installer is not forced under NSIS license. Preserve notice if bundling NSIS itself. |
| Olimex ESP32-SBC-FabGL design files | upstream board repository | CERN-OHL-1.2 hardware; GPLv3 software; CC-BY-SA-3.0 documentation | Referenced for pin mapping. No design files are currently vendored. Preserve attribution/share-alike terms if copied. |

Authoritative sources:

- Olimex FabGL license: <https://github.com/OLIMEX/FabGL/blob/04f328a10573297dd554f13be7f369cdee0f7a2b/LICENSE>
- Arduino-ESP32 2.0.11 license inventory: <https://github.com/espressif/arduino-esp32/blob/2.0.11/LICENSE.md>
- Arduino CLI license: <https://github.com/arduino/arduino-cli/blob/v1.5.1/LICENSE.txt>
- esptool repository: <https://github.com/espressif/esptool>
- Qt open-source obligations: <https://www.qt.io/licensing/open-source-lgpl-obligations>
- Olimex board sources: <https://github.com/OLIMEX/ESP32-SBC-FabGL>

FabGL's authors offer commercial licensing separately. A closed-source firmware distribution
must not rely on this GPL build; it requires appropriate licenses from every relevant rights
holder. This file is an engineering compliance inventory, not legal advice.
