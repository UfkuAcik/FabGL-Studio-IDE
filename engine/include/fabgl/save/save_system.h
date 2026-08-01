#pragma once

#include "fabgl/core/result.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
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

struct LoadedSave final {
    std::string payload;
    std::uint32_t storedSchemaVersion = 0;
    std::uint32_t currentSchemaVersion = 0;
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
    [[nodiscard]] Result<void> remove(std::string_view slot);
    [[nodiscard]] std::vector<std::string> slots() const;

    [[nodiscard]] static std::uint32_t checksum(std::uint32_t schemaVersion,
                                                std::string_view payload) noexcept;

  private:
    [[nodiscard]] Result<void> validateSlot(std::string_view slot) const;

    std::shared_ptr<ISaveStorage> storage_;
    std::uint32_t currentSchemaVersion_ = 1;
    std::map<std::uint32_t, Migration> migrations_;
};

} // namespace fabgl
