# Milestone 14 report

## Milestone

**M14 — Productization. Status: partial.**

## Completed work

Added ten valid example project structures, user/editor/API/package documentation, CMake install
rules, an always-requested portable ZIP workflow, optional NSIS detection, staged CLI smoke test,
Qt runtime deployment hook, license/notice installation, and toolchain/firmware metadata staging.

## Changed files

`examples/`, `USER_GUIDE.md`, `EDITOR_GUIDE.md`, `packaging/`, `PACKAGE_FORMAT.md`, and the
install/CPack section of `CMakeLists.txt`.

## Architecture decisions

Toolchain caches are not bundled; ZIP is baseline, NSIS is optional, and absent Qt never becomes
a fake graphical package claim.

## Commands run

Release CTest passed 8/8, install staging passed, every staged CLI passed its smoke check, the ten
bundled examples passed validation/replay in CTest, and CPack produced a 530,546-byte ZIP with
SHA-256 `cb6b20ee0dec56b05609b5929de2acf536e8129b353f84b5c63b184542b3aa18`.
A separate 6,583,195-byte compile-only ESP32 evidence archive has SHA-256
`b533c6ef11a91bac047ab97c847efa354512c38ba8c467bd056d3217888b1bb4`. A clean Windows
machine, Qt deployment, and NSIS installer were not exercised.

## Test results

- Passed: release build, 8 CTest programs, stage install, four CLI smoke checks, two ZIP checksums.
- Failed: 0 executed checks.
- Skipped: Qt deployment/launch, NSIS, clean-machine, signing, and hardware workflow.

## Remaining work

First-run GUI setup, autosave/crash recovery, signed releases, installer UX, real screenshots,
clean-machine matrix, and production-scale showcase content remain.
