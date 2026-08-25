#pragma once

#include "fabgl/core/result.h"
#include "fabgl/math/types.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fabgl {

class ISaveStorage {
  public:
    virtual ~ISaveStorage() = default;
    [[nodiscard]] virtual Result<void> writeAtomically(std::string_view slot, std::string data) = 0;
    [[nodiscard]] virtual Result<std::string> read(std::string_view slot) const = 0;
    [[nodiscard]] virtual Result<void> remove(std::string_view slot) = 0;
    [[nodiscard]] virtual std::vector<std::string> slots() const = 0;
};

class MemorySaveStorage final : public ISaveStorage {
  public:
    [[nodiscard]] Result<void> writeAtomically(std::string_view slot, std::string data) override;
    [[nodiscard]] Result<std::string> read(std::string_view slot) const override;
    [[nodiscard]] Result<void> remove(std::string_view slot) override;
    [[nodiscard]] std::vector<std::string> slots() const override;

  private:
    std::map<std::string, std::string> data_;
};

// A directory-backed implementation suitable for PC save folders and mounted SD-card paths.
// Each slot is stored as <slot>.fglsave. Writes are committed through a same-directory temporary
// file and retain a bounded set of older copies before replacing the live slot.
class FileSaveStorage final : public ISaveStorage {
  public:
    explicit FileSaveStorage(std::string directory, std::size_t backupCount = 3U);

    [[nodiscard]] Result<void> writeAtomically(std::string_view slot, std::string data) override;
    [[nodiscard]] Result<std::string> read(std::string_view slot) const override;
    [[nodiscard]] Result<void> remove(std::string_view slot) override;
    [[nodiscard]] std::vector<std::string> slots() const override;

    [[nodiscard]] Result<std::string> readBackup(std::string_view slot,
                                                 std::size_t generation) const;
    [[nodiscard]] const std::string& directory() const noexcept {
        return directory_;
    }
    [[nodiscard]] std::size_t backupCount() const noexcept {
        return backupCount_;
    }

  private:
    [[nodiscard]] Result<void> validateSlot(std::string_view slot) const;

    std::string directory_;
    std::size_t backupCount_ = 0U;
};

struct LoadedSave final {
    std::string payload;
    std::uint32_t storedSchemaVersion = 0;
    std::uint32_t currentSchemaVersion = 0;
    bool migrated = false;
};

using SaveValue = std::variant<bool, std::int64_t, double, std::string, Vec2, Vec3>;
using SaveStateMap = std::map<std::string, SaveValue>;

// Gameplay state is intentionally separate from scene/project serialization.
// Stable string keys are supplied by the game, so runtime EntityId reuse does
// not corrupt a save after scene reload or migration.
struct SaveDocument final {
    SaveStateMap primitives;
    SaveStateMap player;
    SaveStateMap scene;
    std::map<std::string, SaveStateMap> entities;
};

struct LoadedSaveDocument final {
    SaveDocument document;
    std::uint32_t storedSchemaVersion = 0U;
    std::uint32_t currentSchemaVersion = 0U;
    bool migrated = false;
};

class SaveSystem final {
  public:
    using Migration = std::function<Result<std::string>(std::string_view)>;
    static constexpr std::uint32_t FormatVersion = 1;

    SaveSystem(std::shared_ptr<ISaveStorage> storage, std::uint32_t currentSchemaVersion);

    [[nodiscard]] Result<void> registerMigration(std::uint32_t fromVersion, Migration migration);
    [[nodiscard]] Result<void> save(std::string_view slot, std::string payload);
    [[nodiscard]] Result<LoadedSave> load(std::string_view slot) const;
    [[nodiscard]] Result<void> saveDocument(std::string_view slot,
                                            const SaveDocument& document);
    [[nodiscard]] Result<LoadedSaveDocument> loadDocument(std::string_view slot) const;
    [[nodiscard]] Result<void> remove(std::string_view slot);
    [[nodiscard]] std::vector<std::string> slots() const;

    [[nodiscard]] static std::uint32_t checksum(std::uint32_t schemaVersion,
                                                std::string_view payload) noexcept;
    [[nodiscard]] static Result<std::string> serializeDocument(
        const SaveDocument& document);
    [[nodiscard]] static Result<SaveDocument> deserializeDocument(std::string_view payload);

  private:
    [[nodiscard]] Result<void> validateSlot(std::string_view slot) const;

    std::shared_ptr<ISaveStorage> storage_;
    std::uint32_t currentSchemaVersion_ = 1;
    std::map<std::uint32_t, Migration> migrations_;
};

} // namespace fabgl
