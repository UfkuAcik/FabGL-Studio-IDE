# FabGL Studio user guide

FabGL Studio 0.1 is a development snapshot, not a finished consumer release. The portable
engine, command-line project and asset tools, deterministic PC player, and automated tests are
usable today. The Qt editor source is present, but this repository has not been compiled with
Qt in the current verification environment. ESP32 firmware can be compiled with the pinned
toolchain; upload and peripheral behavior remain hardware-verification tasks.

## Install or run from a build

After a release build, stage or package the application as described in `BUILDING.md`. A ZIP
package is self-contained for the host executables it contains; it does **not** embed the large
ESP32 toolchain archives. Keep the extracted directory writable if you want tools to create
projects beside it.

From a developer build, the principal programs are:

| Program | Purpose |
|---|---|
| `FabGLStudio` | Qt desktop editor; only produced when matching Qt 6 is found |
| `fabgl_player_pc` | Native Windows or headless deterministic preview |
| `fabgl_project_cli` | Create, validate, and migrate project manifests |
| `fabgl_asset_compiler` | Convert images/audio and build/inspect asset packs |
| `fabgl_toolchain_manager` | Inspect a pinned ESP32 installation and print a safe compile command |

Run any command-line program with `--help` for its implemented syntax.

## Create and validate a project

```powershell
fabgl_project_cli new "C:\Games\My First Game" "My First Game"
fabgl_project_cli validate "C:\Games\My First Game\My First Game.fglproject"
```

Creation refuses to overwrite an existing project file. It creates `Assets`, `Scenes`,
`Scripts`, and `Packages`, plus a valid main scene. Paths with spaces and Unicode are supported.
Source paths stored in a project must remain relative and inside the project root.

The repository examples can be validated the same way. Their `previewDemo` field selects one
of the current deterministic demonstrations. The showcase scenes exercise distinct render paths,
but remain compact regression examples rather than finished games.

## Run the PC preview

On Windows, this opens an interactive native window:

```powershell
fabgl_player_pc --demo 2d
fabgl_player_pc --demo topdown
fabgl_player_pc --demo raycast
fabgl_player_pc --demo racer
fabgl_player_pc --demo lowpoly
fabgl_player_pc --demo ui
fabgl_player_pc --demo audio
fabgl_player_pc --demo animation
fabgl_player_pc --demo streaming
```

Use arrow keys or WASD, Space for the primary action, and Escape to exit. The low-poly path is
experimental. To produce a deterministic frame without a window:

```powershell
fabgl_player_pc --headless --demo raycast --output frame.ppm
```

The checksum printed by headless mode is useful for regression tests. It is not an ESP32
performance measurement.

## Convert assets

```powershell
fabgl_asset_compiler image hero.png hero.fgli --width 32 --height 32 --colors 16 --dither
fabgl_asset_compiler audio music.wav music.fgla --rate 22050 --stream
fabgl_asset_compiler pack build-assets.txt game.fglpack
fabgl_asset_compiler inspect game.fglpack
```

PNG/JPEG/BMP decoding is implemented through Windows Imaging Component. WAV input must be PCM.
The command fails on invalid data rather than emitting a partial result. Font, tilemap, model,
thumbnail, atlas, and compressed-audio importers are not implemented in this snapshot.

## Graphical editor orientation

When a Qt-enabled build is available, the editor layout is:

```text
+----------------------+-----------------------------+----------------------+
| Hierarchy            | Scene / Game                | Inspector            |
| entity selection     | authoring and preview       | selected transform   |
+----------------------+-----------------------------+----------------------+
| Assets / Project     | Code Editor                 | Profiler             |
+----------------------+-----------------------------+----------------------+
| Console                         | Build Output                              |
+---------------------------------------------------------------------------+
```

This is a functional map, not a screenshot. No screenshot is included because the editor was
not rendered in the current environment. See `EDITOR_GUIDE.md` for the controls and known gaps.

## ESP32 workflow

The release-locked profile is in `toolchains/manifest.json`. Inspect the local installation
before compiling:

```powershell
fabgl_toolchain_manager inspect --manifest toolchains/manifest.json --repo .
```

The manager reports paths and mismatches. `compile-command` prints a program/argument model but
does not execute an upload. Upload always requires an explicitly confirmed board and serial
port. Follow `TOOLCHAIN.md` and `HARDWARE_TESTING.md`; a CH340 serial adapter alone does not
prove that the connected device is an ESP32-SBC-FabGL.

## Data safety and recovery

Project and scene saves use atomic replacement in the implemented writers. The editor prompts
before closing modified data and before stopping an active build. Automatic backup rotation,
autosave recovery UI, crash reports, and safe mode are not complete, so use source control and
regular external backups. Do not edit generated `.fglpack`, `.fgli`, or `.fgla` files by hand.

## Troubleshooting

- If `FabGLStudio` is absent, configure with a Qt 6 build matching the compiler ABI.
- If a sample does not open in the editor, validate it with `fabgl_project_cli`; project-schema
  convergence between the CLI examples and Qt editor is still in progress.
- If a build command fails in the editor, inspect Build Output; it launches the configured
  executable directly, without a command shell.
- If ESP32 inspection reports `buildReady: false`, fix every reported version/path mismatch
  instead of allowing an unpinned toolchain.
- Treat PC resource and frame statistics as estimates until a hardware report records them.
