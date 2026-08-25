#include "AssetBrowserController.h"

#include <fabgl/assets/image_pipeline.h>

#include <QColor>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace fgl::studio {
namespace {

[[nodiscard]] QString stateName(const AssetBrowserState state) {
    switch (state) {
    case AssetBrowserState::Clean:
        return AssetBrowserModel::tr("Ready");
    case AssetBrowserState::Dirty:
        return AssetBrowserModel::tr("Dirty");
    case AssetBrowserState::Missing:
        return AssetBrowserModel::tr("Missing");
    case AssetBrowserState::Error:
        return AssetBrowserModel::tr("Error");
    case AssetBrowserState::Unsupported:
        return AssetBrowserModel::tr("No importer");
    }
    return AssetBrowserModel::tr("Unknown");
}

[[nodiscard]] QString byteText(const std::uint64_t bytes) {
    if (bytes >= 1024U * 1024U) {
        return AssetBrowserModel::tr("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0),
                                                   0, 'f', 2);
    }
    if (bytes >= 1024U) {
        return AssetBrowserModel::tr("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    }
    return AssetBrowserModel::tr("%1 B").arg(static_cast<qulonglong>(bytes));
}

[[nodiscard]] QStringList guidList(const QVector<fabgl::AssetGuid>& values) {
    QStringList result;
    result.reserve(values.size());
    for (const auto guid : values) {
        result.push_back(QString::fromStdString(guid.toString()));
    }
    return result;
}

[[nodiscard]] QVariantMap costMap(const AssetBrowserCost& cost) {
    return {{QStringLiteral("payloadBytes"), QVariant::fromValue<qulonglong>(cost.payloadBytes)},
            {QStringLiteral("flashBytes"), QVariant::fromValue<qulonglong>(cost.flashBytes)},
            {QStringLiteral("internalRamBytes"),
             QVariant::fromValue<qulonglong>(cost.internalRamBytes)},
            {QStringLiteral("psramBytes"), QVariant::fromValue<qulonglong>(cost.psramBytes)},
            {QStringLiteral("sdBytes"), QVariant::fromValue<qulonglong>(cost.sdBytes)},
            {QStringLiteral("estimatedDecodeMicros"), cost.estimatedDecodeMicros},
            {QStringLiteral("estimatedRenderPixelsPerFrame"),
             QVariant::fromValue<qulonglong>(cost.estimatedRenderPixelsPerFrame)}};
}

[[nodiscard]] QVariantMap sourceMetadataMap(const AssetBrowserSourceMetadata& source) {
    return {{QStringLiteral("bytes"), QVariant::fromValue<qulonglong>(source.bytes)},
            {QStringLiteral("fingerprint"), QVariant::fromValue<qulonglong>(source.fingerprint)},
            {QStringLiteral("modifiedUtc"), source.modifiedUtc}};
}

[[nodiscard]] QString targetName(const fabgl::assets::AssetTarget target) {
    switch (target) {
    case fabgl::assets::AssetTarget::Pc:
        return QStringLiteral("pc");
    case fabgl::assets::AssetTarget::Esp32Flash:
        return QStringLiteral("flash");
    case fabgl::assets::AssetTarget::Esp32Psram:
        return QStringLiteral("psram");
    case fabgl::assets::AssetTarget::Esp32Sd:
        return QStringLiteral("sd");
    }
    return QStringLiteral("flash");
}

[[nodiscard]] QImage decodedThumbnail(const QByteArray& encoded) {
    const std::vector<std::uint8_t> bytes(encoded.cbegin(), encoded.cend());
    const auto decoded = fabgl::assets::decodeIndexedImage(bytes);
    if (!decoded) {
        return {};
    }
    const auto& source = decoded.value();
    QImage image(source.width, source.height, QImage::Format_RGBA8888);
    if (image.isNull()) {
        return {};
    }
    for (int y = 0; y < source.height; ++y) {
        auto* row = image.scanLine(y);
        for (int x = 0; x < source.width; ++x) {
            const auto pixel =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) +
                static_cast<std::size_t>(x);
            const auto color = source.palette[source.indices[pixel]];
            row[x * 4] = color.r;
            row[x * 4 + 1] = color.g;
            row[x * 4 + 2] = color.b;
            row[x * 4 + 3] = color.a;
        }
    }
    return image;
}

[[nodiscard]] QIcon placeholderIcon(const QString& type) {
    QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
    const auto hue = static_cast<int>(qHash(type) % 360U);
    image.fill(QColor::fromHsv(hue, 115, 145));
    QPainter painter(&image);
    painter.setPen(QColor(QStringLiteral("#f4f4f4")));
    painter.drawRect(1, 1, 29, 29);
    const QString label =
        type.trimmed().isEmpty() ? QStringLiteral("?") : type.trimmed().left(1).toUpper();
    painter.drawText(image.rect(), Qt::AlignCenter, label);
    return QIcon(QPixmap::fromImage(image));
}

} // namespace

AssetBrowserModel::AssetBrowserModel(QObject* parent) : QAbstractTableModel(parent) {}

int AssetBrowserModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

int AssetBrowserModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AssetBrowserModel::data(const QModelIndex& index, const int role) const {
    const auto* item = entryAt(index.row());
    if (!index.isValid() || item == nullptr || index.column() < 0 ||
        index.column() >= ColumnCount) {
        return {};
    }
    if (role == Qt::DecorationRole && index.column() == NameColumn) {
        const QImage thumbnail = decodedThumbnail(item->thumbnail);
        return thumbnail.isNull() ? placeholderIcon(item->type)
                                  : QIcon(QPixmap::fromImage(thumbnail));
    }
    if (role == Qt::ToolTipRole) {
        QString result = item->relativePath;
        if (!item->diagnostic.isEmpty()) {
            result += QStringLiteral("\n") + item->diagnostic;
        }
        result += tr("\nSource %1, modified %2; importer settings %3.")
                      .arg(byteText(item->source.bytes),
                           item->source.modifiedUtc.isValid()
                               ? item->source.modifiedUtc.toLocalTime().toString(Qt::ISODate)
                               : tr("unknown"),
                           item->normalizedSettings);
        result +=
            tr("\nPC payload %1; ESP32 flash %2, internal RAM %3, PSRAM %4, SD %5.")
                .arg(byteText(item->pcCost.payloadBytes), byteText(item->esp32Cost.flashBytes),
                     byteText(item->esp32Cost.internalRamBytes),
                     byteText(item->esp32Cost.psramBytes), byteText(item->esp32Cost.sdBytes));
        result += tr("\nEstimated decode %1 µs; render %2 pixels/frame.")
                      .arg(item->esp32Cost.estimatedDecodeMicros)
                      .arg(static_cast<qulonglong>(item->esp32Cost.estimatedRenderPixelsPerFrame));
        return result;
    }
    switch (role) {
    case GuidRole:
        return QString::fromStdString(item->guid.toString());
    case RelativePathRole:
        return item->relativePath;
    case TypeRole:
        return item->type;
    case StateRole:
        return static_cast<int>(item->state);
    case ImporterRole:
        return item->importer;
    case DependenciesRole:
        return guidList(item->dependencies);
    case DependentsRole:
        return guidList(item->dependents);
    case PcCostRole:
        return costMap(item->pcCost);
    case Esp32CostRole:
        return costMap(item->esp32Cost);
    case ThumbnailBytesRole:
        return item->thumbnail;
    case ThumbnailPlaceholderRole:
        return item->thumbnailPlaceholder;
    case DiagnosticRole:
        return item->diagnostic;
    case SourceMetadataRole:
        return sourceMetadataMap(item->source);
    case ImportSettingsRole:
        return item->normalizedSettings;
    case Esp32TargetRole:
        return targetName(item->esp32Target);
    case CacheKeysRole:
        return QVariantMap{
            {QStringLiteral("pc"), QVariant::fromValue<qulonglong>(item->pcCacheKey)},
            {QStringLiteral("esp32"), QVariant::fromValue<qulonglong>(item->esp32CacheKey)}};
    default:
        break;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case NameColumn:
        return item->relativePath.section(QLatin1Char('/'), -1);
    case TypeColumn:
        return item->type;
    case GuidColumn:
        return QString::fromStdString(item->guid.toString());
    case StateColumn:
        return stateName(item->state);
    case ImporterColumn:
        return item->importer;
    case DependenciesColumn:
        return item->dependencies.size();
    case DependentsColumn:
        return item->dependents.size();
    case PcBytesColumn:
        return byteText(item->pcCost.payloadBytes);
    case FlashBytesColumn:
        return byteText(item->esp32Cost.flashBytes);
    case InternalRamBytesColumn:
        return byteText(item->esp32Cost.internalRamBytes);
    case PsramBytesColumn:
        return byteText(item->esp32Cost.psramBytes);
    case SdBytesColumn:
        return byteText(item->esp32Cost.sdBytes);
    case DecodeCostColumn:
        return tr("%1 µs").arg(item->esp32Cost.estimatedDecodeMicros);
    case RenderCostColumn:
        return tr("%1 px").arg(
            static_cast<qulonglong>(item->esp32Cost.estimatedRenderPixelsPerFrame));
    default:
        return {};
    }
}

QVariant AssetBrowserModel::headerData(const int section, const Qt::Orientation orientation,
                                       const int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    static const QStringList Headers = {
        tr("Asset"),      tr("Type"),    tr("GUID"),       tr("State"),        tr("Importer"),
        tr("Depends on"), tr("Used by"), tr("PC payload"), tr("Flash"),        tr("Internal RAM"),
        tr("PSRAM"),      tr("SD"),      tr("Decode"),     tr("Render/frame"),
    };
    return section >= 0 && section < Headers.size() ? Headers.at(section) : QVariant{};
}

Qt::ItemFlags AssetBrowserModel::flags(const QModelIndex& index) const {
    auto result = QAbstractTableModel::flags(index);
    const auto* item = entryAt(index.row());
    if (index.isValid() && item != nullptr && !item->absolutePath.isEmpty() &&
        item->state != AssetBrowserState::Missing && QFileInfo(item->absolutePath).isFile()) {
        result |= Qt::ItemIsDragEnabled;
    }
    return result;
}

QStringList AssetBrowserModel::mimeTypes() const {
    return {QStringLiteral("text/uri-list"), QStringLiteral("application/x-fabgl-asset-guid"),
            QStringLiteral("application/x-fabgl-asset-path")};
}

QMimeData* AssetBrowserModel::mimeData(const QModelIndexList& indexes) const {
    auto* mime = new QMimeData;
    for (const auto& index : indexes) {
        const auto* item = entryAt(index.row());
        if (item == nullptr || item->absolutePath.isEmpty() ||
            !QFileInfo(item->absolutePath).isFile()) {
            continue;
        }
        mime->setUrls({QUrl::fromLocalFile(item->absolutePath)});
        mime->setData(QStringLiteral("application/x-fabgl-asset-guid"),
                      QByteArray::fromStdString(item->guid.toString()));
        mime->setData(QStringLiteral("application/x-fabgl-asset-path"),
                      item->relativePath.toUtf8());
        break;
    }
    return mime;
}

Qt::DropActions AssetBrowserModel::supportedDragActions() const {
    return Qt::CopyAction;
}

const AssetBrowserEntry* AssetBrowserModel::entryAt(const int row) const noexcept {
    return row >= 0 && row < entries_.size() ? &entries_.at(row) : nullptr;
}

const AssetBrowserEntry* AssetBrowserModel::entry(const fabgl::AssetGuid& guid) const noexcept {
    const auto found =
        std::find_if(entries_.cbegin(), entries_.cend(),
                     [guid](const AssetBrowserEntry& candidate) { return candidate.guid == guid; });
    return found == entries_.cend() ? nullptr : &*found;
}

const QVector<AssetBrowserEntry>& AssetBrowserModel::entries() const noexcept {
    return entries_;
}

void AssetBrowserModel::replaceEntries(QVector<AssetBrowserEntry> entries) {
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.relativePath.compare(right.relativePath, Qt::CaseInsensitive) < 0;
    });
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}

} // namespace fgl::studio
