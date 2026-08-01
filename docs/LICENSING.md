# Licensing and release compliance

## Project choice

FabGL Studio is licensed under `GPL-3.0-or-later`. This is the least surprising default for an
open-source product whose target runtime statically links FabGL, itself GPL-3.0-or-later. The
license applies to first-party source unless a file says otherwise; third-party material keeps
its own terms.

## Firmware boundary

FabGL and game/runtime code are linked into one ESP32 firmware image. Distributed firmware is
therefore treated as a GPL combined work. A release containing firmware must provide, for at
least the required offer period and by a compliant method:

- the exact FabGL/runtime/game source used;
- generated-code inputs and asset build recipes needed to rebuild it;
- patches, configuration, linker scripts, and firmware build/upload scripts;
- the pinned compiler/core/tool versions and their license notices;
- clear installation information where anti-tivoization provisions apply.

Asset copyright is separate from program licensing. Example assets must be original or carry
explicit compatible attribution and redistribution permissions.

## Desktop boundary

Studio dynamically links only Qt modules available under LGPLv3 in the chosen profile. A
Windows package must include Qt's license notices and let recipients replace/relink the Qt
libraries. The project must not impose reverse-engineering restrictions that conflict with
LGPL rights. Plugins running in-process may form a combined work; out-of-process tools are a
cleaner separation but are not an automatic license exemption.

Arduino CLI and esptool are launched as separate executables with explicit arguments. Online
first-run download avoids redistributing those binaries inside the primary installer. An
offline toolchain bundle is a separate distribution and needs its own complete notice/source
inventory.

## Release checklist

1. Generate a file-level SBOM for the exact staged artifact.
2. Compare it with `THIRD_PARTY_LICENSES.md`; resolve every unknown component.
3. Include `LICENSE`, `NOTICE`, third-party license texts, Qt notices, and source links.
4. Archive the exact first-party and GPL dependency corresponding source.
5. Verify installer terms contain no prohibited reverse-engineering clause.
6. Verify checksums for packages and source bundles are published with release notes.
7. Record the audit and artifact hashes in the milestone/release report.

Commercial/closed-source firmware is not supported by the default build. It would require a
separate legal and technical decision plus commercial terms from FabGL/Olimex rights holders.
