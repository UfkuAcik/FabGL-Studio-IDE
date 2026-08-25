# Olimex ESP32-SBC-FabGL hardware testing

> **Current checkpoint (2026-08-25):** the final handoff Release binary compiled but was not
> uploaded. See [`docs/HANDOFF.md`](docs/HANDOFF.md) for its exact hash and remaining gates. The
> 2026-08-13 run below belongs to an older artifact and cannot validate current firmware.

The user explicitly confirmed `COM5` and profile `olimex-esp32-sbc-fabgl-revb`. On
2026-08-13 the verified Pseudo-3D Racer project firmware was uploaded through the
separate guarded uploader and its structured serial stream was captured. The chip
identified as ESP32-D0WD-V3 and every written section hash verified. This establishes
automated parser/update/render-loop evidence for that artifact; it does not replace the
manual VGA image, audio, input, peripheral, board-label, or soak observations below.

Retained local developer evidence is under
`evidence/hardware/2026-08-13-com5-pseudo3d-racer-runtime/`:

`evidence/` is intentionally ignored by Git because raw serial logs and device-specific records
should not be published by default. These paths may therefore be absent from a public clone.

- `result-final.json` indexes the exact build, upload, log, and verification files;
- `build-result-final.json` records a 603,600-byte binary with SHA-256
  `71503add6a622d066f2c4f8c73b8b212675f548702ee104d516e479875768ee3`;
- `upload-result-final.json` records explicit COM5 execution;
- `serial-runtime-final.log` is the unedited bounded serial capture;
- `project-runtime-verification-final.json` and `generic-log-parser-final.json` record the
  project-specific and generic parser verdicts.

The first physical project run correctly reported a VGA failure when runtime memory forced
an undersized viewport. That failed log is deliberately retained. The corrected runtime reserves
a deterministic 320x192 viewport while retaining 320x200@75 Hz output timing.

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

Project firmware contains an explicit gameplay save API, but boot, mount,
diagnostics and soak mode do not invoke it. An SD write test therefore requires
a reviewed portable script that deliberately calls `project.saveSlot(...)`;
never treat `PASS|sd_mount` or a compile as save/load durability evidence.

## Build

Create the reference binary first:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 -Clean
```

Or build a project-backed diagnostic carrying the canonical scene and asset
pack in flash:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 `
  -ProjectPath examples\platformer\Platformer.fglproject `
  -BuildProfile PerformanceOptimized -Clean `
  -OutputRoot out\esp32\platformer
```

For the release soak, build the real Racer payload with the fixed-capacity
churn workload enabled. This compile-time mode alternates the project and
diagnostic scenes, repeatedly reads and clears a bounded 64-byte asset cache,
plays a short tone, and creates/destroys entries in a 16-slot entity pool. It
emits a `METRIC|soak` record with monotonic counters and performs no SD write:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_esp32.ps1 `
  -ProjectPath examples\pseudo3d_racer\Racer.fglproject `
  -BuildProfile Release -SoakDiagnostics -Clean `
  -OutputRoot out\esp32\racer-soak
```

For any artifact, verify `build-result.json` schema 2, build profile, partition
CSV SHA-256, compiler flags, program/RAM analysis, binary/payload hashes, and
`uploadPerformed=false`. A project-backed boot must also emit
`PASS|project_payload` with `sceneV2=true`, the expected entity/asset counts,
and the checksum from its generated payload.

The build script cannot upload. After the safety gate, use the separate uploader
with the human-confirmed port and exact profile token. It accepts no binary
argument: the binary, byte count, and SHA-256 come only from `build-result.json`
and are verified again before any process starts. Never derive the board or port
from the USB adapter.

First inspect the exact argv without opening the port:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM5 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb -DryRun
```

Then, and only after a human has reviewed the plan and completed the gate:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/upload_esp32.ps1 `
  -Port COM5 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
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
  -Port COM5 -Baud 115200 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb
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

The final Racer capture contains 33 structured records and zero `FAIL` records. It loaded Scene
v2 with 3 entities, 1 asset, and 6 input bindings; reported `mode=racer`, one vehicle, and 16 track
segments; and advanced project update/render counters from 330/331 to 2612/2613 over eight metric
samples. No physical input was injected, so zero vehicle speed/distance is expected. Both parsers
return PASS while retaining `hardwareVerified=false` because visual/audio/electrical/soak checks
are manual.

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
| Project payload | `PASS|project_payload`, `sceneV2=true`, matching checksum/counts | Project title/demo visible; no payload failure |
| VGA | `PASS|vga_init`, `PASS|renderer_2d` | Stable 320x200 sync, eight bars, text, checkerboard, moving yellow square; no tearing/corruption |
| Keyboard | `PASS|keyboard_detect`, subsequent `PASS|keyboard_event` | At least ten distinct press/release events, including arrows and Space |
| Mouse | `PASS|mouse_detect`, subsequent `PASS|mouse_event` | X/Y motion, buttons, and wheel if fitted; no stuck stream |
| Audio | `PASS|audio_pipeline` | Clean 440 Hz tone on the intended output, no reset or VGA disturbance |
| microSD | `PASS|sd_mount` with capacity | Correct capacity; pins must report `35/12/14/13`; no writes occur |
| Memory / SD I/O | `PASS|memory`, recurring `METRIC|runtime` | No downward unbounded heap trend; `heapFree`/`largestBlock` produce the Studio fragmentation indicator; cumulative `sdReadBytes` matches intentional save-slot reads |
| 2D throughput | recurring `fps` metric | Stable rate for 30 minutes; record min/mean/max and VGA artifacts |
| PSRAM reference | `PASS|psram_profile|profile=disabled` | Correct for release reference build; not a physical PSRAM test |

Any `FAIL` is a failed run, including a missing keyboard/mouse/card when that
peripheral is part of the test configuration. Retest absent peripherals only
after a cold power cycle and connector inspection.

## Optional destructive microSD save/load test

Use a disposable or fully backed-up card. Review the portable script and require that it uses a
unique test slot, calls `saveSlot` only in response to a deliberate key/action, reports
`project_save`, mutates runtime state, then calls `loadSlot` and reports `project_load`. Confirm the
restored scene/player/entity values and inspect
`/fabglstudio/saves/<slot>.fglsave` plus its single `.bak` generation. Repeat a normal restart and
five cold power cycles. A separate fault-injection run may remove power during staging/rename, but
only on disposable media; accept either the last complete live generation or its checksum-valid
backup, never a partially applied runtime state.

Host memory-fake rollback tests and Xtensa compile-only evidence cover codec/control flow, not FAT
wear, card flush behavior, brown-out timing or physical media durability. Record card model,
filesystem, power procedure, raw serial log and final file hashes. No such physical write/power-cut
run is claimed by the repository unless that retained evidence exists.

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

The bounded serial portion can be captured and evaluated directly with the
repository harness. It opens only the explicitly named port, verifies the
locked Olimex profile and (on Windows) the expected CH340 serial adapter, never
uploads firmware, and retains both the raw protocol stream and a JSON result:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/hardware/run_serial_soak.ps1 `
  -Port COM5 -ConfirmBoardProfile olimex-esp32-sbc-fabgl-revb `
  -DurationSeconds 1800 -RequireChurn `
  -OutputDirectory evidence/hardware/2026-08-13-com5-soak
```

The command fails on firmware `FAIL` records, malformed records, inadequate
sample density, non-monotonic project counters, FPS below 30, less than 48 KiB
free heap, less than a 32 KiB largest block, or a mean heap loss greater than
4 KiB, or any missing/backwards/inconsistent scene, asset, audio or entity
churn counter. A successful JSON result deliberately retains
`hardwareVerified=false`: it covers the 30-minute serial/runtime observation,
not the manual VGA, audible audio, physical input, SD-write, PSRAM, or five
cold-power-cycle evidence listed above.
