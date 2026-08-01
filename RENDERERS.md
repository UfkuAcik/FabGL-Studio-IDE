# Renderer architecture and limits

FabGL Studio uses bounded software renderer modules rather than one desktop-style GPU
pipeline. They draw into a deterministic RGBA framebuffer on PC and emit equivalent bounded
commands for the FabGL adapter. The PC player can render all reference scenes without Qt:

```powershell
out\build\dev\apps\player_pc\fabgl_player_pc.exe --headless --demo 2d --output frame.ppm
out\build\dev\apps\player_pc\fabgl_player_pc.exe --headless --demo raycast --output frame.ppm
out\build\dev\apps\player_pc\fabgl_player_pc.exe --headless --demo racer --output frame.ppm
out\build\dev\apps\player_pc\fabgl_player_pc.exe --headless --demo lowpoly --output frame.ppm
```

On Windows, omit `--headless` for a native pixel-scaled window. Arrow/WASD keys control the
demo, Space performs the primary action, and Escape exits.

## Reference framebuffer

The framebuffer clips pixel, rectangle, line, sprite, and triangle writes; supports integer
alpha blending; writes portable PPM frames; and exposes an FNV-1a image checksum for exact
golden tests. It is intentionally simple enough that target backends can match its rules.

## Renderer2D

The implemented reference path supports transparent/tinted/flipped sprites, integer scaling,
tilemaps, camera culling, viewports, and draw/sprite counters. The target pack will replace
RGBA vectors with indexed palettes or target-selected pixel formats. Sprite rotation, atlas
metadata, parallax, bitmap text, and particles remain separate commands so projects pay only
for enabled features.

## Grid raycast renderer

The working PC renderer uses DDA wall casting with field-of-view plane, wall palette,
side/distance shading, pitch-limited horizon, depth buffer, and depth-tested billboard sprites.
Stats report rays, DDA steps, and visible billboards. Target optimization may replace float
with fixed point and reciprocal/look-up tables while preserving map/camera semantics.

## Pseudo-3D racer

The racer renderer projects scanlines from segment data containing curve, hill, width, road,
grass, and rumble colors. It implements perspective narrowing, curve accumulation, striped
terrain/rumble, lane markers, lateral camera movement, and a deterministic demo track. Track
objects, opponents, weather layers, and checkpoint rules belong to renderer/framework data,
not hard-coded scene behavior.

## Experimental low-poly renderer

The PC technology path transforms indexed triangles, clips the near plane conservatively,
backface-culls, applies directional flat lighting, sorts by painter depth, projects, and fills
triangles. There is no claim that its PC throughput predicts ESP32 throughput. Texture atlas,
limited depth buffer, and billboards must be enabled only after hardware budget measurements.
All UI and examples label this path **Experimental**.

## Golden and hardware validation

Golden tests compare exact frame checksums for deterministic reference scenes. A platform
adapter may additionally use a documented tolerance for pixel-format quantization. VGA
hardware output, frame rate, internal RAM, PSRAM, and fragmentation require the diagnostic
firmware and serial evidence; PC values are reported separately.
