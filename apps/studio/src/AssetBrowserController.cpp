#include "AssetBrowserController.h"

#include "AssetBrowserImporters.h"

#include <project_format.h>

#include <fabgl/assets/asset_database.h>
#include <fabgl/assets/file_io.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fgl::studio {
namespace {

constexpr auto MetadataDirectory = ".fabglstudio";
constexpr auto CacheDirectory = "cache";
constexpr auto AssetCacheDirectory = "assets";
constexpr auto MetadataFile = "asset-index-v1.json";
constexpr qsizetype MaximumSettingsBytes = 16 * 1024;
constexpr qsizetype MaximumMetadataBytes = 4 * 1024 * 1024;
constexpr int MaximumJsonDepth = 32;

struct PersistedImportState final {
    AssetBrowserSourceMetadata source;
    std::uint64_t importedSourceFingerprint = 0U;
    std::uint64_t settingsFingerprint = 0U;
    QString importer;
    std::uint32_t importerVersion = 0U;
    std::uint64_t pcCacheKey = 0U;
    std::uint64_t esp32CacheKey = 0U;
    AssetBrowserCost pcCost;
    AssetBrowserCost esp32Cost;
};

struct SourceSnapshot final {
    QString relativePath;
    QString absolutePath;
    AssetBrowserSourceMetadata metadata;
    std::vector<std::uint8_t> bytes;
};

struct SourceScan final {
    QMap<QString, SourceSnapshot> byPath;
    QStringList directories;
    QStringList files;
};

[[nodiscard]] fabgl::Result<void> failure(const fabgl::ErrorCode code, const QString& message,
                                          const QString& path = {}) {
    fabgl::Error error(code, message.toStdString());
    if (!path.isEmpty()) {
        error.addContext("path", path.toStdString());
    }
    return fabgl::Result<void>::failure(std::move(error));
}

template <typename T>
[[nodiscard]] fabgl::Result<T> failureValue(const fabgl::ErrorCode code, const QString& message,
                                            const QString& path = {}) {
    fabgl::Error error(code, message.toStdString());
    if (!path.isEmpty()) {
        error.addContext("path", path.toStdString());
    }
    return fabgl::Result<T>::failure(std::move(error));
}

[[nodiscard]] QString errorText(const fabgl::Error& error) {
    QString text = QString::fromStdString(error.message());
    for (const auto& context : error.context()) {
        text +=
            QStringLiteral(" [%1=%2]")
                .arg(QString::fromStdString(context.key), QString::fromStdString(context.value));
    }
    return text;
}

[[nodiscard]] QString portableRelativePath(QString path) {
    path = QDir::fromNativeSeparators(QDir::cleanPath(path.trimmed()));
    while (path.startsWith(QStringLiteral("./"))) {
        path.remove(0, 2);
    }
    return path;
}

[[nodiscard]] QString pathKey(QString path) {
    path = portableRelativePath(std::move(path));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

[[nodiscard]] bool isPrivatePath(const QString& relativePath) {
    const auto value = pathKey(relativePath);
    return value == QStringLiteral(".fabglstudio") ||
           value.startsWith(QStringLiteral(".fabglstudio/"));
}

[[nodiscard]] bool isSafeRelativeAssetPath(const QString& relativePath) {
    const auto path = portableRelativePath(relativePath);
    if (path.isEmpty() || isPrivatePath(path) ||
        !fabgl::assets::isSafeRelativePath(path.toUtf8().toStdString())) {
        return false;
    }
#ifdef Q_OS_WIN
    for (const auto& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part.contains(QLatin1Char(':'))) {
            return false;
        }
    }
#endif
    return true;
}

[[nodiscard]] QString comparableAbsolute(QString path) {
    path = QDir::fromNativeSeparators(QDir::cleanPath(std::move(path)));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

[[nodiscard]] bool isInsideRoot(const QString& canonicalRoot, const QString& candidate,
                                const bool allowRoot = false) {
    const auto root = comparableAbsolute(canonicalRoot);
    const auto value = comparableAbsolute(candidate);
    if (allowRoot && value == root) {
        return true;
    }
    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/'))) {
        prefix += QLatin1Char('/');
    }
    return value.startsWith(prefix);
}

[[nodiscard]] bool isLinkLikePath(const QString& path) {
    const QFileInfo info(path);
    if (info.isSymLink()) {
        return true;
    }
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    return false;
#endif
}

[[nodiscard]] bool isSafeExistingPath(const QString& canonicalRoot, const QString& absolutePath,
                                      const bool requireDirectory, QString* reason = nullptr) {
    const QFileInfo rootInfo(canonicalRoot);
    const QFileInfo candidateInfo(absolutePath);
    if (!rootInfo.isDir() || !candidateInfo.exists()) {
        if (reason != nullptr) {
            *reason = QStringLiteral("path does not exist");
        }
        return false;
    }
    if (isLinkLikePath(rootInfo.absoluteFilePath()) || isLinkLikePath(absolutePath)) {
        if (reason != nullptr) {
            *reason = QStringLiteral("path uses a symlink, junction, or reparse point");
        }
        return false;
    }
    if ((requireDirectory && !candidateInfo.isDir()) ||
        (!requireDirectory && !candidateInfo.isFile())) {
        if (reason != nullptr) {
            *reason = QStringLiteral("path has the wrong file type");
        }
        return false;
    }
    const auto candidateCanonical = candidateInfo.canonicalFilePath();
    if (candidateCanonical.isEmpty() || !isInsideRoot(canonicalRoot, candidateCanonical, true)) {
        if (reason != nullptr) {
            *reason = QStringLiteral("canonical path resolves outside the project root");
        }
        return false;
    }
    const auto lexicalRelative = portableRelativePath(
        QDir(canonicalRoot).relativeFilePath(candidateInfo.absoluteFilePath()));
    const auto parts = lexicalRelative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.contains(QStringLiteral(".."))) {
        if (reason != nullptr) {
            *reason = QStringLiteral("path contains traversal");
        }
        return false;
    }
    QString current = canonicalRoot;
    for (const auto& part : parts) {
        current = QDir(current).filePath(part);
        if (isLinkLikePath(current)) {
            if (reason != nullptr) {
                *reason =
                    QStringLiteral("path component uses a symlink, junction, or reparse point");
            }
            return false;
        }
    }
    return true;
}

[[nodiscard]] fabgl::Result<QString> validateWritablePath(const QString& canonicalRoot,
                                                          const QString& absolutePath,
                                                          const bool allowExistingFile) {
    const QFileInfo requestedInfo(absolutePath);
    const auto requested = QDir::cleanPath(requestedInfo.absoluteFilePath());
    if (!isInsideRoot(canonicalRoot, requested)) {
        return failureValue<QString>(
            fabgl::ErrorCode::InvalidArgument,
            QStringLiteral("write destination is outside the project root"), requested);
    }
    const auto relative = portableRelativePath(QDir(canonicalRoot).relativeFilePath(requested));
    if (!fabgl::assets::isSafeRelativePath(relative.toUtf8().toStdString())) {
        return failureValue<QString>(fabgl::ErrorCode::InvalidArgument,
                                     QStringLiteral("write destination contains traversal"),
                                     relative);
    }
#ifdef Q_OS_WIN
    if (requestedInfo.fileName().contains(QLatin1Char(':'))) {
        return failureValue<QString>(fabgl::ErrorCode::InvalidArgument,
                                     QStringLiteral("alternate data streams are not allowed"),
                                     relative);
    }
#endif
    QString current = canonicalRoot;
    const auto parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype index = 0; index + 1 < parts.size(); ++index) {
        current = QDir(current).filePath(parts.at(index));
        if (!isSafeExistingPath(canonicalRoot, current, true)) {
            return failureValue<QString>(
                fabgl::ErrorCode::InvalidArgument,
                QStringLiteral("write directory uses a missing or link-like path component"),
                relative);
        }
    }
    const QFileInfo parentInfo(requestedInfo.absolutePath());
    if (!isSafeExistingPath(canonicalRoot, parentInfo.absoluteFilePath(), true)) {
        return failureValue<QString>(fabgl::ErrorCode::InvalidArgument,
                                     QStringLiteral("write parent is not a safe project directory"),
                                     relative);
    }
    if (requestedInfo.exists() &&
        (!allowExistingFile || !isSafeExistingPath(canonicalRoot, requested, false))) {
        return failureValue<QString>(fabgl::ErrorCode::AlreadyExists,
                                     QStringLiteral("write target is not a safe replaceable file"),
                                     relative);
    }
    return fabgl::Result<QString>::success(requested);
}

[[nodiscard]] std::uint64_t fingerprint(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint64_t value = 1469598103934665603ULL;
    for (const auto byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    value ^= static_cast<std::uint64_t>(bytes.size());
    value *= 1099511628211ULL;
    return value == 0U ? 1U : value;
}

[[nodiscard]] std::uint64_t fingerprint(const QByteArray& bytes) noexcept {
    const std::vector<std::uint8_t> values(bytes.cbegin(), bytes.cend());
    return fingerprint(values);
}

[[nodiscard]] QString hexValue(const std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value), 16).rightJustified(16, QLatin1Char('0'));
}

[[nodiscard]] std::uint64_t parseHex(const QJsonValue& value, bool* valid = nullptr) {
    bool converted = false;
    const auto result = value.toString().toULongLong(&converted, 16);
    if (valid != nullptr) {
        *valid = converted;
    }
    return converted ? static_cast<std::uint64_t>(result) : 0U;
}

[[nodiscard]] QJsonValue canonicalJsonValue(const QJsonValue& value, const int depth, bool& valid) {
    if (depth > MaximumJsonDepth) {
        valid = false;
        return {};
    }
    if (value.isArray()) {
        QJsonArray output;
        for (const auto& item : value.toArray()) {
            output.push_back(canonicalJsonValue(item, depth + 1, valid));
            if (!valid) {
                return {};
            }
        }
        return output;
    }
    if (value.isObject()) {
        const auto object = value.toObject();
        auto keys = object.keys();
        std::sort(keys.begin(), keys.end());
        QJsonObject output;
        for (const auto& key : keys) {
            output.insert(key, canonicalJsonValue(object.value(key), depth + 1, valid));
            if (!valid) {
                return {};
            }
        }
        return output;
    }
    return value;
}

[[nodiscard]] fabgl::Result<QString> normalizedSettings(QString json) {
    json = json.trimmed();
    if (json.isEmpty()) {
        return fabgl::Result<QString>::success(QStringLiteral("{}"));
    }
    if (json.toUtf8().size() > MaximumSettingsBytes) {
        return failureValue<QString>(fabgl::ErrorCode::CapacityExceeded,
                                     QStringLiteral("asset import settings exceed 16 KiB"));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failureValue<QString>(
            fabgl::ErrorCode::InvalidFormat,
            QStringLiteral("asset import settings must be a JSON object: %1")
                .arg(parseError.errorString()));
    }
    bool valid = true;
    const auto canonical = canonicalJsonValue(document.object(), 0, valid);
    if (!valid || !canonical.isObject()) {
        return failureValue<QString>(fabgl::ErrorCode::CapacityExceeded,
                                     QStringLiteral("asset import settings are too deeply nested"));
    }
    const auto bytes = QJsonDocument(canonical.toObject()).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumSettingsBytes) {
        return failureValue<QString>(fabgl::ErrorCode::CapacityExceeded,
                                     QStringLiteral("normalized asset settings exceed 16 KiB"));
    }
    return fabgl::Result<QString>::success(QString::fromUtf8(bytes));
}

[[nodiscard]] QString inferredType(const QString& relativePath,
                                   const std::vector<std::uint8_t>& bytes) {
    const auto extension = QFileInfo(relativePath).suffix().trimmed().toLower();
    if (QStringList{QStringLiteral("bmp"), QStringLiteral("jpeg"), QStringLiteral("jpg"),
                    QStringLiteral("png"), QStringLiteral("fgli")}
            .contains(extension))
        return QStringLiteral("image");
    if (extension == QStringLiteral("wav") || extension == QStringLiteral("fgla"))
        return QStringLiteral("audio");
    if (extension == QStringLiteral("csv") || extension == QStringLiteral("fglt") ||
        extension == QStringLiteral("fgltilemap"))
        return QStringLiteral("tilemap");
    if (extension == QStringLiteral("fgltileset"))
        return QStringLiteral("tileset");
    if (extension == QStringLiteral("obj") || extension == QStringLiteral("fglm"))
        return QStringLiteral("mesh");
    if (extension == QStringLiteral("bdf") || extension == QStringLiteral("fglf"))
        return QStringLiteral("font");
    if (extension == QStringLiteral("fglscene"))
        return QStringLiteral("scene");
    if (extension == QStringLiteral("fglprefab"))
        return QStringLiteral("prefab");
    if (extension == QStringLiteral("fglmaterial"))
        return QStringLiteral("material");
    if (extension == QStringLiteral("fglanim"))
        return QStringLiteral("animation.clip");
    if (extension == QStringLiteral("fglcontroller"))
        return QStringLiteral("animation.controller");
    if (extension == QStringLiteral("fgltrack"))
        return QStringLiteral("racer.track");
    if (extension == QStringLiteral("fglray"))
        return QStringLiteral("raycast.map");
    if (extension == QStringLiteral("fgls"))
        return QStringLiteral("sprite.atlas");
    if (extension == QStringLiteral("fglvisual"))
        return QStringLiteral("visual.script");
    if (extension == QStringLiteral("cpp") || extension == QStringLiteral("c") ||
        extension == QStringLiteral("h") || extension == QStringLiteral("hpp"))
        return QStringLiteral("script");
    if (extension == QStringLiteral("json")) {
        const QByteArray json(reinterpret_cast<const char*>(bytes.data()),
                              static_cast<qsizetype>(bytes.size()));
        const auto object = QJsonDocument::fromJson(json).object();
        if ((object.contains(QStringLiteral("width")) &&
             object.contains(QStringLiteral("height"))) ||
            object.contains(QStringLiteral("layers"))) {
            return QStringLiteral("tilemap");
        }
        return QStringLiteral("json");
    }
    return QStringLiteral("binary");
}

void normalizeDependencies(QVector<fabgl::AssetGuid>& values) {
    values.erase(
        std::remove_if(values.begin(), values.end(), [](const auto& guid) { return guid.isNil(); }),
        values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

[[nodiscard]] AssetBrowserCost costFor(const fabgl::assets::ImportedAsset& imported) {
    AssetBrowserCost cost;
    cost.payloadBytes = imported.payload.size();
    cost.flashBytes = imported.flashBytes;
    cost.internalRamBytes = imported.internalRamBytes;
    cost.psramBytes = imported.psramBytes;
    cost.sdBytes = imported.sdBytes;
    cost.estimatedDecodeMicros = imported.estimatedDecodeMicros;
    cost.estimatedRenderPixelsPerFrame = imported.estimatedRenderPixelsPerFrame;
    return cost;
}

[[nodiscard]] QJsonObject costJson(const AssetBrowserCost& cost) {
    return {{QStringLiteral("payload"), hexValue(cost.payloadBytes)},
            {QStringLiteral("flash"), hexValue(cost.flashBytes)},
            {QStringLiteral("internalRam"), hexValue(cost.internalRamBytes)},
            {QStringLiteral("psram"), hexValue(cost.psramBytes)},
            {QStringLiteral("sd"), hexValue(cost.sdBytes)},
            {QStringLiteral("decodeMicros"), static_cast<qint64>(cost.estimatedDecodeMicros)},
            {QStringLiteral("renderPixels"), hexValue(cost.estimatedRenderPixelsPerFrame)}};
}

[[nodiscard]] AssetBrowserCost costFromJson(const QJsonObject& object) {
    AssetBrowserCost cost;
    cost.payloadBytes = parseHex(object.value(QStringLiteral("payload")));
    cost.flashBytes = parseHex(object.value(QStringLiteral("flash")));
    cost.internalRamBytes = parseHex(object.value(QStringLiteral("internalRam")));
    cost.psramBytes = parseHex(object.value(QStringLiteral("psram")));
    cost.sdBytes = parseHex(object.value(QStringLiteral("sd")));
    cost.estimatedDecodeMicros = static_cast<std::uint32_t>(
        std::clamp<qint64>(object.value(QStringLiteral("decodeMicros")).toInteger(), 0,
                           std::numeric_limits<std::uint32_t>::max()));
    cost.estimatedRenderPixelsPerFrame = parseHex(object.value(QStringLiteral("renderPixels")));
    return cost;
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

[[nodiscard]] fabgl::assets::AssetTarget targetFromJson(const QJsonValue& value) {
    const auto text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("psram"))
        return fabgl::assets::AssetTarget::Esp32Psram;
    if (text == QStringLiteral("sd"))
        return fabgl::assets::AssetTarget::Esp32Sd;
    return fabgl::assets::AssetTarget::Esp32Flash;
}

[[nodiscard]] std::string utf8String(const QString& value) {
    const auto bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

[[nodiscard]] QByteArray byteArray(const std::vector<std::uint8_t>& value) {
    return QByteArray(reinterpret_cast<const char*>(value.data()),
                      static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::vector<std::uint8_t> byteVector(const QByteArray& value) {
    return std::vector<std::uint8_t>(value.cbegin(), value.cend());
}

} // namespace

class AssetBrowserController::Implementation final {
  public:
    explicit Implementation(AssetBrowserController& owner)
        : owner_(owner), model_(std::make_unique<AssetBrowserModel>(&owner)) {
        debounceTimer_.setSingleShot(true);
        QObject::connect(&debounceTimer_, &QTimer::timeout, &owner_, [this]() {
            refreshPending_ = false;
            const auto result = owner_.refreshNow();
            if (!result) {
                emit owner_.diagnosticRaised({}, errorText(result.error()));
            }
        });
        const auto changed = [this](const QString&) { scheduleRefresh(); };
        QObject::connect(&watcher_, &QFileSystemWatcher::fileChanged, &owner_, changed);
        QObject::connect(&watcher_, &QFileSystemWatcher::directoryChanged, &owner_, changed);
    }

    AssetBrowserController& owner_;
    std::unique_ptr<AssetBrowserModel> model_;
    QFileSystemWatcher watcher_;
    QTimer debounceTimer_;
    QElapsedTimer debounceWindow_;
    QString root_;
    AssetBrowserLimits limits_;
    QVector<AssetBrowserProjectEntry> mappings_;
    QMap<QString, PersistedImportState> importStates_;
    AssetBrowserRefreshStats lastStats_;
    AssetBrowserExtensionImporterHooks extensionImporterHooks_;
    std::uint64_t refreshCount_ = 0U;
    bool refreshPending_ = false;
    bool refreshing_ = false;

    [[nodiscard]] QString metadataPath() const {
        return root_.isEmpty() ? QString{}
                               : QDir(QDir(root_).filePath(QString::fromLatin1(MetadataDirectory)))
                                     .filePath(QString::fromLatin1(MetadataFile));
    }

    [[nodiscard]] QString cacheRoot() const {
        return QDir(QDir(QDir(root_).filePath(QString::fromLatin1(MetadataDirectory)))
                        .filePath(QString::fromLatin1(CacheDirectory)))
            .filePath(QString::fromLatin1(AssetCacheDirectory));
    }

    [[nodiscard]] QString cachePath(const fabgl::AssetGuid& guid, const std::uint64_t key,
                                    const QString& suffix) const {
        const auto guidText = QString::fromStdString(guid.toString());
        return QDir(QDir(cacheRoot()).filePath(guidText)).filePath(hexValue(key) + suffix);
    }

    [[nodiscard]] fabgl::Result<void> ensureDirectory(const QString& parent,
                                                      const QString& child) const {
        const auto target = QDir(parent).filePath(child);
        if (!QFileInfo::exists(target) && !QDir(parent).mkdir(child)) {
            return failure(fabgl::ErrorCode::IoError,
                           QStringLiteral("failed to create private asset cache directory"),
                           target);
        }
        if (!isSafeExistingPath(root_, target, true)) {
            return failure(fabgl::ErrorCode::InvalidArgument,
                           QStringLiteral("private asset cache directory is link-like or escaped"),
                           target);
        }
        return fabgl::Result<void>::success();
    }

    [[nodiscard]] fabgl::Result<void>
    ensurePrivateDirectories(const std::optional<fabgl::AssetGuid>& guid = std::nullopt) const {
        auto result = ensureDirectory(root_, QString::fromLatin1(MetadataDirectory));
        if (!result)
            return result;
        const auto metadataRoot = QDir(root_).filePath(QString::fromLatin1(MetadataDirectory));
        result = ensureDirectory(metadataRoot, QString::fromLatin1(CacheDirectory));
        if (!result)
            return result;
        const auto cache = QDir(metadataRoot).filePath(QString::fromLatin1(CacheDirectory));
        result = ensureDirectory(cache, QString::fromLatin1(AssetCacheDirectory));
        if (!result || !guid.has_value())
            return result;
        return ensureDirectory(cacheRoot(), QString::fromStdString(guid->toString()));
    }

    [[nodiscard]] fabgl::Result<SourceSnapshot> readSource(const QString& relativePath,
                                                           std::uint64_t& aggregateBytes) const {
        if (!isSafeRelativeAssetPath(relativePath)) {
            return failureValue<SourceSnapshot>(fabgl::ErrorCode::InvalidArgument,
                                                QStringLiteral("asset path is unsafe"),
                                                relativePath);
        }
        const auto absolute = QDir(root_).filePath(relativePath);
        QString reason;
        if (!isSafeExistingPath(root_, absolute, false, &reason)) {
            return failureValue<SourceSnapshot>(
                fabgl::ErrorCode::InvalidArgument,
                QStringLiteral("asset source is not a safe regular project file: %1").arg(reason),
                relativePath);
        }
        QFile file(absolute);
        const auto size = file.size();
        if (size < 0 || static_cast<std::uint64_t>(size) > limits_.maximumSourceBytes) {
            return failureValue<SourceSnapshot>(
                fabgl::ErrorCode::CapacityExceeded,
                QStringLiteral("asset source exceeds the size limit"), relativePath);
        }
        const auto unsignedSize = static_cast<std::uint64_t>(size);
        if (aggregateBytes > limits_.maximumAggregateSourceBytes - unsignedSize) {
            return failureValue<SourceSnapshot>(
                fabgl::ErrorCode::CapacityExceeded,
                QStringLiteral("aggregate asset source size exceeds the scan limit"), relativePath);
        }
        if (!file.open(QIODevice::ReadOnly)) {
            return failureValue<SourceSnapshot>(fabgl::ErrorCode::IoError, file.errorString(),
                                                relativePath);
        }
        const auto contents = file.readAll();
        if (contents.size() != size) {
            return failureValue<SourceSnapshot>(
                fabgl::ErrorCode::IoError, QStringLiteral("asset source changed while reading"),
                relativePath);
        }
        aggregateBytes += unsignedSize;
        SourceSnapshot snapshot;
        snapshot.relativePath = portableRelativePath(relativePath);
        snapshot.absolutePath = QFileInfo(absolute).canonicalFilePath();
        snapshot.bytes = byteVector(contents);
        snapshot.metadata.bytes = unsignedSize;
        snapshot.metadata.fingerprint = fingerprint(snapshot.bytes);
        snapshot.metadata.modifiedUtc = QFileInfo(absolute).lastModified().toUTC();
        return fabgl::Result<SourceSnapshot>::success(std::move(snapshot));
    }

    [[nodiscard]] fabgl::Result<void> scanDirectory(const QString& absoluteDirectory,
                                                    SourceScan& scan,
                                                    std::uint64_t& aggregateBytes) const {
        if (static_cast<std::size_t>(scan.directories.size()) >= limits_.maximumDirectories) {
            return failure(fabgl::ErrorCode::CapacityExceeded,
                           QStringLiteral("asset directory count exceeds the scan limit"),
                           absoluteDirectory);
        }
        if (!isSafeExistingPath(root_, absoluteDirectory, true)) {
            return failure(fabgl::ErrorCode::InvalidArgument,
                           QStringLiteral("asset directory is link-like or outside the project"),
                           absoluteDirectory);
        }
        scan.directories.push_back(QFileInfo(absoluteDirectory).canonicalFilePath());
        const QDir directory(absoluteDirectory);
        const auto entries = directory.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
        for (const auto& info : entries) {
            if (isLinkLikePath(info.absoluteFilePath())) {
                return failure(
                    fabgl::ErrorCode::InvalidArgument,
                    QStringLiteral("asset tree contains a symlink, junction, or reparse point"),
                    QDir(root_).relativeFilePath(info.absoluteFilePath()));
            }
            if (info.isDir()) {
                auto nested = scanDirectory(info.absoluteFilePath(), scan, aggregateBytes);
                if (!nested)
                    return nested;
                continue;
            }
            if (!info.isFile()) {
                return failure(fabgl::ErrorCode::InvalidArgument,
                               QStringLiteral("asset tree contains a non-regular file"),
                               info.absoluteFilePath());
            }
            if (scan.byPath.size() >= static_cast<qsizetype>(limits_.maximumAssets)) {
                return failure(fabgl::ErrorCode::CapacityExceeded,
                               QStringLiteral("asset count exceeds the scan limit"));
            }
            const auto relative =
                portableRelativePath(QDir(root_).relativeFilePath(info.absoluteFilePath()));
            auto source = readSource(relative, aggregateBytes);
            if (!source)
                return fabgl::Result<void>::failure(source.error());
            const auto key = pathKey(relative);
            if (scan.byPath.contains(key)) {
                return failure(fabgl::ErrorCode::AlreadyExists,
                               QStringLiteral("asset tree contains a case-colliding path"),
                               relative);
            }
            scan.files.push_back(source.value().absolutePath);
            scan.byPath.insert(key, std::move(source.value()));
        }
        return fabgl::Result<void>::success();
    }

    [[nodiscard]] fabgl::Result<SourceScan> scanSources() const {
        SourceScan scan;
        std::uint64_t aggregateBytes = 0U;
        const auto assetsDirectory = QDir(root_).filePath(QStringLiteral("Assets"));
        if (QFileInfo::exists(assetsDirectory)) {
            auto scanned = scanDirectory(assetsDirectory, scan, aggregateBytes);
            if (!scanned)
                return fabgl::Result<SourceScan>::failure(scanned.error());
        } else {
            scan.directories.push_back(root_);
        }
        for (const auto& mapping : mappings_) {
            const auto key = pathKey(mapping.relativePath);
            if (scan.byPath.contains(key) ||
                !QFileInfo::exists(QDir(root_).filePath(mapping.relativePath))) {
                continue;
            }
            auto source = readSource(mapping.relativePath, aggregateBytes);
            if (!source)
                return fabgl::Result<SourceScan>::failure(source.error());
            scan.files.push_back(source.value().absolutePath);
            const auto parent = QFileInfo(source.value().absolutePath).canonicalPath();
            if (!scan.directories.contains(parent))
                scan.directories.push_back(parent);
            scan.byPath.insert(key, std::move(source.value()));
        }
        return fabgl::Result<SourceScan>::success(std::move(scan));
    }

    [[nodiscard]] fabgl::Result<void> atomicWrite(const QString& path,
                                                  const QByteArray& contents) const {
        auto target = validateWritablePath(root_, path, true);
        if (!target)
            return fabgl::Result<void>::failure(target.error());
        QSaveFile file(target.value());
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() ||
            !file.commit()) {
            return failure(
                fabgl::ErrorCode::IoError,
                QStringLiteral("atomic asset cache write failed: %1").arg(file.errorString()),
                path);
        }
        if (!isSafeExistingPath(root_, path, false)) {
            return failure(fabgl::ErrorCode::InvalidState,
                           QStringLiteral("asset cache path changed identity after write"), path);
        }
        return fabgl::Result<void>::success();
    }

    [[nodiscard]] bool validCacheFile(const QString& path,
                                      const std::uint64_t expectedBytes) const {
        if (!isSafeExistingPath(root_, path, false)) {
            return false;
        }
        const auto size = QFileInfo(path).size();
        return size >= 0 && static_cast<std::uint64_t>(size) == expectedBytes;
    }

    [[nodiscard]] QByteArray readSafeCacheFile(const QString& path,
                                               const std::uint64_t maximumBytes) const {
        if (!isSafeExistingPath(root_, path, false))
            return {};
        QFile file(path);
        const auto size = file.size();
        if (size < 0 || static_cast<std::uint64_t>(size) > maximumBytes ||
            !file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    [[nodiscard]] fabgl::Result<void> loadMetadata() {
        importStates_.clear();
        const auto path = metadataPath();
        if (!QFileInfo::exists(path))
            return fabgl::Result<void>::success();
        if (!isSafeExistingPath(root_, path, false)) {
            return failure(fabgl::ErrorCode::InvalidArgument,
                           QStringLiteral("asset metadata is link-like or outside the project"),
                           path);
        }
        QFile file(path);
        if (file.size() < 0 || file.size() > MaximumMetadataBytes ||
            !file.open(QIODevice::ReadOnly)) {
            return failure(fabgl::ErrorCode::IoError,
                           QStringLiteral("asset metadata is unreadable or exceeds 4 MiB"), path);
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return failure(
                fabgl::ErrorCode::InvalidFormat,
                QStringLiteral("asset metadata JSON is invalid: %1").arg(parseError.errorString()),
                path);
        }
        const auto root = document.object();
        if (root.value(QStringLiteral("kind")).toString() != QStringLiteral("fabgl.asset-index") ||
            root.value(QStringLiteral("version")).toInt() != 1) {
            return failure(fabgl::ErrorCode::UnsupportedVersion,
                           QStringLiteral("asset metadata kind/version is unsupported"), path);
        }
        const auto records = root.value(QStringLiteral("assets")).toArray();
        if (records.size() > static_cast<qsizetype>(limits_.maximumAssets)) {
            return failure(fabgl::ErrorCode::CapacityExceeded,
                           QStringLiteral("asset metadata contains too many records"), path);
        }
        QSet<QString> seenPaths;
        QSet<QString> seenGuids;
        for (const auto& value : records) {
            if (!value.isObject()) {
                return failure(fabgl::ErrorCode::InvalidFormat,
                               QStringLiteral("asset metadata record is not an object"), path);
            }
            const auto object = value.toObject();
            const auto guidText = object.value(QStringLiteral("guid")).toString();
            auto guid = fabgl::AssetGuid::parse(utf8String(guidText));
            const auto relativePath =
                portableRelativePath(object.value(QStringLiteral("path")).toString());
            if (!guid || !isSafeRelativeAssetPath(relativePath) || seenGuids.contains(guidText) ||
                seenPaths.contains(pathKey(relativePath))) {
                return failure(
                    fabgl::ErrorCode::InvalidFormat,
                    QStringLiteral("asset metadata contains an invalid or duplicate mapping"),
                    relativePath);
            }
            auto settings = normalizedSettings(object.value(QStringLiteral("settings")).toString());
            if (!settings)
                return fabgl::Result<void>::failure(settings.error());
            AssetBrowserProjectEntry mapping;
            mapping.guid = guid.value();
            mapping.relativePath = relativePath;
            mapping.type = object.value(QStringLiteral("type")).toString().trimmed().toLower();
            mapping.normalizedSettings = settings.value();
            mapping.esp32Target = targetFromJson(object.value(QStringLiteral("esp32Target")));
            const auto dependencyValues = object.value(QStringLiteral("dependencies")).toArray();
            if (dependencyValues.size() > static_cast<qsizetype>(limits_.maximumAssets)) {
                return failure(fabgl::ErrorCode::CapacityExceeded,
                               QStringLiteral("asset metadata dependency list is too large"),
                               relativePath);
            }
            for (const auto& dependencyValue : dependencyValues) {
                auto dependency = fabgl::AssetGuid::parse(utf8String(dependencyValue.toString()));
                if (!dependency) {
                    return failure(fabgl::ErrorCode::InvalidFormat,
                                   QStringLiteral("asset metadata dependency GUID is invalid"),
                                   relativePath);
                }
                mapping.dependencies.push_back(dependency.value());
            }
            normalizeDependencies(mapping.dependencies);
            mappings_.push_back(mapping);

            PersistedImportState state;
            const auto source = object.value(QStringLiteral("source")).toObject();
            state.source.bytes = parseHex(source.value(QStringLiteral("bytes")));
            state.source.fingerprint = parseHex(source.value(QStringLiteral("fingerprint")));
            state.source.modifiedUtc = QDateTime::fromString(
                source.value(QStringLiteral("modifiedUtc")).toString(), Qt::ISODateWithMs);
            const auto imported = object.value(QStringLiteral("imported")).toObject();
            state.importedSourceFingerprint =
                parseHex(imported.value(QStringLiteral("sourceFingerprint")));
            state.settingsFingerprint =
                parseHex(imported.value(QStringLiteral("settingsFingerprint")));
            state.importer = imported.value(QStringLiteral("importer")).toString();
            state.importerVersion = static_cast<std::uint32_t>(
                std::max(0, imported.value(QStringLiteral("importerVersion")).toInt()));
            state.pcCacheKey = parseHex(imported.value(QStringLiteral("pcCacheKey")));
            state.esp32CacheKey = parseHex(imported.value(QStringLiteral("esp32CacheKey")));
            state.pcCost = costFromJson(imported.value(QStringLiteral("pcCost")).toObject());
            state.esp32Cost = costFromJson(imported.value(QStringLiteral("esp32Cost")).toObject());
            importStates_.insert(guidText, state);
            seenGuids.insert(guidText);
            seenPaths.insert(pathKey(relativePath));
        }
        return fabgl::Result<void>::success();
    }

    [[nodiscard]] fabgl::Result<void> persistMetadata() const {
        auto prepared = ensurePrivateDirectories();
        if (!prepared)
            return prepared;
        QJsonArray records;
        auto mappings = mappings_;
        std::sort(mappings.begin(), mappings.end(),
                  [](const auto& left, const auto& right) { return left.guid < right.guid; });
        for (const auto& mapping : mappings) {
            QJsonObject object;
            const auto guidText = QString::fromStdString(mapping.guid.toString());
            object.insert(QStringLiteral("guid"), guidText);
            object.insert(QStringLiteral("path"), mapping.relativePath);
            object.insert(QStringLiteral("type"), mapping.type);
            object.insert(QStringLiteral("settings"), mapping.normalizedSettings);
            object.insert(QStringLiteral("esp32Target"), targetName(mapping.esp32Target));
            QJsonArray dependencies;
            for (const auto& dependency : mapping.dependencies) {
                dependencies.push_back(QString::fromStdString(dependency.toString()));
            }
            object.insert(QStringLiteral("dependencies"), dependencies);
            const auto state = importStates_.value(guidText);
            object.insert(
                QStringLiteral("source"),
                QJsonObject{{QStringLiteral("bytes"), hexValue(state.source.bytes)},
                            {QStringLiteral("fingerprint"), hexValue(state.source.fingerprint)},
                            {QStringLiteral("modifiedUtc"),
                             state.source.modifiedUtc.toUTC().toString(Qt::ISODateWithMs)}});
            object.insert(
                QStringLiteral("imported"),
                QJsonObject{
                    {QStringLiteral("sourceFingerprint"),
                     hexValue(state.importedSourceFingerprint)},
                    {QStringLiteral("settingsFingerprint"), hexValue(state.settingsFingerprint)},
                    {QStringLiteral("importer"), state.importer},
                    {QStringLiteral("importerVersion"), static_cast<qint64>(state.importerVersion)},
                    {QStringLiteral("pcCacheKey"), hexValue(state.pcCacheKey)},
                    {QStringLiteral("esp32CacheKey"), hexValue(state.esp32CacheKey)},
                    {QStringLiteral("pcCost"), costJson(state.pcCost)},
                    {QStringLiteral("esp32Cost"), costJson(state.esp32Cost)}});
            records.push_back(object);
        }
        const QJsonObject root{{QStringLiteral("kind"), QStringLiteral("fabgl.asset-index")},
                               {QStringLiteral("version"), 1},
                               {QStringLiteral("assets"), records}};
        const auto contents = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (contents.size() > MaximumMetadataBytes) {
            return failure(fabgl::ErrorCode::CapacityExceeded,
                           QStringLiteral("asset metadata exceeds the 4 MiB limit"));
        }
        return atomicWrite(metadataPath(), contents);
    }

    void replaceWatches(const SourceScan& scan) {
        const auto oldFiles = watcher_.files();
        const auto oldDirectories = watcher_.directories();
        if (!oldFiles.isEmpty())
            watcher_.removePaths(oldFiles);
        if (!oldDirectories.isEmpty())
            watcher_.removePaths(oldDirectories);
        QStringList directories = scan.directories;
        directories.removeDuplicates();
        std::sort(directories.begin(), directories.end());
        if (directories.size() > static_cast<qsizetype>(limits_.maximumWatchedDirectories)) {
            directories =
                directories.mid(0, static_cast<qsizetype>(limits_.maximumWatchedDirectories));
        }
        QStringList files = scan.files;
        files.removeDuplicates();
        std::sort(files.begin(), files.end());
        if (files.size() > static_cast<qsizetype>(limits_.maximumWatchedFiles)) {
            files = files.mid(0, static_cast<qsizetype>(limits_.maximumWatchedFiles));
        }
        if (!directories.isEmpty())
            watcher_.addPaths(directories);
        if (!files.isEmpty())
            watcher_.addPaths(files);
    }

    void scheduleRefresh() {
        if (root_.isEmpty())
            return;
        const int debounce = std::clamp(limits_.debounceMilliseconds, 0, 60'000);
        const int maximum = std::clamp(limits_.maximumDebounceMilliseconds, debounce, 60'000);
        if (!refreshPending_) {
            refreshPending_ = true;
            debounceWindow_.restart();
            emit owner_.refreshScheduled();
        }
        const auto elapsed = static_cast<int>(
            std::min<qint64>(debounceWindow_.elapsed(), std::numeric_limits<int>::max()));
        const auto remaining = std::max(0, maximum - elapsed);
        debounceTimer_.start(std::min(debounce, remaining));
    }

    [[nodiscard]] int mappingIndex(const fabgl::AssetGuid& guid) const noexcept {
        for (qsizetype index = 0; index < mappings_.size(); ++index) {
            if (mappings_.at(index).guid == guid)
                return static_cast<int>(index);
        }
        return -1;
    }

    void reportDiagnostic(const QString& path, const QString& message) {
        emit owner_.diagnosticRaised(path, message);
    }

    [[nodiscard]] fabgl::Result<void> reconcileMappings(SourceScan& scan) {
        QSet<QString> claimed;
        for (const auto& mapping : mappings_) {
            if (scan.byPath.contains(pathKey(mapping.relativePath)))
                claimed.insert(pathKey(mapping.relativePath));
        }
        for (auto& mapping : mappings_) {
            const auto oldKey = pathKey(mapping.relativePath);
            if (scan.byPath.contains(oldKey))
                continue;
            const auto state = importStates_.value(QString::fromStdString(mapping.guid.toString()));
            if (state.source.fingerprint == 0U)
                continue;
            QString candidate;
            for (auto it = scan.byPath.cbegin(); it != scan.byPath.cend(); ++it) {
                if (claimed.contains(it.key()) ||
                    it.value().metadata.fingerprint != state.source.fingerprint ||
                    it.value().metadata.bytes != state.source.bytes) {
                    continue;
                }
                if (!candidate.isEmpty()) {
                    candidate.clear();
                    break;
                }
                candidate = it.key();
            }
            if (candidate.isEmpty())
                continue;
            const auto oldPath = mapping.relativePath;
            mapping.relativePath = scan.byPath.value(candidate).relativePath;
            claimed.insert(candidate);
            ++lastStats_.mappingsMoved;
            emit owner_.assetMappingMoved(QString::fromStdString(mapping.guid.toString()), oldPath,
                                          mapping.relativePath);
        }
        for (auto it = scan.byPath.cbegin(); it != scan.byPath.cend(); ++it) {
            if (claimed.contains(it.key()))
                continue;
            if (mappings_.size() >= static_cast<qsizetype>(limits_.maximumAssets)) {
                return failure(fabgl::ErrorCode::CapacityExceeded,
                               QStringLiteral("automatic asset discovery exceeds the asset limit"));
            }
            AssetBrowserProjectEntry mapping;
            mapping.guid = fabgl::AssetGuid::generate();
            mapping.relativePath = it.value().relativePath;
            mapping.type = inferredType(mapping.relativePath, it.value().bytes);
            mapping.normalizedSettings = QStringLiteral("{}");
            mappings_.push_back(mapping);
            claimed.insert(it.key());
            ++lastStats_.discovered;
            emit owner_.assetDiscovered(QString::fromStdString(mapping.guid.toString()),
                                        mapping.relativePath, mapping.type);
        }
        return fabgl::Result<void>::success();
    }

    [[nodiscard]] fabgl::Result<void> performRefresh() {
        if (root_.isEmpty()) {
            return failure(fabgl::ErrorCode::InvalidState,
                           QStringLiteral("no asset browser project is active"));
        }
        if (refreshing_) {
            scheduleRefresh();
            return fabgl::Result<void>::success();
        }
        refreshing_ = true;
        struct RefreshGuard final {
            bool& value;
            ~RefreshGuard() {
                value = false;
            }
        } guard{refreshing_};
        debounceTimer_.stop();
        refreshPending_ = false;
        lastStats_ = {};

        auto scanned = scanSources();
        if (!scanned) {
            ++lastStats_.errors;
            reportDiagnostic({}, errorText(scanned.error()));
            return fabgl::Result<void>::failure(scanned.error());
        }
        auto reconciled = reconcileMappings(scanned.value());
        if (!reconciled) {
            ++lastStats_.errors;
            return reconciled;
        }

        fabgl::assets::AssetDatabase database;
        std::vector<fabgl::assets::AssetSourceState> sourceStates;
        sourceStates.reserve(static_cast<std::size_t>(scanned.value().byPath.size()));
        for (auto it = scanned.value().byPath.cbegin(); it != scanned.value().byPath.cend(); ++it) {
            sourceStates.push_back(
                {utf8String(it.value().relativePath), it.value().metadata.fingerprint});
        }
        QVector<AssetBrowserEntry> output;
        QMap<QString, AssetBrowserImporterDescriptor> descriptors;
        output.reserve(mappings_.size());
        QMap<QString, qsizetype> outputIndices;
        for (auto& mapping : mappings_) {
            auto settings = normalizedSettings(mapping.normalizedSettings);
            if (!settings)
                return fabgl::Result<void>::failure(settings.error());
            mapping.normalizedSettings = settings.value();
            mapping.relativePath = portableRelativePath(mapping.relativePath);
            mapping.type = mapping.type.trimmed().toLower();
            normalizeDependencies(mapping.dependencies);
            auto descriptor = assetBrowserImporterFor(mapping.relativePath, mapping.type);
            if (extensionImporterHooks_.probe) {
                auto probed = extensionImporterHooks_.probe(
                    mapping.relativePath, mapping.type, mapping.normalizedSettings);
                if (!probed)
                    return fabgl::Result<void>::failure(probed.error());
                if (probed.value()) {
                    descriptor = std::move(*probed.value());
                    if (!descriptor.supported || descriptor.id.empty() ||
                        descriptor.version == 0U || descriptor.extensionServiceId.empty()) {
                        return failure(
                            fabgl::ErrorCode::InvalidFormat,
                            QStringLiteral("extension importer probe returned an invalid descriptor"),
                            mapping.relativePath);
                    }
                    if (!descriptor.normalizedSettings.empty()) {
                        auto canonical = normalizedSettings(
                            QString::fromUtf8(descriptor.normalizedSettings));
                        if (!canonical)
                            return fabgl::Result<void>::failure(canonical.error());
                        mapping.normalizedSettings = canonical.value();
                        descriptor.normalizedSettings = utf8String(canonical.value());
                    } else {
                        descriptor.normalizedSettings = utf8String(mapping.normalizedSettings);
                    }
                }
            }
            const auto guidText = QString::fromStdString(mapping.guid.toString());
            descriptors.insert(guidText, descriptor);
            auto state = importStates_.value(guidText);
            fabgl::assets::AssetRecord record;
            record.guid = mapping.guid;
            record.relativePath = utf8String(mapping.relativePath);
            record.importer = descriptor.supported ? descriptor.id : std::string("unsupported");
            record.dependencies = std::vector<fabgl::AssetGuid>(mapping.dependencies.cbegin(),
                                                                mapping.dependencies.cend());
            record.importerVersion = std::max<std::uint32_t>(1U, descriptor.version);
            record.settingsFingerprint = fingerprint(mapping.normalizedSettings.toUtf8());
            record.importedSourceFingerprint = state.importedSourceFingerprint;
            record.importedSettingsFingerprint = state.settingsFingerprint;
            record.importedVersion = state.importerVersion;
            record.lastImportCacheKey = state.pcCacheKey;
            auto added = database.add(std::move(record));
            if (!added)
                return fabgl::Result<void>::failure(added.error());

            AssetBrowserEntry entry;
            entry.guid = mapping.guid;
            entry.relativePath = mapping.relativePath;
            entry.type = mapping.type;
            entry.importer = QString::fromStdString(descriptor.id);
            entry.normalizedSettings = mapping.normalizedSettings;
            entry.esp32Target = mapping.esp32Target;
            entry.dependencies = mapping.dependencies;
            entry.source = state.source;
            entry.state =
                descriptor.supported ? AssetBrowserState::Dirty : AssetBrowserState::Unsupported;
            outputIndices.insert(guidText, output.size());
            output.push_back(std::move(entry));
        }
        auto synchronized = database.synchronizeSources(sourceStates);
        if (!synchronized)
            return fabgl::Result<void>::failure(synchronized.error());

        std::vector<fabgl::AssetGuid> order;
        auto builtOrder = database.buildOrder();
        if (builtOrder) {
            order = std::move(builtOrder.value());
        } else {
            order.reserve(static_cast<std::size_t>(mappings_.size()));
            for (const auto& mapping : mappings_)
                order.push_back(mapping.guid);
            std::sort(order.begin(), order.end());
            reportDiagnostic({}, errorText(builtOrder.error()));
        }

        QMap<QString, std::uint64_t> pcKeys;
        QMap<QString, std::uint64_t> esp32Keys;
        for (const auto& guid : order) {
            const auto guidText = QString::fromStdString(guid.toString());
            const auto index = outputIndices.value(guidText, -1);
            if (index < 0)
                continue;
            auto& entry = output[index];
            const auto mappingPosition = mappingIndex(guid);
            if (mappingPosition < 0)
                continue;
            auto& mapping = mappings_[mappingPosition];
            const auto sourceIt = scanned.value().byPath.constFind(pathKey(mapping.relativePath));
            if (sourceIt == scanned.value().byPath.cend()) {
                entry.state = AssetBrowserState::Missing;
                entry.diagnostic = QStringLiteral("Source file is missing.");
                ++lastStats_.errors;
                reportDiagnostic(entry.relativePath, entry.diagnostic);
                continue;
            }
            const auto& source = sourceIt.value();
            entry.absolutePath = source.absolutePath;
            entry.source = source.metadata;
            const auto descriptor = descriptors.value(guidText);
            if (!descriptor.supported) {
                entry.state = AssetBrowserState::Unsupported;
                entry.diagnostic = QStringLiteral("No safe in-process importer is available.");
                reportDiagnostic(entry.relativePath, entry.diagnostic);
                continue;
            }

            QString unavailableDependency;
            for (const auto& dependency : mapping.dependencies) {
                const auto dependencyText = QString::fromStdString(dependency.toString());
                if (!pcKeys.contains(dependencyText) || !esp32Keys.contains(dependencyText)) {
                    unavailableDependency = dependencyText;
                    break;
                }
            }
            if (!unavailableDependency.isEmpty()) {
                entry.state = AssetBrowserState::Error;
                entry.diagnostic = QStringLiteral("Dependency %1 is missing or failed to import.")
                                       .arg(unavailableDependency);
                auto failedState = importStates_.value(guidText);
                failedState.source = source.metadata;
                failedState.importer = entry.importer;
                failedState.importerVersion = descriptor.version;
                importStates_.insert(guidText, failedState);
                ++lastStats_.errors;
                reportDiagnostic(entry.relativePath, entry.diagnostic);
                continue;
            }

            fabgl::assets::AssetImportRequest pcRequest;
            pcRequest.guid = guid;
            pcRequest.sourcePath = utf8String(source.absolutePath);
            pcRequest.relativePath = utf8String(mapping.relativePath);
            pcRequest.sourceBytes = source.bytes;
            pcRequest.normalizedSettings = utf8String(mapping.normalizedSettings);
            pcRequest.target = fabgl::assets::AssetTarget::Pc;
            pcRequest.pipelineVersion = 1U;
            for (const auto& dependency : mapping.dependencies) {
                pcRequest.dependencyCacheKeys.push_back(
                    pcKeys.value(QString::fromStdString(dependency.toString()), 0U));
            }
            auto esp32Request = pcRequest;
            esp32Request.target = mapping.esp32Target;
            esp32Request.dependencyCacheKeys.clear();
            for (const auto& dependency : mapping.dependencies) {
                esp32Request.dependencyCacheKeys.push_back(
                    esp32Keys.value(QString::fromStdString(dependency.toString()), 0U));
            }
            const auto pcKey =
                fabgl::assets::assetImportCacheKey(pcRequest, descriptor.id, descriptor.version);
            const auto esp32Key =
                fabgl::assets::assetImportCacheKey(esp32Request, descriptor.id, descriptor.version);
            const auto settingsKey = fingerprint(mapping.normalizedSettings.toUtf8());
            const auto state = importStates_.value(guidText);
            const auto pcPath = cachePath(guid, pcKey, QStringLiteral("-pc.bin"));
            const auto esp32Path = cachePath(guid, esp32Key, QStringLiteral("-esp32.bin"));
            const auto thumbnailPath = cachePath(guid, pcKey, QStringLiteral(".thumb.fgli"));
            const bool cacheValid =
                state.importedSourceFingerprint == source.metadata.fingerprint &&
                state.settingsFingerprint == settingsKey && state.importer == entry.importer &&
                state.importerVersion == descriptor.version && state.pcCacheKey == pcKey &&
                state.esp32CacheKey == esp32Key &&
                validCacheFile(pcPath, state.pcCost.payloadBytes) &&
                validCacheFile(esp32Path, state.esp32Cost.payloadBytes);
            if (cacheValid) {
                entry.state = AssetBrowserState::Clean;
                entry.pcCacheKey = pcKey;
                entry.esp32CacheKey = esp32Key;
                entry.pcCost = state.pcCost;
                entry.esp32Cost = state.esp32Cost;
                entry.thumbnail = readSafeCacheFile(thumbnailPath, 2U * 1024U * 1024U);
                entry.thumbnailPlaceholder = entry.thumbnail.isEmpty();
                ++lastStats_.cacheHits;
                pcKeys.insert(guidText, pcKey);
                esp32Keys.insert(guidText, esp32Key);
                auto updatedState = state;
                updatedState.source = source.metadata;
                importStates_.insert(guidText, updatedState);
                continue;
            }

            const auto runImporter = [this, &descriptor, &mapping](
                                         const fabgl::assets::AssetImportRequest& request) {
                if (descriptor.extensionServiceId.empty())
                    return importAssetForBrowser(descriptor, request, mapping.type);
                if (!extensionImporterHooks_.import) {
                    return fabgl::Result<fabgl::assets::ImportedAsset>::failure(
                        fabgl::Error(fabgl::ErrorCode::InvalidState,
                                     "selected extension importer is no longer available")
                            .addContext("service", descriptor.extensionServiceId));
                }
                return extensionImporterHooks_.import(descriptor, request, mapping.type);
            };
            auto importedPc = runImporter(pcRequest);
            auto importedEsp32 = runImporter(esp32Request);
            if (!importedPc || !importedEsp32) {
                entry.state = AssetBrowserState::Error;
                entry.diagnostic =
                    !importedPc ? errorText(importedPc.error()) : errorText(importedEsp32.error());
                auto failedState = state;
                failedState.source = source.metadata;
                failedState.importer = entry.importer;
                failedState.importerVersion = descriptor.version;
                importStates_.insert(guidText, failedState);
                ++lastStats_.errors;
                reportDiagnostic(entry.relativePath, entry.diagnostic);
                continue;
            }
            std::uint64_t verifyAggregate = 0U;
            auto verifiedSource = readSource(mapping.relativePath, verifyAggregate);
            if (!verifiedSource ||
                verifiedSource.value().metadata.fingerprint != source.metadata.fingerprint) {
                entry.state = AssetBrowserState::Error;
                entry.diagnostic = QStringLiteral("Source changed while the importer was running.");
                ++lastStats_.errors;
                reportDiagnostic(entry.relativePath, entry.diagnostic);
                scheduleRefresh();
                continue;
            }
            auto prepared = ensurePrivateDirectories(guid);
            if (!prepared)
                return prepared;
            auto wrote = atomicWrite(pcPath, byteArray(importedPc.value().payload));
            if (!wrote)
                return wrote;
            wrote = atomicWrite(esp32Path, byteArray(importedEsp32.value().payload));
            if (!wrote)
                return wrote;
            const auto thumbnail = byteArray(importedPc.value().thumbnail);
            if (!thumbnail.isEmpty()) {
                wrote = atomicWrite(thumbnailPath, thumbnail);
                if (!wrote)
                    return wrote;
            }

            QVector<fabgl::AssetGuid> dependencies = mapping.dependencies;
            for (const auto& dependency : importedPc.value().dependencies)
                dependencies.push_back(dependency);
            for (const auto& dependency : importedEsp32.value().dependencies)
                dependencies.push_back(dependency);
            normalizeDependencies(dependencies);
            mapping.dependencies = dependencies;
            entry.dependencies = dependencies;
            entry.state = AssetBrowserState::Clean;
            entry.pcCacheKey = pcKey;
            entry.esp32CacheKey = esp32Key;
            entry.pcCost = costFor(importedPc.value());
            entry.esp32Cost = costFor(importedEsp32.value());
            entry.thumbnail = thumbnail;
            entry.thumbnailPlaceholder = thumbnail.isEmpty();
            PersistedImportState importedState;
            importedState.source = source.metadata;
            importedState.importedSourceFingerprint = source.metadata.fingerprint;
            importedState.settingsFingerprint = settingsKey;
            importedState.importer = entry.importer;
            importedState.importerVersion = descriptor.version;
            importedState.pcCacheKey = pcKey;
            importedState.esp32CacheKey = esp32Key;
            importedState.pcCost = entry.pcCost;
            importedState.esp32Cost = entry.esp32Cost;
            importStates_.insert(guidText, importedState);
            auto marked = database.markImported(guid, source.metadata.fingerprint, pcKey);
            if (!marked) {
                entry.state = AssetBrowserState::Error;
                entry.diagnostic = errorText(marked.error());
                ++lastStats_.errors;
                reportDiagnostic(entry.relativePath, entry.diagnostic);
                continue;
            }
            ++lastStats_.imported;
            pcKeys.insert(guidText, pcKey);
            esp32Keys.insert(guidText, esp32Key);
        }

        QMap<QString, QVector<fabgl::AssetGuid>> dependents;
        for (const auto& entry : output) {
            for (const auto& dependency : entry.dependencies) {
                dependents[QString::fromStdString(dependency.toString())].push_back(entry.guid);
            }
        }
        std::uint64_t flash = 0U;
        std::uint64_t internalRam = 0U;
        std::uint64_t psram = 0U;
        std::uint64_t sd = 0U;
        for (auto& entry : output) {
            entry.dependents = dependents.value(QString::fromStdString(entry.guid.toString()));
            normalizeDependencies(entry.dependents);
            flash += entry.esp32Cost.flashBytes;
            internalRam += entry.esp32Cost.internalRamBytes;
            psram += entry.esp32Cost.psramBytes;
            sd += entry.esp32Cost.sdBytes;
        }
        const std::array budgets{
            std::tuple{QStringLiteral("Flash"), flash, limits_.flashBudgetBytes},
            std::tuple{QStringLiteral("Internal RAM"), internalRam, limits_.internalRamBudgetBytes},
            std::tuple{QStringLiteral("PSRAM"), psram, limits_.psramBudgetBytes},
            std::tuple{QStringLiteral("SD"), sd, limits_.sdBudgetBytes},
        };
        for (const auto& [name, used, budget] : budgets) {
            if (used > budget) {
                emit owner_.storageBudgetExceeded(name, static_cast<qulonglong>(used),
                                                  static_cast<qulonglong>(budget));
            }
        }
        auto persisted = persistMetadata();
        if (!persisted)
            return persisted;
        model_->replaceEntries(std::move(output));
        replaceWatches(scanned.value());
        ++refreshCount_;
        emit owner_.refreshed(
            static_cast<qulonglong>(refreshCount_), static_cast<int>(lastStats_.imported),
            static_cast<int>(lastStats_.cacheHits), static_cast<int>(lastStats_.errors));
        return fabgl::Result<void>::success();
    }
};

AssetBrowserController::AssetBrowserController(QObject* parent)
    : QObject(parent), implementation_(std::make_unique<Implementation>(*this)) {}

AssetBrowserController::~AssetBrowserController() = default;

fabgl::Result<void> AssetBrowserController::setProject(QString projectRoot,
                                                       QVector<AssetBrowserProjectEntry> entries,
                                                       AssetBrowserLimits limits) {
    clearProject();
    const QFileInfo rootInfo(projectRoot);
    const auto canonicalRoot = rootInfo.canonicalFilePath();
    if (!rootInfo.isDir() || canonicalRoot.isEmpty() ||
        isLinkLikePath(rootInfo.absoluteFilePath())) {
        return failure(fabgl::ErrorCode::InvalidArgument,
                       QStringLiteral("project root is unavailable, non-canonical, or link-like"),
                       projectRoot);
    }
    if (limits.maximumAssets == 0U || limits.maximumDirectories == 0U ||
        limits.maximumSourceBytes == 0U || limits.maximumAggregateSourceBytes == 0U ||
        limits.maximumAggregateSourceBytes < limits.maximumSourceBytes ||
        limits.debounceMilliseconds < 0 || limits.maximumDebounceMilliseconds < 0) {
        return failure(fabgl::ErrorCode::InvalidArgument,
                       QStringLiteral("asset browser limits are invalid"));
    }
    implementation_->root_ = canonicalRoot;
    implementation_->limits_ = limits;
    auto loaded = implementation_->loadMetadata();
    if (!loaded) {
        clearProject();
        return loaded;
    }
    QMap<QString, qsizetype> persistedByGuid;
    for (qsizetype index = 0; index < implementation_->mappings_.size(); ++index) {
        persistedByGuid.insert(
            QString::fromStdString(implementation_->mappings_.at(index).guid.toString()), index);
    }
    for (auto& entry : entries) {
        if (entry.guid.isNil() || !isSafeRelativeAssetPath(entry.relativePath)) {
            clearProject();
            return failure(fabgl::ErrorCode::InvalidArgument,
                           QStringLiteral("project asset mapping contains an invalid GUID or path"),
                           entry.relativePath);
        }
        entry.relativePath = portableRelativePath(entry.relativePath);
        entry.type = entry.type.trimmed().toLower();
        auto settings = normalizedSettings(entry.normalizedSettings);
        if (!settings) {
            clearProject();
            return fabgl::Result<void>::failure(settings.error());
        }
        entry.normalizedSettings = settings.value();
        normalizeDependencies(entry.dependencies);
        const auto guidText = QString::fromStdString(entry.guid.toString());
        const auto existing = persistedByGuid.constFind(guidText);
        if (existing == persistedByGuid.cend()) {
            implementation_->mappings_.push_back(entry);
            persistedByGuid.insert(guidText, implementation_->mappings_.size() - 1);
            continue;
        }
        auto& persisted = implementation_->mappings_[existing.value()];
        const bool requestedExists =
            QFileInfo(QDir(canonicalRoot).filePath(entry.relativePath)).exists();
        const bool persistedExists =
            QFileInfo(QDir(canonicalRoot).filePath(persisted.relativePath)).exists();
        if (requestedExists || !persistedExists)
            persisted.relativePath = entry.relativePath;
        if (!entry.type.isEmpty())
            persisted.type = entry.type;
        if (entry.hasExplicitImportMetadata || persisted.normalizedSettings.isEmpty())
            persisted.normalizedSettings = entry.normalizedSettings;
        if (entry.hasExplicitImportMetadata)
            persisted.dependencies = entry.dependencies;
        if (entry.hasExplicitImportMetadata)
            persisted.esp32Target = entry.esp32Target;
    }
    if (implementation_->mappings_.size() > static_cast<qsizetype>(limits.maximumAssets)) {
        clearProject();
        return failure(fabgl::ErrorCode::CapacityExceeded,
                       QStringLiteral("project contains too many asset mappings"));
    }
    QSet<QString> paths;
    QSet<QString> guids;
    for (const auto& mapping : implementation_->mappings_) {
        const auto guid = QString::fromStdString(mapping.guid.toString());
        const auto path = pathKey(mapping.relativePath);
        if (mapping.guid.isNil() || !isSafeRelativeAssetPath(mapping.relativePath) ||
            guids.contains(guid) || paths.contains(path)) {
            clearProject();
            return failure(fabgl::ErrorCode::AlreadyExists,
                           QStringLiteral("project contains duplicate or unsafe asset mappings"),
                           mapping.relativePath);
        }
        guids.insert(guid);
        paths.insert(path);
    }
    auto refreshed = implementation_->performRefresh();
    if (!refreshed) {
        clearProject();
        return refreshed;
    }
    return fabgl::Result<void>::success();
}

void AssetBrowserController::clearProject() {
    implementation_->debounceTimer_.stop();
    const auto files = implementation_->watcher_.files();
    const auto directories = implementation_->watcher_.directories();
    if (!files.isEmpty())
        implementation_->watcher_.removePaths(files);
    if (!directories.isEmpty())
        implementation_->watcher_.removePaths(directories);
    implementation_->root_.clear();
    implementation_->mappings_.clear();
    implementation_->importStates_.clear();
    implementation_->lastStats_ = {};
    implementation_->refreshCount_ = 0U;
    implementation_->refreshPending_ = false;
    implementation_->refreshing_ = false;
    implementation_->model_->replaceEntries({});
}

fabgl::Result<void> AssetBrowserController::refreshNow() {
    return implementation_->performRefresh();
}

void AssetBrowserController::requestRefresh() {
    implementation_->scheduleRefresh();
}

void AssetBrowserController::setExtensionImporterHooks(AssetBrowserExtensionImporterHooks hooks) {
    implementation_->extensionImporterHooks_ = std::move(hooks);
    if (!implementation_->root_.isEmpty())
        implementation_->scheduleRefresh();
}

void AssetBrowserController::clearExtensionImporterHooks() {
    implementation_->extensionImporterHooks_ = {};
    if (!implementation_->root_.isEmpty())
        implementation_->scheduleRefresh();
}

fabgl::Result<void>
AssetBrowserController::setImportSettings(const fabgl::AssetGuid& guid, QString jsonSettings,
                                          const fabgl::assets::AssetTarget esp32Target) {
    const auto index = implementation_->mappingIndex(guid);
    if (index < 0)
        return failure(fabgl::ErrorCode::NotFound, QStringLiteral("asset GUID was not found"));
    if (esp32Target == fabgl::assets::AssetTarget::Pc) {
        return failure(fabgl::ErrorCode::InvalidArgument,
                       QStringLiteral("ESP32 storage target cannot be PC"));
    }
    auto normalized = normalizedSettings(std::move(jsonSettings));
    if (!normalized)
        return fabgl::Result<void>::failure(normalized.error());
    const auto& mapping = implementation_->mappings_[index];
    const auto descriptor = assetBrowserImporterFor(mapping.relativePath, mapping.type);
    if (descriptor.id == "fabgl.image.source") {
        auto imageSettings =
            fabgl::project::decodeProjectImageImportSettings(utf8String(normalized.value()));
        if (!imageSettings)
            return fabgl::Result<void>::failure(imageSettings.error());
        const bool atlas =
            imageSettings.value().outputKind == fabgl::assets::ImageOutputKind::SpriteAtlas;
        const auto type = mapping.type.trimmed().toLower();
        if (type != QStringLiteral("image") && type != QStringLiteral("sprite.atlas")) {
            return failure(fabgl::ErrorCode::TypeMismatch,
                           QStringLiteral("image source has an incompatible asset type"),
                           mapping.relativePath);
        }
        implementation_->mappings_[index].type =
            atlas ? QStringLiteral("sprite.atlas") : QStringLiteral("image");
    }
    implementation_->mappings_[index].normalizedSettings = normalized.value();
    implementation_->mappings_[index].esp32Target = esp32Target;
    auto persisted = implementation_->persistMetadata();
    if (!persisted)
        return persisted;
    implementation_->scheduleRefresh();
    return fabgl::Result<void>::success();
}

fabgl::Result<void>
AssetBrowserController::setDependencies(const fabgl::AssetGuid& guid,
                                        QVector<fabgl::AssetGuid> dependencies) {
    const auto index = implementation_->mappingIndex(guid);
    if (index < 0)
        return failure(fabgl::ErrorCode::NotFound, QStringLiteral("asset GUID was not found"));
    normalizeDependencies(dependencies);
    if (dependencies.contains(guid)) {
        return failure(fabgl::ErrorCode::CycleDetected,
                       QStringLiteral("asset cannot depend on itself"));
    }
    for (const auto& dependency : dependencies) {
        if (implementation_->mappingIndex(dependency) < 0) {
            return failure(fabgl::ErrorCode::NotFound,
                           QStringLiteral("asset dependency GUID was not found"));
        }
    }
    implementation_->mappings_[index].dependencies = std::move(dependencies);
    auto persisted = implementation_->persistMetadata();
    if (!persisted)
        return persisted;
    implementation_->scheduleRefresh();
    return fabgl::Result<void>::success();
}

fabgl::Result<void> AssetBrowserController::relocateAsset(const fabgl::AssetGuid& guid,
                                                          QString newRelativePath) {
    const auto index = implementation_->mappingIndex(guid);
    if (index < 0)
        return failure(fabgl::ErrorCode::NotFound, QStringLiteral("asset GUID was not found"));
    newRelativePath = portableRelativePath(std::move(newRelativePath));
    if (!isSafeRelativeAssetPath(newRelativePath)) {
        return failure(fabgl::ErrorCode::InvalidArgument,
                       QStringLiteral("asset relocation path is unsafe"), newRelativePath);
    }
    for (const auto& mapping : implementation_->mappings_) {
        if (mapping.guid != guid && pathKey(mapping.relativePath) == pathKey(newRelativePath)) {
            return failure(fabgl::ErrorCode::AlreadyExists,
                           QStringLiteral("asset relocation collides with another GUID mapping"),
                           newRelativePath);
        }
    }
    auto& mapping = implementation_->mappings_[index];
    const auto oldRelativePath = mapping.relativePath;
    const auto source = QDir(implementation_->root_).filePath(oldRelativePath);
    const auto destination = QDir(implementation_->root_).filePath(newRelativePath);
    if (!isSafeExistingPath(implementation_->root_, source, false)) {
        return failure(fabgl::ErrorCode::InvalidArgument,
                       QStringLiteral("asset relocation source is missing or link-like"),
                       oldRelativePath);
    }
    auto writable = validateWritablePath(implementation_->root_, destination, false);
    if (!writable)
        return fabgl::Result<void>::failure(writable.error());
    if (!QFile::rename(source, writable.value())) {
        return failure(fabgl::ErrorCode::IoError, QStringLiteral("asset relocation failed"),
                       newRelativePath);
    }
    mapping.relativePath = newRelativePath;
    auto persisted = implementation_->persistMetadata();
    if (!persisted) {
        (void)QFile::rename(writable.value(), source);
        mapping.relativePath = oldRelativePath;
        return persisted;
    }
    emit assetMappingMoved(QString::fromStdString(guid.toString()), oldRelativePath,
                           newRelativePath);
    implementation_->scheduleRefresh();
    return fabgl::Result<void>::success();
}

AssetBrowserModel* AssetBrowserController::model() noexcept {
    return implementation_->model_.get();
}

const AssetBrowserModel* AssetBrowserController::model() const noexcept {
    return implementation_->model_.get();
}

QString AssetBrowserController::projectRoot() const {
    return implementation_->root_;
}

QString AssetBrowserController::metadataPath() const {
    return implementation_->metadataPath();
}

QString AssetBrowserController::cachedPayloadPath(const fabgl::AssetGuid& guid,
                                                  const fabgl::assets::AssetTarget target) const {
    const auto* entry = implementation_->model_->entry(guid);
    if (entry == nullptr)
        return {};
    if (target == fabgl::assets::AssetTarget::Pc) {
        const auto path =
            implementation_->cachePath(guid, entry->pcCacheKey, QStringLiteral("-pc.bin"));
        return implementation_->validCacheFile(path, entry->pcCost.payloadBytes) ? path : QString{};
    }
    if (target != entry->esp32Target)
        return {};
    const auto path =
        implementation_->cachePath(guid, entry->esp32CacheKey, QStringLiteral("-esp32.bin"));
    return implementation_->validCacheFile(path, entry->esp32Cost.payloadBytes) ? path : QString{};
}

AssetBrowserRefreshStats AssetBrowserController::lastRefreshStats() const noexcept {
    return implementation_->lastStats_;
}

std::uint64_t AssetBrowserController::refreshCount() const noexcept {
    return implementation_->refreshCount_;
}

bool AssetBrowserController::refreshPending() const noexcept {
    return implementation_->refreshPending_ || implementation_->debounceTimer_.isActive();
}

} // namespace fgl::studio
