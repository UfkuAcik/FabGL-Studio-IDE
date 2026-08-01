# ESP32/FabGL toolchain

The release profile is a reproducible, repository-scoped Arduino build for the
Olimex ESP32-SBC-FabGL Rev B. The machine-wide Arduino installation is never
silently upgraded or modified by the managed workflow.

The machine-readable authority is [`toolchains/manifest.json`](toolchains/manifest.json).
This document explains the decisions and operating procedure; duplicated values
below are informational and must not override the manifest.

## Locked release profile

| Component | Lock |
| --- | --- |
| Board | Olimex ESP32-SBC-FabGL Rev B, `ESP32 Dev Module` |
| Arduino-ESP32 | `2.0.11`, commit `ae9dae4a6c063d95786d95b6770b70693e132a7d` |
| FabGL | Olimex fork, library `1.0.9`, commit `04f328a10573297dd554f13be7f369cdee0f7a2b` |
| Distribution label | `1.0.9+olimex.04f328a` |
| Arduino CLI | `1.5.1`, commit `01f3d4f2ba7c2eaafb5dc710c8a1903af7762fea` |
| Partition | Huge APP: 3 MiB application, 1 MiB SPIFFS, no OTA |
| PSRAM | Disabled reference profile; enabled only as a separately tested profile |
| Vendor compatibility | Arduino warnings `default`; C++ extra flag `-Wno-error=narrowing` |

Olimex explicitly documents Arduino-ESP32 2.x compatibility, incompatibility
with 3.x, and `2.0.11` as its used version. Arduino-ESP32 `2.0.17` is an upgrade
candidate, not an automatic patch update. It must pass compile, binary-budget,
and hardware-in-the-loop tests before an ADR changes the lock.

The exact FQBN is:

```text
esp32:esp32:esp32:UploadSpeed=921600,CPUFreq=240,FlashFreq=40,FlashMode=dio,FlashSize=4M,PartitionScheme=huge_app,DebugLevel=none,PSRAM=disabled
```

The board physically has 8 MiB PSRAM. `PSRAM=disabled` follows the Olimex/FabGL
reference setup and avoids changing allocator/compiler behavior unnoticed. Use
`scripts/build_esp32.ps1 -EnablePsram` only for the explicit experimental
profile described in `HARDWARE_TESTING.md`.

The exact Olimex commit adds optional CH32V003 drivers containing five C++11
list-initialization narrowing diagnostics. Arduino-ESP32's `all` warning preset
turns those vendor diagnostics into errors and also promotes unrelated optional
driver warnings. The manifest therefore locks Arduino's normal `default`
warning preset and the narrow `-Wno-error=narrowing` compatibility flag. The
vendor archive remains byte-for-byte checksummed and unpatched; project-owned
native targets keep their strict warning policy.

## Secure bootstrap

On Windows x86-64, from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_toolchain.ps1
```

The bootstrap:

1. confines cache and installations to `.downloads/` and `.toolchains/`;
2. performs a drive-space preflight before downloading or extracting;
3. downloads to a `.part` file and never promotes an incomplete transfer;
4. verifies the exact byte count and SHA-256 from the lock;
5. rejects absolute paths, `..` traversal, drive-qualified entries, paths that
   escape staging, and ZIP symbolic links;
6. holds the verified archive open without file sharing during validation and
   extraction;
7. extracts to a unique staging directory and moves it into its versioned
   destination only after success and marker creation;
8. writes an isolated Arduino CLI configuration with unsafe installs disabled;
9. installs exactly `esp32:esp32@2.0.11` through the official Espressif index;
10. never discovers a board, opens a serial port, erases flash, or uploads.

A clean source-only bootstrap requires at least 4 GiB free. Provisioning the
repo-scoped Board Manager core and all of its declared compiler/debug/tool
dependencies requires at least 8 GiB. A cache-hit/marker-hit validation needs
only a small working reserve.

To verify and install the locked CLI and sources without installing another
copy of the Board Manager core:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_toolchain.ps1 -SkipBoardManagerInstall
```

This source-only mode is useful for auditing and on machines where an existing
2.0.11 installation is used for a non-release compatibility build. It does not
make the managed profile release-ready.

### Offline files

Place files under their exact manifest `fileName` values in one directory, then
run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_toolchain.ps1 `
  -OfflineSourceDirectory D:\approved-fabgl-toolchain `
  -SkipBoardManagerInstall
```

Local files receive the same size and SHA-256 checks as downloads. Supplying an
offline directory disables network fallback. A fully offline Board Manager
installation additionally needs an approved, already populated
`.toolchains/arduino-data` bundle; if it is absent, the script stops rather than
silently contacting the network. Release engineering should mirror and sign
the exact GitHub-generated FabGL archive because its checksum was measured by
the project rather than published by Olimex.

## Compile-only firmware build

With a fully managed bootstrap:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 -Clean
```

For an explicitly non-release check using the user's installed core and FabGL:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 `
  -UseSystemToolchain -Clean
```

The build script checks Arduino-ESP32 `2.0.11`, FabGL `1.0.9`, the FQBN, and the
managed FabGL commit marker when applicable. It invokes Arduino CLI with a
PowerShell argument array; paths are never concatenated into a shell command.
It rejects `upload`, `--upload`, and `-u` internally. Output is written under
`out/esp32/`, including `build-result.json` with hashes, sizes, compiler
contract, CLI/config paths, and an explicit `uploadPerformed=false` record.

The exact-fork integration build performed on 2026-08-01 used the locked
Arduino CLI `1.5.1`, installed Arduino-ESP32 `2.0.11`, and the checksummed
Olimex FabGL commit. It passed:

```text
Program reported by Arduino CLI: 448,757 bytes / 3,145,728 bytes (14%)
Primary firmware.ino.bin:        449,120 bytes
Global variables:                 25,920 bytes / 327,680 bytes (7%)
Remaining dynamic memory:        301,760 bytes
Primary binary SHA-256:          87137e737da22ad4e686a7974f8ac35edae881e58c1944c3f0ff794a5ab08a56
```

The selected core came from the user's Arduino data directory, so this is an
exact-source integration result rather than a release attestation. A release
build must report `toolchainMode=managed` and `fabglCommitVerified=true`.

## Explicit upload and serial monitor

Compilation cannot upload. The separate uploader accepts no free-form firmware
path: it reads only the binary path, byte count, and SHA-256 from a successful
`build-result.json`, re-hashes the file, revalidates the locked CLI/core/FabGL,
compiler contract, profile, FQBN, and PSRAM label, and requires a human-selected
port plus the exact board confirmation token. Preview the argv without touching
hardware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM9 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun
```

Remove `-DryRun` only after completing `HARDWARE_TESTING.md`'s safety gate. A
successful upload writes `upload-result.json` beside the build result. No build
or bootstrap command calls this uploader.

Serial monitoring is independently explicit and requires both port and baud:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/serial_monitor.ps1 `
  -Port COM9 -Baud 115200 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
```

Its argument model contains `monitor` only and rejects any upload operation.

## C++ command model

`fabgl_toolchain_manager` loads and validates the manifest, detects managed or
explicit inputs, and produces a `program` plus `arguments[]` model. It does not
execute the rendered diagnostic string and has no upload command builder.

```powershell
out\build\dev\tools\toolchain_manager\fabgl_toolchain_manager.exe inspect `
  --manifest toolchains\manifest.json --repo .
```

```powershell
out\build\dev\tools\toolchain_manager\fabgl_toolchain_manager.exe compile-command `
  --manifest toolchains\manifest.json --repo . `
  --sketch platforms\fabgl\firmware --build out\esp32\build --output out\esp32\bin
```

Tests assert the exact core/FabGL/FQBN/pins, validate each lock field, preserve a
path containing spaces and shell metacharacters as one argument, and reject any
compile model containing an upload operation.

## Pin-specific build constraints

The Rev B hardware pins are locked in the manifest and mirrored in
`platforms/fabgl/firmware/BoardProfile.h`. In particular, microSD is HSPI
`MISO=35, MOSI=12, CLK=14, CS=13`. FabGL's generic `FileBrowser::mountSDCard`
defaults use MISO 16 and MOSI 17 and are wrong for this board. Target code must
always pass the board pins explicitly or initialize `SPIClass(HSPI)` itself.

VGA occupies GPIO 23/19/18/5, which overlap the usual VSPI bus, and GPIO 15/4,
which are commonly used for I2C. These functions cannot be enabled on their
generic defaults alongside VGA.

## Licensing and distribution

- The locked FabGL fork is `GPL-3.0-or-later`. Firmware linked with it must ship
  complete corresponding source, build material, licenses, and notices. Closed
  firmware requires appropriate commercial licensing from the relevant FabGL
  and Olimex rightsholders.
- Arduino-ESP32's main core is `LGPL-2.1-or-later`, while its package contains
  mixed-license dependencies. Preserve the licenses for the exact bundle.
- Arduino CLI is `GPL-3.0-only` and remains a separate process. First-run
  download avoids bundling it in the Studio installer; an offline redistribution
  needs its license, notices, and corresponding source obligations.
- Board hardware is CERN-OHL-1.2; Olimex repository software is GPLv3 and its
  documentation is CC-BY-SA-3.0.

See `THIRD_PARTY_LICENSES.md` and obtain legal review before distributing a
firmware or offline toolchain bundle.
