# FabGL hardware log harness

The firmware under `platforms/fabgl/firmware/` emits protocol-versioned serial
records. This directory contains an **offline log parser only**. It never lists
ports, opens a serial device, invokes Arduino CLI, uploads firmware, or writes to
a microSD card.

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
