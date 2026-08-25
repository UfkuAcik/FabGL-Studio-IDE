# Rendering golden fixtures

`renderer2d-reference.ppm` and `raycast-reference.ppm` are real P6 PPM reference images. The
rendering test compares every RGB channel with a tolerance of 3 and permits at most 1% mismatched
pixels; it prints maximum channel deltas and the mismatch ratio on every run.

Baseline updates are intentionally opt-in. Build `fabgl_rendering_tests`, review the renderer
change, then run the test executable once with `FGL_UPDATE_RENDER_GOLDENS=1`. Run it again without
that environment variable to prove the checked-in images pass the normal read-only gate.
