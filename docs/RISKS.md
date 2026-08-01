# Risk register

| Risk | Likelihood | Impact | Mitigation / evidence gate |
|---|---:|---:|---|
| FabGL and newer ESP32 cores regress together | High | High | Pin a known build, compile a smoke firmware, upgrade only with CI and hardware results. |
| Desktop Qt/compiler ABI mismatch | High | High | Manifest pairs them; bootstrap checks architecture and ABI before configure. |
| Toolchain downloads exhaust disk | High | High | Preflight free-space threshold, resumable cache, checksums, and offline bundles. |
| PC preview diverges from ESP32 behavior | Medium | High | Shared engine/data code, compatibility budgets, deterministic replay, hardware telemetry. |
| Dynamic allocation fragments ESP32 heap | Medium | High | Packed target data, preallocation, pools, long-running firmware soak test. |
| Source migration corrupts projects | Medium | High | Versioned readers, atomic writes, backup rotation, fixtures for every migration. |
| Asset import executes untrusted content | Low | Critical | No implicit executables, canonical roots, importer allowlist, explicit trust model. |
| Renderer scope exceeds hardware capacity | High | Medium | Separate bounded renderers; budget profiles; Experimental label until measured. |
| GPL/LGPL obligations are missed in packages | Medium | High | SPDX inventory, source offers/notices, dynamic Qt deployment, release license audit. |
| A serial port is misidentified and flashed | Medium | High | No automatic upload solely from VID/PID; explicit target selection and confirmation. |

Milestone reports update probability, impact, and actual observations.
