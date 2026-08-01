# Milestone 6 report

## Milestone

**M6 — Toolchain and FabGL runtime. Status: partial; hardware validation blocked.**

## Completed work

Pinned Arduino-ESP32 2.0.11 and Olimex FabGL commit `04f328a`, recorded checksums, board options and
pins, implemented verified download/bootstrap, detection and safe compile-command generation, and
added FabGL diagnostic firmware. Upload and serial monitor are separate scripts requiring an
explicit port and exact board-profile token; upload is never implicit. An offline-only structured
log parser covers success and expected-failure fixtures without opening a port.

## Changed files

`toolchains/manifest.json`, `tools/toolchain_manager/`, `platforms/fabgl/`, `TOOLCHAIN.md`,
`HARDWARE_TESTING.md`, `scripts/{bootstrap_toolchain,build_esp32,upload_esp32,serial_monitor}.ps1`,
`tests/hardware/`, and ADR 0004.

## Architecture decisions

ADR 0004 documents the compatibility pin and explicit-port upload boundary.

## Commands run

Arduino CLI 1.5.1 and the exact Olimex FabGL source compiled the diagnostic against the locally
installed Arduino-ESP32 2.0.11 core. Program storage was 448,757 bytes, globals were 25,920 bytes,
and the primary 449,120-byte binary has SHA-256
`87137e737da22ad4e686a7974f8ac35edae881e58c1944c3f0ff794a5ab08a56`.
`uploadPerformed=false`. **Upload, monitor, VGA, PS/2, audio, SD, PSRAM, reset, FPS, memory, and
soak tests were skipped** because no board identity was positively confirmed.

## Test results

- Passed: toolchain manager, locked-source firmware compile, and two offline log-fixture CTests.
- Failed: 0 host CTest programs.
- Skipped: upload and every physical peripheral/performance/soak check.

## Remaining work

Run a clean fully managed-core build for release attestation, then exercise the explicit upload,
monitor, and full hardware checklist on a known Olimex board. The release profile keeps PSRAM
disabled until HIL.
