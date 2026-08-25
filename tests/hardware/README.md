# FabGL hardware log harness

The firmware under `platforms/fabgl/firmware/` emits protocol-versioned serial
records. This directory contains an offline log parser and a separately invoked
bounded serial soak harness. Neither invokes Arduino CLI, uploads firmware,
resets the board, or writes to a microSD card. Only `run_serial_soak.ps1` opens a
port, and it requires the caller to name that exact port and confirm the locked
board profile.

Capture a log only through the separate, explicitly invoked monitor documented
in [`HARDWARE_TESTING.md`](../../HARDWARE_TESTING.md). Then parse the retained
file:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/hardware/parse_fabgl_log.ps1 `
  -LogPath evidence/board-run.log -Json
```

The parser accepts only `FABGLSTUDIO|1|STATUS|CHECK|details` records, while
ignoring unrelated ESP32 boot-ROM text. It fails on malformed protocol records,
any firmware `FAIL`, a missing required `PASS`, an incorrect SD pin report, a
missing `READY`, or a missing runtime metric. A passing parse still reports
`hardwareVerified=false`: VGA appearance, audio, electrical identity, stability,
and soak requirements remain human/HIL evidence from `HARDWARE_TESTING.md`.

The files in `fixtures/` are synthetic parser inputs, not captured device logs
and never release evidence. Validate both parser branches without hardware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/hardware/parse_fabgl_log.ps1 `
  -LogPath tests/hardware/fixtures/sample-pass.log

powershell -NoProfile -ExecutionPolicy Bypass -File tests/hardware/parse_fabgl_log.ps1 `
  -LogPath tests/hardware/fixtures/sample-fail.log
# The second command must exit 1.
```

Keep real evidence outside source control unless the project security and
privacy policy explicitly approves it; serial logs can contain device-specific
or user-entered data.

For an approved connected release fixture, capture the 30-minute automated
portion with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/hardware/run_serial_soak.ps1 `
  -Port COM5 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
  -DurationSeconds 1800 -RequireChurn `
  -OutputDirectory evidence/hardware/com5-soak
```

On Windows the harness also rejects a missing port or a serial adapter whose
PnP identifier is not the expected CH340 VID/PID. Its PASS is intentionally not
a complete hardware verdict; see `HARDWARE_TESTING.md` for the visual, audible,
physical-input, SD, PSRAM, and cold-power-cycle observations still required.

`test_esp32_build_profiles.ps1` is also hardware-free. It dry-runs all four
ESP32 compiler profiles, proves that their effective Arduino properties are
distinct, verifies the 3 MiB `huge_app` contract, exercises project staging
planning, and rejects output traversal, repository-root output, and ambiguous
project/sketch inputs. `test_port_detection.ps1` uses only its synthetic JSON
fixture; neither test opens `COM5` or any other port.
