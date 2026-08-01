# Contributing to FabGL Studio

FabGL Studio accepts focused changes that preserve the portable engine/editor boundary and do
not overstate hardware validation.

## Development workflow

1. Read `ARCHITECTURE.md`, the relevant ADRs, and the current milestone report.
2. Configure with `cmake --preset dev` (or `core-only` when Qt is unavailable).
3. Add or update tests for behavior and migrations.
4. Run `cmake --build --preset dev` and `ctest --preset dev`.
5. Format first-party C++ with the repository `.clang-format`.
6. Update user/API documentation when public behavior changes.

Commits should have one coherent purpose. Generated build trees, managed toolchains, imported
caches, personal editor layouts, serial logs, and secrets must not be committed.

## Compatibility rules

- Engine public headers cannot include Qt, SDL, Win32, or FabGL headers.
- Platform behavior enters through interfaces; avoid spreading preprocessor platform checks.
- Source formats require explicit versions and fixture-based migration tests.
- New asset formats require deterministic compilation and a documented target-memory cost.
- ESP32 hot paths avoid per-frame heap allocation and unbounded containers.
- A result observed only in PC simulation must never be described as a hardware measurement.

## Tests and reports

Unit tests cover pure behavior; integration tests exercise project/asset/build flows; rendering
tests compare deterministic reference frames; replay tests compare simulation state. Hardware
tests include the board/profile/firmware hash and raw serial evidence or are marked skipped.

Bug reports should include the host OS, Studio version/commit, build profile, project path
characteristics (spaces/Unicode), complete diagnostic text, and whether a physical board was
used. Do not attach proprietary projects or credentials.

## License

Contributions are accepted under the repository license. By submitting a contribution, you
represent that you have the right to license it and that third-party code/assets are clearly
identified with compatible terms.
