# ADR 0019: Use a separate transactional ESP32 gameplay-save codec

- Status: Accepted
- Date: 2026-08-22

## Context

Desktop saves use dynamic strings, maps, variants, filesystem paths and migration functions. The
ESP32 project runtime is allocation-free and shares limited internal RAM with FabGL VGA/audio.
Reusing editable scene serialization would also mix authoring data with mutable gameplay state.
Arduino's SD/FAT adapter supplies rename and flush operations but does not promise that filesystem
metadata is atomic across power loss.

## Options considered

1. Serialize the editable `.fglscene` to SD.
2. Port the desktop `SaveSystem` containers/filesystem implementation to Arduino.
3. Define a bounded target codec, keep stable runtime identities, and place an explicit
   temp/backup transaction adapter behind callbacks.

## Decision

Choose option 3. `ProjectSaveRuntime.h` defines a little-endian version-1 binary document with a
schema, sequence, size, CRC-32 and strict bounds. It captures a scene and at most 16 runtime
entities by stable GUID plus an optional player identity. Typed game fields support primitive,
vector, string and entity-reference values. Sequential migration callbacks are explicit.

`SaveService` knows no Arduino API. It receives read/stage/commit/discard/remove callbacks and a
caller-owned 4 KiB scratch buffer. `ProjectSaveSdAdapter.h` stages and verifies `.tmp`, retains one
`.bak`, restores the old live path after a failed commit and reports rollback failure distinctly.
The firmware places scratch bytes in bounded RTC slow memory to preserve ordinary DRAM and binds
save/load callbacks to `RuntimeProject`. Only gameplay calls to `saveSlot` write; boot, mount,
diagnostics and soak do not.

## Rationale

A separate bounded codec keeps authoring serialization out of the target hot path, makes every
allocation and file size explicit, and allows the transaction algorithm to be host-tested without
pretending that an in-memory mock proves FAT power-loss behavior. Stable GUIDs preserve the useful
cross-platform identity contract while the callback boundary keeps Arduino SD details outside the
portable runtime.

## Positive consequences

- Host tests exercise exactly the target codec and transaction contract with no Arduino mock.
- Corrupt, newer, oversized, unsafe-path and identity-mismatched data fail before runtime mutation.
- A failed stage or ordinary commit rollback preserves the previous live generation.
- The PSRAM-disabled reference firmware remains linkable with a measured static-RAM margin.

## Negative consequences

- The target binary is not automatically interchangeable with the desktop ASCII save envelope.
- At most 16 entities, 16 total custom fields and 4 KiB are saved by version 1.
- FAT rename/flush cannot guarantee survival of every physical brown-out pattern.
- The default runtime convenience callback captures standard runtime fields only; advanced custom
  fields require the lower-level document API.

## Verification boundary

Host memory storage tests cover typed values, multiple slots, migration, corruption, versions,
paths, capacities, identity validation, backup recovery and failed-write rollback. The pinned
Xtensa Release build verifies C++11/link/static-RAM compatibility and performs no upload. Physical
card wear, flush behavior and power-cut durability require the destructive HIL procedure in
`HARDWARE_TESTING.md` and must not be inferred from compilation.

## Reconsider when

Introduce a shared cross-platform payload only if it retains these bounds on ESP32 and has frozen
desktop/target compatibility fixtures. Revisit the transaction when the platform offers a
documented power-fail-safe filesystem primitive or a journaled storage layer.
