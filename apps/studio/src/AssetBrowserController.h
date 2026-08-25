#pragma once

#include "AssetBrowserImporters.h"

#include <fabgl/assets/asset_importer.h>
#include <fabgl/core/guid.h>
#include <fabgl/core/result.h>

#include <QAbstractTableModel>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QMimeData;

namespace fgl::studio {

enum class AssetBrowserState : std::uint8_t {
    Clean = 0,
    Dirty,
    Missing,
    Error,
    Unsupported,
};

struct AssetBrowserCost final {
    std::uint64_t payloadBytes = 0U;
    std::uint64_t flashBytes = 0U;
    std::uint64_t internalRamBytes = 0U;
    std::uint64_t psramBytes = 0U;
    std::uint64_t sdBytes = 0U;
    std::uint32_t estimatedDecodeMicros = 0U;
    std::uint64_t estimatedRenderPixelsPerFrame = 0U;

    friend bool operator==(const AssetBrowserCost&, const AssetBrowserCost&) = default;
};

struct AssetBrowserSourceMetadata final {
    std::uint64_t bytes = 0U;
    std::uint64_t fingerprint = 0U;
    QDateTime modifiedUtc;

    friend bool operator==(const AssetBrowserSourceMetadata&,
                           const AssetBrowserSourceMetadata&) = default;
};

struct AssetBrowserProjectEntry final {
    fabgl::AssetGuid guid;
    QString relativePath;
    QString type;
    QString normalizedSettings;
    fabgl::assets::AssetTarget esp32Target = fabgl::assets::AssetTarget::Esp32Flash;
    QVector<fabgl::AssetGuid> dependencies;
    bool hasExplicitImportMetadata = false;
};

struct AssetBrowserEntry final {
    fabgl::AssetGuid guid;
    QString relativePath;
    QString absolutePath;
    QString type;
    QString importer;
    QString normalizedSettings;
    QString diagnostic;
    fabgl::assets::AssetTarget esp32Target = fabgl::assets::AssetTarget::Esp32Flash;
    AssetBrowserState state = AssetBrowserState::Dirty;
    AssetBrowserSourceMetadata source;
    QVector<fabgl::AssetGuid> dependencies;
    QVector<fabgl::AssetGuid> dependents;
    AssetBrowserCost pcCost;
    AssetBrowserCost esp32Cost;
    QByteArray thumbnail;
    bool thumbnailPlaceholder = true;
    std::uint64_t pcCacheKey = 0U;
    std::uint64_t esp32CacheKey = 0U;
};

struct AssetBrowserLimits final {
    std::size_t maximumAssets = 4096U;
    std::size_t maximumDirectories = 512U;
    std::size_t maximumWatchedDirectories = 256U;
    std::size_t maximumWatchedFiles = 1024U;
    std::uint64_t maximumSourceBytes = 64U * 1024U * 1024U;
    std::uint64_t maximumAggregateSourceBytes = 256U * 1024U * 1024U;
    int debounceMilliseconds = 150;
    int maximumDebounceMilliseconds = 1000;
    std::uint64_t flashBudgetBytes = 4U * 1024U * 1024U;
    std::uint64_t internalRamBudgetBytes = 192U * 1024U;
    std::uint64_t psramBudgetBytes = 4U * 1024U * 1024U;
    std::uint64_t sdBudgetBytes = 64U * 1024U * 1024U;
};

struct AssetBrowserRefreshStats final {
    std::size_t imported = 0U;
    std::size_t cacheHits = 0U;
    std::size_t errors = 0U;
    std::size_t discovered = 0U;
    std::size_t mappingsMoved = 0U;
};

struct AssetBrowserExtensionImporterHooks final {
    using Probe = std::function<fabgl::Result<std::optional<AssetBrowserImporterDescriptor>>(
        const QString&, const QString&, const QString&)>;
    using Import = std::function<fabgl::Result<fabgl::assets::ImportedAsset>(
        const AssetBrowserImporterDescriptor&, const fabgl::assets::AssetImportRequest&,
        const QString&)>;

    Probe probe;
    Import import;
};

class AssetBrowserModel final : public QAbstractTableModel {
    Q_OBJECT

  public:
    enum Column : int {
        NameColumn = 0,
        TypeColumn,
        GuidColumn,
        StateColumn,
        ImporterColumn,
        DependenciesColumn,
        DependentsColumn,
        PcBytesColumn,
        FlashBytesColumn,
        InternalRamBytesColumn,
        PsramBytesColumn,
        SdBytesColumn,
        DecodeCostColumn,
        RenderCostColumn,
        ColumnCount,
    };

    enum Role : int {
        GuidRole = Qt::UserRole + 1,
        RelativePathRole,
        TypeRole,
        StateRole,
        ImporterRole,
        DependenciesRole,
        DependentsRole,
        PcCostRole,
        Esp32CostRole,
        ThumbnailBytesRole,
        ThumbnailPlaceholderRole,
        DiagnosticRole,
        SourceMetadataRole,
        ImportSettingsRole,
        Esp32TargetRole,
        CacheKeysRole,
    };

    explicit AssetBrowserModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;
    [[nodiscard]] const AssetBrowserEntry* entryAt(int row) const noexcept;
    [[nodiscard]] const AssetBrowserEntry* entry(const fabgl::AssetGuid& guid) const noexcept;
    [[nodiscard]] const QVector<AssetBrowserEntry>& entries() const noexcept;

  private:
    friend class AssetBrowserController;
    void replaceEntries(QVector<AssetBrowserEntry> entries);

    QVector<AssetBrowserEntry> entries_;
};

class AssetBrowserController final : public QObject {
    Q_OBJECT

  public:
    explicit AssetBrowserController(QObject* parent = nullptr);
    ~AssetBrowserController() override;

    AssetBrowserController(const AssetBrowserController&) = delete;
    AssetBrowserController& operator=(const AssetBrowserController&) = delete;

    [[nodiscard]] fabgl::Result<void> setProject(QString projectRoot,
                                                 QVector<AssetBrowserProjectEntry> entries,
                                                 AssetBrowserLimits limits = {});
    void clearProject();
    [[nodiscard]] fabgl::Result<void> refreshNow();
    void requestRefresh();
    void setExtensionImporterHooks(AssetBrowserExtensionImporterHooks hooks);
    void clearExtensionImporterHooks();

    [[nodiscard]] fabgl::Result<void> setImportSettings(const fabgl::AssetGuid& guid,
                                                        QString jsonSettings,
                                                        fabgl::assets::AssetTarget esp32Target);
    [[nodiscard]] fabgl::Result<void> setDependencies(const fabgl::AssetGuid& guid,
                                                      QVector<fabgl::AssetGuid> dependencies);
    [[nodiscard]] fabgl::Result<void> relocateAsset(const fabgl::AssetGuid& guid,
                                                    QString newRelativePath);

    [[nodiscard]] AssetBrowserModel* model() noexcept;
    [[nodiscard]] const AssetBrowserModel* model() const noexcept;
    [[nodiscard]] QString projectRoot() const;
    [[nodiscard]] QString metadataPath() const;
    [[nodiscard]] QString cachedPayloadPath(const fabgl::AssetGuid& guid,
                                            fabgl::assets::AssetTarget target) const;
    [[nodiscard]] AssetBrowserRefreshStats lastRefreshStats() const noexcept;
    [[nodiscard]] std::uint64_t refreshCount() const noexcept;
    [[nodiscard]] bool refreshPending() const noexcept;

  signals:
    void diagnosticRaised(const QString& relativePath, const QString& message);
    void refreshScheduled();
    void refreshed(qulonglong refreshCount, int imported, int cacheHits, int errors);
    void assetDiscovered(const QString& guid, const QString& relativePath, const QString& type);
    void assetMappingMoved(const QString& guid, const QString& oldRelativePath,
                           const QString& newRelativePath);
    void storageBudgetExceeded(const QString& storage, qulonglong usedBytes,
                               qulonglong budgetBytes);

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace fgl::studio
