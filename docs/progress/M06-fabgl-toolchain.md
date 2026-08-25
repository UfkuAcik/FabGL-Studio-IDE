# Milestone 6 report

## Milestone

**M6 — Toolchain and FabGL runtime. Status: software complete; hardware validation pending.**

## Completed work

Arduino CLI 1.5.1, Arduino-ESP32 2.0.11 and Olimex FabGL commit
`04f328a10573297dd554f13be7f369cdee0f7a2b` are locked with checksums in the repository-local
toolchain manifest. Debug, Release, SizeOptimized and PerformanceOptimized profiles perform real
project export, asset-pack generation, firmware compile/link, map/size/hash reporting and output
schema validation. Long paths are normalized safely. Detection, upload and serial monitor are
separate commands; upload requires an explicit port and exact board-profile confirmation.

## Build evidence

- Diagnostic Release: 448,976 program bytes.
- Empty SizeOptimized: 448,848 program bytes.
- Platformer PerformanceOptimized: 488,496 program bytes and 25,928 / 327,680 RAM bytes.

These are compiler outputs from the locked FabGL/ESP32 pipeline, not simulated size estimates.
Profile/port parser and offline diagnostic-log tests pass, and Windows CI contains the ESP32 build
job.

## Hardware boundary

A CH340 serial interface was detected as `COM5`, but USB-serial detection does not establish the
attached board identity. No flash, erase, upload or serial-open operation was performed without the
user confirming both `COM5` and `olimex-esp32-sbc-fabgl-revb`. VGA, PS/2, audio, SD, PSRAM,
on-device FPS/memory and soak checks therefore remain unverified HIL evidence.
