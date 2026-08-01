# Security policy

## Supported versions

Until the first stable release, security fixes target the current `main` branch. After stable
release, this file will list supported release lines and end-of-support dates.

## Reporting a vulnerability

Do not open a public issue for a vulnerability that can execute code, escape project roots,
overwrite arbitrary files, expose project data, or flash an unintended device. Use the
repository host's private security-advisory channel. Include a minimal reproduction, affected
commit/version, impact, and suggested mitigation if known. Do not include unrelated user data.

## Threat model

FabGL Studio treats imported projects, archives, build scripts, native plugins, and gameplay
code as untrusted. Merely browsing a project must not execute it. Code/plugin/build execution
requires an explicit trust decision.

Security-sensitive implementation requirements:

- Canonicalize and validate all extraction and generated-output paths against an allowed root.
- Start processes with an executable plus argument array; never interpolate a shell command.
- Keep telemetry disabled and make any future network behavior opt-in and inspectable.
- Verify managed downloads using trusted package metadata and checksums where published.
- Require explicit port and target selection before upload; VID/PID is only a hint.
- Use atomic replacement and backups for authoring files.
- Load plugins only from declared package entry points after trust and compatibility checks.

## Disclosure

Maintainers will acknowledge a complete private report, reproduce it, prepare regression
tests where safe, and coordinate disclosure after a fix is available. No response-time promise
is made before project governance and contact channels are finalized.
