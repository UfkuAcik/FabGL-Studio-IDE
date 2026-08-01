# ADR-0004: Pin Arduino-ESP32 2.0.11 and the Olimex FabGL fork

- **Status:** Accepted
- **Date:** 2026-08-01

## Context

FabGL accesses timing-sensitive ESP32 peripherals and is not source-compatible
with arbitrary Arduino-ESP32 releases. Olimex documents FabGL as compatible
with Espressif core 2.x, not 3.x, and identifies 2.0.11 as its used release.
The Studio must build reproducibly without modifying or silently upgrading a
developer's global Arduino environment. Build execution also accepts project
paths that may contain spaces or shell metacharacters.

The target is specifically Olimex ESP32-SBC-FabGL Rev B. Generic FabGL defaults
are insufficient because the board's microSD MISO/MOSI pins differ from
`FileBrowser::mountSDCard` defaults.

## Options considered

1. Track the newest Arduino-ESP32 and upstream FabGL releases.
2. Use an unconstrained PlatformIO environment and resolve versions at build time.
3. Pin Arduino CLI, Arduino-ESP32, and the Olimex FabGL fork in a checksummed,
   repository-scoped profile with compile and upload as separate operations.

## Decision

Choose option 3:

- Arduino CLI 1.5.1;
- Arduino-ESP32 2.0.11 at commit
  `ae9dae4a6c063d95786d95b6770b70693e132a7d`;
- Olimex FabGL fork at commit
  `04f328a10573297dd554f13be7f369cdee0f7a2b`, labelled
  `1.0.9+olimex.04f328a`;
- `ESP32 Dev Module`, Huge APP, 4 MiB flash, 240 MHz CPU, DIO/40 MHz flash,
  reference PSRAM disabled, upload speed 921600 with a documented 115200
  fallback;
- exact Rev B pins in the manifest and firmware, including HSPI SD
  `MISO=35, MOSI=12, CLK=14, CS=13`;
- application-owned `.downloads` and `.toolchains` roots with locked URL, byte
  count, SHA-256, source revision, license, and source URL;
- no automatic upload. Compilation never selects a port or performs board
  discovery, erase, flash, or reset;
- process execution as a program plus argument vector. A rendered command is
  diagnostic text only and is never passed to a shell.
- Arduino's `default` warning preset plus the targeted
  `-Wno-error=narrowing` C++ flag. This preserves the exact locked Olimex
  archive while allowing its optional CH32V003 driver sources, which contain
  five C++11 narrowing initializers, to compile with Arduino-ESP32 2.0.11.

The managed bootstrap rejects unsafe ZIP paths and symbolic links, uses a
verified `.part` promotion, stages extraction, and atomically installs a
versioned payload. Offline local files receive identical integrity checks and
never fall back to the network.

## Rationale

This is the closest reproducible automation of the board vendor's documented
Arduino recipe. It avoids known 3.x API/build-system breaks while preserving a
clear upgrade path. The explicit argv model prevents quoting and injection bugs
for ordinary project paths. Separating upload ensures a successful build cannot
modify hardware whose identity has not been established.

## Consequences

### Positive

- Release builds have attributable source and binary inputs.
- User/global Arduino packages are not silently mutated.
- Bad downloads and traversal archives cannot become executable installs.
- The smoke firmware continuously reports VGA, 2D, input, audio, SD, PSRAM
  profile, FPS, and memory behavior through a stable serial protocol.
- The SD mapping cannot regress unnoticed to generic FabGL defaults.

### Negative

- A clean managed Board Manager installation needs substantial disk space and
  downloads tools for architectures declared by the Espressif package.
- Arduino-ESP32 security or bug fixes are not inherited automatically.
- Huge APP has no OTA slot.
- PSRAM is physically present but disabled in the reference build; an enabled
  profile needs separate performance and hardware qualification.
- Optional Olimex driver sources still emit narrowing and macro-redefinition
  warnings. Their compatibility concession is manifest-locked rather than
  silently broadening flags at individual workstations.
- FabGL's GPL-3.0-or-later license applies to linked firmware. A closed-source
  distribution requires commercial licensing and legal review.

## Validation

The C++ manifest/command tests validate locks, pins, the compiler compatibility
contract, metacharacter-containing paths, and the absence of upload arguments.
On 2026-08-01, the diagnostic firmware compiled with locked Arduino CLI 1.5.1,
locally installed Arduino-ESP32 2.0.11, and the checksummed Olimex FabGL commit,
using 448,757 program bytes, a 449,120-byte primary image, and 25,920
global-data bytes. Its binary SHA-256 was
`87137e737da22ad4e686a7974f8ac35edae881e58c1944c3f0ff794a5ab08a56`.
Because the core was selected from the user Arduino data directory, this is an
exact-source integration result, not release provenance; release builds require
the complete managed profile.

Hardware upload and serial monitoring were intentionally not performed because
the board identity and port were not verified. They are separate scripts that
require a human port, exact profile confirmation, and (for upload) a
hash-verified build result.

## Reconsider when

Reconsider the core or FabGL lock only when a candidate passes all of:

1. manifest and safe-argument tests;
2. clean compile and binary/memory budgets;
3. API migration review, especially I2S, audio, timer, RMT, LEDC, and UART;
4. VGA/input/audio/SD/PSRAM hardware diagnostics;
5. a 30-minute hardware soak and repeated cold boots;
6. license/SBOM review;
7. a new ADR with exact URLs, sizes, checksums, commits, and rollback plan.
