# Olimex ESP32-SBC-FabGL hardware testing

No firmware was uploaded during the Milestone 6 implementation because the
physical board identity and serial port were not independently confirmed. A
CH340/CH340X USB adapter identity alone is insufficient evidence: many devices
use the same bridge.

## Safety gate before upload

Record all of the following in the test log:

- photograph/label confirming `ESP32-SBC-FabGL` and the hardware revision;
- visual comparison against the Olimex Rev B schematic and connector layout;
- the serial port selected by a human after observing connect/disconnect;
- supply voltage/cable and connected VGA, PS/2, audio, and microSD peripherals;
- `build-result.json` proving core 2.0.11, the full FQBN, binary SHA-256, and no
  prior upload action;
- whether the reference PSRAM-disabled or experimental PSRAM-enabled build is
  under test.

Do not attach or remove PS/2 devices while the board is powered. Back up any
important microSD contents. The diagnostic mounts the card and reads metadata;
it deliberately does not write, format, or remove files.

## Build

Create the reference binary first:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 -Clean
```

The build script cannot upload. After the safety gate, use the separate uploader
with the human-confirmed port and exact profile token. It accepts no binary
argument: the binary, byte count, and SHA-256 come only from `build-result.json`
and are verified again before any process starts. Never derive the board or port
from the USB adapter.

First inspect the exact argv without opening the port:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM9 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun
```

Then, and only after a human has reviewed the plan and completed the gate:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM9 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
```

If 921600 upload speed is unreliable, stop and record the failure. The manifest
identifies 115200 as the approved fallback, but changing the build-result FQBN
or uploader argv by hand is forbidden; add an explicit reviewed fallback option
before using it.

## Serial protocol

Open the separate serial monitor at 115200 baud; it requires both values and
cannot upload:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/serial_monitor.ps1 `
  -Port COM9 -Baud 115200 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
```

Each machine-readable line is:

```text
FABGLSTUDIO|1|STATUS|CHECK|key=value;key=value
```

After capture, validate the retained file without reconnecting to hardware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/hardware/parse_fabgl_log.ps1 `
  -LogPath evidence/board-run.log -Json
```

The parser fails on firmware `FAIL` records, missing required checks, the wrong
SD pin report, a missing `READY`, or no runtime metric. Even a parser PASS keeps
`hardwareVerified=false`; manual observations and soak evidence below are still
required. Synthetic positive/negative fixtures live under
`tests/hardware/fixtures/` and are never hardware evidence.

Statuses are:

- `BOOT`: firmware/profile identity;
- `PASS` or `FAIL`: an executable check;
- `MANUAL`: electrical/visual/audible confirmation still required;
- `READY`: startup checks completed and the interactive loop is running;
- `METRIC`: once-per-second runtime measurements.

Do not treat `PASS|vga_init` or `PASS|audio_pipeline` as proof of an analog
signal. They prove controller/generator initialization; the matching `MANUAL`
record requires a human observation.

## Expected checks

| Check | Automated evidence | Human evidence / pass criterion |
| --- | --- | --- |
| Serial | `PASS|serial`, 115200 protocol | Continuous uncorrupted lines for 10 minutes |
| VGA | `PASS|vga_init`, `PASS|renderer_2d` | Stable 320x200 sync, eight bars, text, checkerboard, moving yellow square; no tearing/corruption |
| Keyboard | `PASS|keyboard_detect`, subsequent `PASS|keyboard_event` | At least ten distinct press/release events, including arrows and Space |
| Mouse | `PASS|mouse_detect`, subsequent `PASS|mouse_event` | X/Y motion, buttons, and wheel if fitted; no stuck stream |
| Audio | `PASS|audio_pipeline` | Clean 440 Hz tone on the intended output, no reset or VGA disturbance |
| microSD | `PASS|sd_mount` with capacity | Correct capacity; pins must report `35/12/14/13`; no writes occur |
| Memory | `PASS|memory`, recurring `METRIC|runtime` | No downward unbounded heap trend, min/largest block remain within project budgets |
| 2D throughput | recurring `fps` metric | Stable rate for 30 minutes; record min/mean/max and VGA artifacts |
| PSRAM reference | `PASS|psram_profile|profile=disabled` | Correct for release reference build; not a physical PSRAM test |

Any `FAIL` is a failed run, including a missing keyboard/mouse/card when that
peripheral is part of the test configuration. Retest absent peripherals only
after a cold power cycle and connector inspection.

## Experimental physical PSRAM test

Build a separate artifact:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 `
  -EnablePsram -Clean -OutputRoot out\esp32-psram
```

This changes only `PSRAM=disabled` to `PSRAM=enabled` and labels the result
`enabled-experimental`. A passing run must emit `PASS|psram` with a nonzero size
and free count, complete the same VGA/audio/SD/input test, and show no material
FPS regression or heap instability. It does not replace the reference profile
without an ADR and performance evidence.

## Soak and retained evidence

For a release candidate:

1. run the interactive diagnostic for 30 minutes;
2. collect the complete serial stream, not screenshots of selected lines;
3. sample VGA output at startup and after the soak;
4. repeat with microSD present and absent, recording the expected difference;
5. power-cycle five times and require identical boot-to-`READY` behavior;
6. retain firmware `.bin`, `.elf`, `build-result.json`, serial log, board photo,
   test operator, date, ambient conditions, and pass/fail disposition.

The test is complete only when all automated checks and all applicable manual
checks pass. Compilation alone is not hardware validation.
