#include "ProjectDocument.h"

#include <fabgl/core/guid.h>
#include <fabgl/packages/package_manifest.h>

#include <project_format.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fgl::studio {
namespace {

constexpr auto ProjectKind = "FabGLStudioProject";
constexpr qsizetype MaximumProjectBytes = 1024 * 1024;
constexpr qsizetype MaximumBuildArguments = 64;
constexpr qsizetype MaximumAssets = 4096;
constexpr qsizetype MaximumInputContexts = 32;
constexpr qsizetype MaximumInputValuesPerContext = 128;
constexpr qsizetype MaximumBindingsPerValue = 16;
constexpr qsizetype MaximumTotalBindings = 2048;
constexpr qsizetype MaximumPackageDependencies = 256;

bool containsUnsafePathSyntax(const QString& path) {
    if (path.isEmpty() || path.contains(QLatin1Char(':')) || path.contains(QChar::Null)) {
        return true;
    }
    QString portablePath = path;
    portablePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (QDir::isAbsolutePath(portablePath) || portablePath.startsWith(QStringLiteral("//"))) {
        return true;
    }
    return portablePath.split(QLatin1Char('/')).contains(QStringLiteral(".."));
}

bool isCanonicalProjectRoot(const QString& path) {
    return path == QStringLiteral(".") && !containsUnsafePathSyntax(path);
}

bool isSafeScenePath(const QString& path) {
    if (path.toUtf8().size() > 240 || containsUnsafePathSyntax(path)) {
        return false;
    }
    const auto clean = QDir::cleanPath(path);
    return clean != QStringLiteral(".") && clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

QString canonicalAssetPath(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.contains(QStringLiteral("//"))) {
        path.replace(QStringLiteral("//"), QStringLiteral("/"));
    }
    return path;
}

bool isSafeAssetPath(QString path) {
    if (path.toUtf8().size() > 512 || path.isEmpty() || path.startsWith(QLatin1Char('/')) ||
        path.startsWith(QLatin1Char('\\')) || path.contains(QChar::Null)) {
        return false;
    }
    for (const auto character : path) {
        if (character.unicode() < 0x20U || QStringLiteral(":\"|?*").contains(character)) {
            return false;
        }
    }
    path = canonicalAssetPath(path);
    const auto segments = path.split(QLatin1Char('/'));
    for (const auto& segment : segments) {
        if (segment.isEmpty() || segment == QStringLiteral(".") ||
            segment == QStringLiteral("..") || segment.endsWith(QLatin1Char('.')) ||
            segment.endsWith(QLatin1Char(' '))) {
            return false;
        }
        const QString base = segment.section(QLatin1Char('.'), 0, 0).toUpper();
        if (QStringList{QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                        QStringLiteral("NUL"), QStringLiteral("CLOCK$")}
                .contains(base) ||
            (base.size() == 4 &&
             (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT"))) &&
             base.at(3) >= QLatin1Char('1') && base.at(3) <= QLatin1Char('9'))) {
            return false;
        }
    }
    return true;
}

bool validateCanonicalImporterSettings(const ProjectAssetEntry& asset, QString& errorMessage) {
    const auto suffix = QFileInfo(asset.path).suffix().trimmed().toLower();
    if (QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                    QStringLiteral("bmp")}
            .contains(suffix)) {
        auto settings = fabgl::project::decodeProjectImageImportSettings(
            asset.importSettings.toUtf8().toStdString());
        if (!settings) {
            errorMessage = QObject::tr("Image import settings are invalid: %1")
                               .arg(QString::fromStdString(settings.error().message()));
            return false;
        }
        const auto expectedType =
            settings.value().outputKind == fabgl::assets::ImageOutputKind::SpriteAtlas
                ? QStringLiteral("sprite.atlas")
                : QStringLiteral("image");
        if (asset.type.trimmed().toLower() != expectedType) {
            errorMessage = QObject::tr("Image asset type must be %1 for its selected output.")
                               .arg(expectedType);
            return false;
        }
    } else if (suffix == QStringLiteral("wav")) {
        auto settings = fabgl::project::decodeProjectAudioImportSettings(
            asset.importSettings.toUtf8().toStdString());
        if (!settings) {
            errorMessage = QObject::tr("Audio import settings are invalid: %1")
                               .arg(QString::fromStdString(settings.error().message()));
            return false;
        }
    }
    return true;
}

bool isValidText(const QString& value, const qsizetype maximumBytes,
                 const bool allowEmpty = false) {
    const QByteArray bytes = value.toUtf8();
    if ((!allowEmpty && bytes.isEmpty()) || bytes.size() > maximumBytes) {
        return false;
    }
    return std::none_of(bytes.cbegin(), bytes.cend(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7FU;
    });
}

bool isValidStableId(const QString& value) {
    const QByteArray bytes = value.toLatin1();
    if (bytes.isEmpty() || bytes.size() > 80) {
        return false;
    }
    const auto first = static_cast<unsigned char>(bytes.front());
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) {
        return false;
    }
    return std::all_of(bytes.cbegin(), bytes.cend(), [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '.' || character == '_' || character == '-';
    });
}

bool isValidGuid(const QString& value) {
    const QByteArray bytes = value.toLatin1();
    return fabgl::AssetGuid::parse(
               std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))
        .hasValue();
}

bool isValidVersionRequirement(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return fabgl::VersionRequirement::parse(
               std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))
        .hasValue();
}

QString canonicalVersionRequirement(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    const auto parsed = fabgl::VersionRequirement::parse(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    return parsed ? QString::fromStdString(parsed.value().toString()) : QString{};
}

bool rejectUnknownFields(const QJsonObject& object, const QSet<QString>& allowed,
                         const QString& objectName, QString& errorMessage) {
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            errorMessage = QObject::tr("Unknown %1 field '%2'.").arg(objectName, iterator.key());
            return false;
        }
    }
    return true;
}

bool requireField(const QJsonObject& object, const QString& field, const QJsonValue::Type type,
                  const QString& objectName, QString& errorMessage) {
    const auto value = object.value(field);
    if (value.isUndefined()) {
        errorMessage = QObject::tr("%1 is missing required field '%2'.").arg(objectName, field);
        return false;
    }
    if (value.type() != type) {
        errorMessage = QObject::tr("%1.%2 has the wrong JSON type.").arg(objectName, field);
        return false;
    }
    return true;
}

bool decodeBuild(const QJsonValue& value, ProjectData& decoded, const bool requireFields,
                 QString& errorMessage) {
    if (!value.isObject()) {
        errorMessage = QObject::tr("build must be an object.");
        return false;
    }
    const auto build = value.toObject();
    if (!rejectUnknownFields(build, {QStringLiteral("program"), QStringLiteral("arguments")},
                             QStringLiteral("build"), errorMessage)) {
        return false;
    }
    if (requireFields && (!requireField(build, QStringLiteral("program"), QJsonValue::String,
                                        QStringLiteral("build"), errorMessage) ||
                          !requireField(build, QStringLiteral("arguments"), QJsonValue::Array,
                                        QStringLiteral("build"), errorMessage))) {
        return false;
    }
    if (const auto program = build.value(QStringLiteral("program")); !program.isUndefined()) {
        if (!program.isString()) {
            errorMessage = QObject::tr("build.program must be a string.");
            return false;
        }
        decoded.buildProgram = program.toString();
    }
    if (const auto arguments = build.value(QStringLiteral("arguments")); !arguments.isUndefined()) {
        if (!arguments.isArray() || arguments.toArray().size() > MaximumBuildArguments) {
            errorMessage = QObject::tr("build.arguments must be a bounded array.");
            return false;
        }
        decoded.buildArguments.clear();
        for (const auto& argument : arguments.toArray()) {
            if (!argument.isString() || !isValidText(argument.toString(), 4096, true)) {
                errorMessage = QObject::tr("Every build argument must be a valid string.");
                return false;
            }
            decoded.buildArguments.push_back(argument.toString());
        }
    }
    if (!isValidText(decoded.buildProgram, 1024)) {
        errorMessage = QObject::tr("build.program is invalid.");
        return false;
    }
    return true;
}

bool decodeInputValues(const QJsonValue& value, QVector<ProjectInputValue>& output,
                       qsizetype& totalBindings, const QString& category, QString& errorMessage) {
    if (!value.isArray() || value.toArray().size() > MaximumInputValuesPerContext) {
        errorMessage = QObject::tr("%1 must be a bounded array.").arg(category);
        return false;
    }
    QSet<QString> names;
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) {
            errorMessage = QObject::tr("Every %1 entry must be an object.").arg(category);
            return false;
        }
        const auto object = entry.toObject();
        if (!rejectUnknownFields(object, {QStringLiteral("name"), QStringLiteral("bindings")},
                                 category, errorMessage) ||
            !requireField(object, QStringLiteral("name"), QJsonValue::String, category,
                          errorMessage) ||
            !requireField(object, QStringLiteral("bindings"), QJsonValue::Array, category,
                          errorMessage)) {
            return false;
        }
        ProjectInputValue decoded;
        decoded.name = object.value(QStringLiteral("name")).toString();
        const auto bindings = object.value(QStringLiteral("bindings")).toArray();
        if (!isValidText(decoded.name, 80) || names.contains(decoded.name) || bindings.isEmpty() ||
            bindings.size() > MaximumBindingsPerValue ||
            totalBindings > MaximumTotalBindings - bindings.size()) {
            errorMessage = QObject::tr("%1 name or binding count is invalid.").arg(category);
            return false;
        }
        names.insert(decoded.name);
        totalBindings += bindings.size();
        QSet<QString> controls;
        for (const auto& bindingValue : bindings) {
            if (!bindingValue.isObject()) {
                errorMessage = QObject::tr("Every input binding must be an object.");
                return false;
            }
            const auto bindingObject = bindingValue.toObject();
            if (!rejectUnknownFields(bindingObject,
                                     {QStringLiteral("control"), QStringLiteral("scale"),
                                      QStringLiteral("threshold")},
                                     QStringLiteral("input binding"), errorMessage) ||
                !requireField(bindingObject, QStringLiteral("control"), QJsonValue::String,
                              QStringLiteral("input binding"), errorMessage) ||
                !requireField(bindingObject, QStringLiteral("scale"), QJsonValue::Double,
                              QStringLiteral("input binding"), errorMessage) ||
                !requireField(bindingObject, QStringLiteral("threshold"), QJsonValue::Double,
                              QStringLiteral("input binding"), errorMessage)) {
                return false;
            }
            ProjectInputBinding binding;
            binding.control = bindingObject.value(QStringLiteral("control")).toString();
            binding.scale = bindingObject.value(QStringLiteral("scale")).toDouble();
            binding.threshold = bindingObject.value(QStringLiteral("threshold")).toDouble();
            if (!isValidText(binding.control, 128) || controls.contains(binding.control) ||
                !std::isfinite(binding.scale) || std::abs(binding.scale) > 16.0 ||
                !std::isfinite(binding.threshold) || binding.threshold < 0.0 ||
                binding.threshold > 1.0) {
                errorMessage = QObject::tr("Input binding '%1' is invalid.").arg(binding.control);
                return false;
            }
            controls.insert(binding.control);
            decoded.bindings.push_back(std::move(binding));
        }
        output.push_back(std::move(decoded));
    }
    return true;
}

bool decodeInput(const QJsonValue& value, ProjectData& decoded, QString& errorMessage) {
    if (!value.isObject()) {
        errorMessage = QObject::tr("input must be an object.");
        return false;
    }
    const auto input = value.toObject();
    if (!rejectUnknownFields(input, {QStringLiteral("contexts")}, QStringLiteral("input"),
                             errorMessage) ||
        !requireField(input, QStringLiteral("contexts"), QJsonValue::Array, QStringLiteral("input"),
                      errorMessage)) {
        return false;
    }
    const auto contexts = input.value(QStringLiteral("contexts")).toArray();
    if (contexts.size() > MaximumInputContexts) {
        errorMessage = QObject::tr("input.contexts exceeds the context-count limit.");
        return false;
    }
    QSet<QString> names;
    qsizetype totalBindings = 0;
    for (const auto& contextValue : contexts) {
        if (!contextValue.isObject()) {
            errorMessage = QObject::tr("Every input context must be an object.");
            return false;
        }
        const auto contextObject = contextValue.toObject();
        const QSet<QString> contextFields = {QStringLiteral("name"), QStringLiteral("priority"),
                                             QStringLiteral("enabled"), QStringLiteral("actions"),
                                             QStringLiteral("axes")};
        if (!rejectUnknownFields(contextObject, contextFields, QStringLiteral("input context"),
                                 errorMessage) ||
            !requireField(contextObject, QStringLiteral("name"), QJsonValue::String,
                          QStringLiteral("input context"), errorMessage) ||
            !requireField(contextObject, QStringLiteral("priority"), QJsonValue::Double,
                          QStringLiteral("input context"), errorMessage) ||
            !requireField(contextObject, QStringLiteral("enabled"), QJsonValue::Bool,
                          QStringLiteral("input context"), errorMessage) ||
            !requireField(contextObject, QStringLiteral("actions"), QJsonValue::Array,
                          QStringLiteral("input context"), errorMessage) ||
            !requireField(contextObject, QStringLiteral("axes"), QJsonValue::Array,
                          QStringLiteral("input context"), errorMessage)) {
            return false;
        }
        const double priority = contextObject.value(QStringLiteral("priority")).toDouble();
        ProjectInputContext context;
        context.name = contextObject.value(QStringLiteral("name")).toString();
        context.enabled = contextObject.value(QStringLiteral("enabled")).toBool();
        if (!isValidText(context.name, 80) || names.contains(context.name) ||
            !std::isfinite(priority) || std::trunc(priority) != priority || priority < -1000000.0 ||
            priority > 1000000.0) {
            errorMessage = QObject::tr("Input context name or priority is invalid.");
            return false;
        }
        context.priority = static_cast<int>(priority);
        names.insert(context.name);
        if (!decodeInputValues(contextObject.value(QStringLiteral("actions")), context.actions,
                               totalBindings, QStringLiteral("input actions"), errorMessage) ||
            !decodeInputValues(contextObject.value(QStringLiteral("axes")), context.axes,
                               totalBindings, QStringLiteral("input axes"), errorMessage)) {
            return false;
        }
        decoded.inputContexts.push_back(std::move(context));
    }
    return true;
}

bool decodePackages(const QJsonValue& value, ProjectData& decoded, QString& errorMessage) {
    if (!value.isArray() || value.toArray().size() > MaximumPackageDependencies) {
        errorMessage = QObject::tr("packages must be a bounded array.");
        return false;
    }
    QSet<QString> ids;
    for (const auto& packageValue : value.toArray()) {
        if (!packageValue.isObject()) {
            errorMessage = QObject::tr("Every package dependency must be an object.");
            return false;
        }
        const auto packageObject = packageValue.toObject();
        if (!rejectUnknownFields(packageObject, {QStringLiteral("id"), QStringLiteral("version")},
                                 QStringLiteral("package dependency"), errorMessage) ||
            !requireField(packageObject, QStringLiteral("id"), QJsonValue::String,
                          QStringLiteral("package dependency"), errorMessage) ||
            !requireField(packageObject, QStringLiteral("version"), QJsonValue::String,
                          QStringLiteral("package dependency"), errorMessage)) {
            return false;
        }
        ProjectPackageDependency dependency;
        dependency.id = packageObject.value(QStringLiteral("id")).toString();
        dependency.version = packageObject.value(QStringLiteral("version")).toString();
        if (!isValidStableId(dependency.id) || ids.contains(dependency.id) ||
            !isValidVersionRequirement(dependency.version)) {
            errorMessage =
                QObject::tr("Package dependency '%1' is invalid or duplicated.").arg(dependency.id);
            return false;
        }
        dependency.version = canonicalVersionRequirement(dependency.version);
        ids.insert(dependency.id);
        decoded.packageDependencies.push_back(std::move(dependency));
    }
    return true;
}

bool decodeAssets(const QJsonValue& value, ProjectData& decoded, QString& errorMessage) {
    if (!value.isArray() || value.toArray().size() > MaximumAssets) {
        errorMessage = QObject::tr("assets must be a bounded array.");
        return false;
    }
    QSet<QString> guids;
    QSet<QString> portablePaths;
    for (const auto& assetValue : value.toArray()) {
        if (!assetValue.isObject()) {
            errorMessage = QObject::tr("Every asset entry must be an object.");
            return false;
        }
        const auto object = assetValue.toObject();
        if (!rejectUnknownFields(object,
                                 {QStringLiteral("guid"), QStringLiteral("path"),
                                  QStringLiteral("type"), QStringLiteral("import")},
                                 QStringLiteral("asset entry"), errorMessage) ||
            !requireField(object, QStringLiteral("guid"), QJsonValue::String,
                          QStringLiteral("asset entry"), errorMessage) ||
            !requireField(object, QStringLiteral("path"), QJsonValue::String,
                          QStringLiteral("asset entry"), errorMessage) ||
            !requireField(object, QStringLiteral("type"), QJsonValue::String,
                          QStringLiteral("asset entry"), errorMessage)) {
            return false;
        }
        ProjectAssetEntry asset;
        asset.guid = object.value(QStringLiteral("guid")).toString().toLower();
        asset.path = canonicalAssetPath(object.value(QStringLiteral("path")).toString());
        asset.type = object.value(QStringLiteral("type")).toString();
        if (object.contains(QStringLiteral("import"))) {
            if (!object.value(QStringLiteral("import")).isObject()) {
                errorMessage = QObject::tr("Asset import metadata must be an object.");
                return false;
            }
            const auto import = object.value(QStringLiteral("import")).toObject();
            if (!rejectUnknownFields(import,
                                     {QStringLiteral("settings"), QStringLiteral("esp32Target"),
                                      QStringLiteral("dependencies")},
                                     QStringLiteral("asset import metadata"), errorMessage)) {
                return false;
            }
            const auto settings = import.value(QStringLiteral("settings"));
            if (!settings.isUndefined()) {
                if (!settings.isObject()) {
                    errorMessage = QObject::tr("Asset import settings must be an object.");
                    return false;
                }
                asset.importSettings = QString::fromUtf8(
                    QJsonDocument(settings.toObject()).toJson(QJsonDocument::Compact));
            }
            if (import.contains(QStringLiteral("esp32Target"))) {
                if (!import.value(QStringLiteral("esp32Target")).isString()) {
                    errorMessage = QObject::tr("Asset ESP32 target must be a string.");
                    return false;
                }
                asset.esp32Target =
                    import.value(QStringLiteral("esp32Target")).toString().toLower();
            }
            const auto dependencies = import.value(QStringLiteral("dependencies"));
            if (!dependencies.isUndefined()) {
                if (!dependencies.isArray() || dependencies.toArray().size() > MaximumAssets) {
                    errorMessage = QObject::tr("Asset dependency list is invalid.");
                    return false;
                }
                for (const auto& dependency : dependencies.toArray()) {
                    if (!dependency.isString()) {
                        errorMessage = QObject::tr("Asset dependency GUID must be a string.");
                        return false;
                    }
                    asset.dependencies.push_back(dependency.toString().toLower());
                }
                asset.dependencies.removeDuplicates();
                std::sort(asset.dependencies.begin(), asset.dependencies.end());
            }
            asset.hasImportMetadata = true;
        }
        const auto parsedGuid = fabgl::AssetGuid::parse(asset.guid.toStdString());
        const QString pathKey = asset.path.toCaseFolded();
        if (!parsedGuid || parsedGuid.value().isNil() || guids.contains(asset.guid) ||
            !isSafeAssetPath(asset.path) || portablePaths.contains(pathKey) ||
            !isValidStableId(asset.type)) {
            errorMessage = QObject::tr("Asset GUID, path, or type is invalid or duplicated.");
            return false;
        }
        if (!validateCanonicalImporterSettings(asset, errorMessage))
            return false;
        guids.insert(asset.guid);
        portablePaths.insert(pathKey);
        decoded.assets.push_back(std::move(asset));
    }
    return true;
}

bool decodeTargetProfiles(const QJsonValue& value, ProjectData& decoded, QString& errorMessage) {
    if (!value.isObject()) {
        errorMessage = QObject::tr("targetProfiles must be an object.");
        return false;
    }
    const auto targets = value.toObject();
    if (!rejectUnknownFields(targets, {QStringLiteral("pc"), QStringLiteral("esp32")},
                             QStringLiteral("targetProfiles"), errorMessage) ||
        !requireField(targets, QStringLiteral("pc"), QJsonValue::String,
                      QStringLiteral("targetProfiles"), errorMessage) ||
        !requireField(targets, QStringLiteral("esp32"), QJsonValue::String,
                      QStringLiteral("targetProfiles"), errorMessage)) {
        return false;
    }
    decoded.targetProfiles.pc = targets.value(QStringLiteral("pc")).toString();
    decoded.targetProfiles.esp32 = targets.value(QStringLiteral("esp32")).toString();
    if (!isValidStableId(decoded.targetProfiles.pc) ||
        !isValidStableId(decoded.targetProfiles.esp32)) {
        errorMessage = QObject::tr("A selected target profile identifier is invalid.");
        return false;
    }
    return true;
}

bool decodePerformanceValues(const QJsonValue& jsonValue,
                             fabgl::project::PerformanceBudgetValues& budget,
                             const QString& category, QString& errorMessage) {
    if (!jsonValue.isObject()) {
        errorMessage = QObject::tr("%1 must be an object.").arg(category);
        return false;
    }
    const auto object = jsonValue.toObject();
    const QSet<QString> fields = {QStringLiteral("frameTotalMs"),
                                  QStringLiteral("fixedUpdateMs"),
                                  QStringLiteral("updateMs"),
                                  QStringLiteral("physicsMs"),
                                  QStringLiteral("animationMs"),
                                  QStringLiteral("aiMs"),
                                  QStringLiteral("renderMs"),
                                  QStringLiteral("audioMs"),
                                  QStringLiteral("assetStreamingMs"),
                                  QStringLiteral("entities"),
                                  QStringLiteral("components"),
                                  QStringLiteral("drawCalls"),
                                  QStringLiteral("sprites"),
                                  QStringLiteral("triangles"),
                                  QStringLiteral("rays"),
                                  QStringLiteral("particles"),
                                  QStringLiteral("audioVoices"),
                                  QStringLiteral("assetResidentBytes"),
                                  QStringLiteral("internalRamBytes"),
                                  QStringLiteral("psramBytes"),
                                  QStringLiteral("flashBytes"),
                                  QStringLiteral("sdBytes")};
    if (!rejectUnknownFields(object, fields, category, errorMessage)) {
        return false;
    }
    const auto readTime = [&](const QString& name,
                              double fabgl::project::PerformanceBudgetValues::*member) {
        if (!object.contains(name))
            return true;
        const auto value = object.value(name);
        const double number = value.toDouble(-1.0);
        if (!value.isDouble() || !std::isfinite(number) || number <= 0.0 || number > 10000.0) {
            errorMessage = QObject::tr("%1.%2 is not a valid time budget.").arg(category, name);
            return false;
        }
        budget.*member = number;
        return true;
    };
    const auto readCount = [&](const QString& name,
                               std::uint32_t fabgl::project::PerformanceBudgetValues::*member) {
        if (!object.contains(name))
            return true;
        const auto value = object.value(name);
        const double number = value.toDouble(-1.0);
        if (!value.isDouble() || !std::isfinite(number) || std::trunc(number) != number ||
            number <= 0.0 ||
            number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            errorMessage = QObject::tr("%1.%2 is not a valid count budget.").arg(category, name);
            return false;
        }
        budget.*member = static_cast<std::uint32_t>(number);
        return true;
    };
    const auto readBytes = [&](const QString& name,
                               std::uint64_t fabgl::project::PerformanceBudgetValues::*member,
                               const bool allowZero = false) {
        if (!object.contains(name))
            return true;
        const auto value = object.value(name);
        const double number = value.toDouble(-1.0);
        constexpr double MaximumBudgetBytes = 1024.0 * 1024.0 * 1024.0 * 1024.0;
        if (!value.isDouble() || !std::isfinite(number) || std::trunc(number) != number ||
            number < (allowZero ? 0.0 : 1.0) || number > MaximumBudgetBytes) {
            errorMessage = QObject::tr("%1.%2 is not a valid byte budget.").arg(category, name);
            return false;
        }
        budget.*member = static_cast<std::uint64_t>(number);
        return true;
    };
    return readTime(QStringLiteral("frameTotalMs"),
                    &fabgl::project::PerformanceBudgetValues::frameTotalMilliseconds) &&
           readTime(QStringLiteral("fixedUpdateMs"),
                    &fabgl::project::PerformanceBudgetValues::fixedUpdateMilliseconds) &&
           readTime(QStringLiteral("updateMs"),
                    &fabgl::project::PerformanceBudgetValues::updateMilliseconds) &&
           readTime(QStringLiteral("physicsMs"),
                    &fabgl::project::PerformanceBudgetValues::physicsMilliseconds) &&
           readTime(QStringLiteral("animationMs"),
                    &fabgl::project::PerformanceBudgetValues::animationMilliseconds) &&
           readTime(QStringLiteral("aiMs"),
                    &fabgl::project::PerformanceBudgetValues::aiMilliseconds) &&
           readTime(QStringLiteral("renderMs"),
                    &fabgl::project::PerformanceBudgetValues::renderMilliseconds) &&
           readTime(QStringLiteral("audioMs"),
                    &fabgl::project::PerformanceBudgetValues::audioMilliseconds) &&
           readTime(QStringLiteral("assetStreamingMs"),
                    &fabgl::project::PerformanceBudgetValues::assetStreamingMilliseconds) &&
           readCount(QStringLiteral("entities"),
                     &fabgl::project::PerformanceBudgetValues::entities) &&
           readCount(QStringLiteral("components"),
                     &fabgl::project::PerformanceBudgetValues::components) &&
           readCount(QStringLiteral("drawCalls"),
                     &fabgl::project::PerformanceBudgetValues::drawCalls) &&
           readCount(QStringLiteral("sprites"),
                     &fabgl::project::PerformanceBudgetValues::sprites) &&
           readCount(QStringLiteral("triangles"),
                     &fabgl::project::PerformanceBudgetValues::triangles) &&
           readCount(QStringLiteral("rays"), &fabgl::project::PerformanceBudgetValues::rays) &&
           readCount(QStringLiteral("particles"),
                     &fabgl::project::PerformanceBudgetValues::particles) &&
           readCount(QStringLiteral("audioVoices"),
                     &fabgl::project::PerformanceBudgetValues::audioVoices) &&
           readBytes(QStringLiteral("assetResidentBytes"),
                     &fabgl::project::PerformanceBudgetValues::assetResidentBytes) &&
           readBytes(QStringLiteral("internalRamBytes"),
                     &fabgl::project::PerformanceBudgetValues::internalRamBytes) &&
           readBytes(QStringLiteral("psramBytes"),
                     &fabgl::project::PerformanceBudgetValues::psramBytes, true) &&
           readBytes(QStringLiteral("flashBytes"),
                     &fabgl::project::PerformanceBudgetValues::flashBytes) &&
           readBytes(QStringLiteral("sdBytes"), &fabgl::project::PerformanceBudgetValues::sdBytes,
                     true) &&
           fabgl::project::validPerformanceBudget(budget);
}

bool decodePerformance(const QJsonValue& value, ProjectData& decoded, QString& errorMessage) {
    if (!value.isObject()) {
        errorMessage = QObject::tr("performance must be an object.");
        return false;
    }
    const auto performance = value.toObject();
    if (!rejectUnknownFields(performance,
                             {QStringLiteral("version"), QStringLiteral("pcProfile"),
                              QStringLiteral("esp32Profile"), QStringLiteral("customPc"),
                              QStringLiteral("customEsp32")},
                             QStringLiteral("performance"), errorMessage) ||
        !requireField(performance, QStringLiteral("version"), QJsonValue::Double,
                      QStringLiteral("performance"), errorMessage) ||
        !requireField(performance, QStringLiteral("pcProfile"), QJsonValue::String,
                      QStringLiteral("performance"), errorMessage) ||
        !requireField(performance, QStringLiteral("esp32Profile"), QJsonValue::String,
                      QStringLiteral("performance"), errorMessage)) {
        return false;
    }
    const double version = performance.value(QStringLiteral("version")).toDouble(-1.0);
    if (version != fabgl::project::PerformanceBudgetSettings::CurrentVersion) {
        errorMessage = QObject::tr("Unsupported performance budget version.");
        return false;
    }
    decoded.performance.version = static_cast<int>(version);
    const auto parseProfile = [&errorMessage](const QString& text,
                                              fabgl::project::PerformanceBudgetProfile& output) {
        if (!fabgl::project::parsePerformanceBudgetProfile(text.toStdString(), output)) {
            errorMessage =
                QObject::tr("Performance profile must be safe, balanced, maximum, or custom.");
            return false;
        }
        return true;
    };
    if (!parseProfile(performance.value(QStringLiteral("pcProfile")).toString(),
                      decoded.performance.pcProfile) ||
        !parseProfile(performance.value(QStringLiteral("esp32Profile")).toString(),
                      decoded.performance.esp32Profile)) {
        return false;
    }
    if (performance.contains(QStringLiteral("customPc")) &&
        !decodePerformanceValues(performance.value(QStringLiteral("customPc")),
                                 decoded.performance.pcCustom,
                                 QStringLiteral("performance.customPc"), errorMessage)) {
        return false;
    }
    if (performance.contains(QStringLiteral("customEsp32")) &&
        !decodePerformanceValues(performance.value(QStringLiteral("customEsp32")),
                                 decoded.performance.esp32Custom,
                                 QStringLiteral("performance.customEsp32"), errorMessage)) {
        return false;
    }
    return true;
}

bool validateInputValues(const QVector<ProjectInputValue>& values, qsizetype& totalBindings,
                         const QString& category, QString& errorMessage) {
    if (values.size() > MaximumInputValuesPerContext) {
        errorMessage = QObject::tr("%1 exceeds the value-count limit.").arg(category);
        return false;
    }
    QSet<QString> names;
    for (const auto& value : values) {
        if (!isValidText(value.name, 80) || names.contains(value.name) ||
            value.bindings.isEmpty() || value.bindings.size() > MaximumBindingsPerValue ||
            totalBindings > MaximumTotalBindings - value.bindings.size()) {
            errorMessage =
                QObject::tr("%1 contains an invalid name or binding count.").arg(category);
            return false;
        }
        names.insert(value.name);
        totalBindings += value.bindings.size();
        QSet<QString> controls;
        for (const auto& binding : value.bindings) {
            if (!isValidText(binding.control, 128) || controls.contains(binding.control) ||
                !std::isfinite(binding.scale) || std::abs(binding.scale) > 16.0 ||
                !std::isfinite(binding.threshold) || binding.threshold < 0.0 ||
                binding.threshold > 1.0) {
                errorMessage = QObject::tr("%1 contains an invalid binding.").arg(category);
                return false;
            }
            controls.insert(binding.control);
        }
    }
    return true;
}

bool validateProjectData(const ProjectData& data, QString& errorMessage) {
    errorMessage.clear();
    if (!isValidGuid(data.projectGuid) || !isValidText(data.name, 160) ||
        !isCanonicalProjectRoot(data.relativeRoot) || !isSafeScenePath(data.sceneFile) ||
        !isValidText(data.previewDemo, 80, true) || !isValidText(data.buildProgram, 1024) ||
        data.buildArguments.size() > MaximumBuildArguments || data.assets.size() > MaximumAssets ||
        data.inputContexts.size() > MaximumInputContexts ||
        data.packageDependencies.size() > MaximumPackageDependencies ||
        !isValidStableId(data.targetProfiles.pc) || !isValidStableId(data.targetProfiles.esp32) ||
        data.performance.version != fabgl::project::PerformanceBudgetSettings::CurrentVersion ||
        !fabgl::project::validPerformanceBudgetProfile(data.performance.pcProfile) ||
        !fabgl::project::validPerformanceBudgetProfile(data.performance.esp32Profile) ||
        !fabgl::project::validPerformanceBudget(data.performance.pcCustom) ||
        !fabgl::project::validPerformanceBudget(data.performance.esp32Custom)) {
        errorMessage = QObject::tr("Project identity, path, build, or target data is invalid.");
        return false;
    }
    for (const auto& argument : data.buildArguments) {
        if (!isValidText(argument, 4096, true)) {
            errorMessage = QObject::tr("A build argument is invalid.");
            return false;
        }
    }
    QSet<QString> assetGuids;
    QSet<QString> assetPaths;
    for (const auto& asset : data.assets) {
        const QString guid = asset.guid.toLower();
        const auto parsedGuid = fabgl::AssetGuid::parse(guid.toStdString());
        const QString path = canonicalAssetPath(asset.path);
        const QString pathKey = path.toCaseFolded();
        if (!parsedGuid || parsedGuid.value().isNil() || assetGuids.contains(guid) ||
            !isSafeAssetPath(path) || assetPaths.contains(pathKey) ||
            !isValidStableId(asset.type)) {
            errorMessage = QObject::tr("An asset GUID, path, or type is invalid or duplicated.");
            return false;
        }
        QJsonParseError importError;
        const auto importDocument =
            QJsonDocument::fromJson(asset.importSettings.toUtf8(), &importError);
        if (asset.importSettings.toUtf8().size() > 16 * 1024 ||
            importError.error != QJsonParseError::NoError || !importDocument.isObject() ||
            (asset.esp32Target != QStringLiteral("flash") &&
             asset.esp32Target != QStringLiteral("psram") &&
             asset.esp32Target != QStringLiteral("sd")) ||
            asset.dependencies.size() > MaximumAssets) {
            errorMessage = QObject::tr("An asset import setting or ESP32 target is invalid.");
            return false;
        }
        if (!validateCanonicalImporterSettings(asset, errorMessage))
            return false;
        assetGuids.insert(guid);
        assetPaths.insert(pathKey);
    }
    for (const auto& asset : data.assets) {
        QSet<QString> dependencies;
        for (const auto& dependencyValue : asset.dependencies) {
            const auto dependency = dependencyValue.toLower();
            const auto parsed = fabgl::AssetGuid::parse(dependency.toStdString());
            if (!parsed || parsed.value().isNil() || dependency == asset.guid.toLower() ||
                dependencies.contains(dependency) || !assetGuids.contains(dependency)) {
                errorMessage = QObject::tr("An asset dependency is invalid or duplicated.");
                return false;
            }
            dependencies.insert(dependency);
        }
    }
    QSet<QString> contextNames;
    qsizetype totalBindings = 0;
    for (const auto& context : data.inputContexts) {
        if (!isValidText(context.name, 80) || contextNames.contains(context.name) ||
            context.priority < -1000000 || context.priority > 1000000 ||
            !validateInputValues(context.actions, totalBindings, QStringLiteral("actions"),
                                 errorMessage) ||
            !validateInputValues(context.axes, totalBindings, QStringLiteral("axes"),
                                 errorMessage)) {
            if (errorMessage.isEmpty()) {
                errorMessage = QObject::tr("An input context is invalid or duplicated.");
            }
            return false;
        }
        contextNames.insert(context.name);
    }
    QSet<QString> packageIds;
    for (const auto& dependency : data.packageDependencies) {
        if (!isValidStableId(dependency.id) || packageIds.contains(dependency.id) ||
            !isValidVersionRequirement(dependency.version)) {
            errorMessage = QObject::tr("A package dependency is invalid or duplicated.");
            return false;
        }
        packageIds.insert(dependency.id);
    }
    errorMessage.clear();
    return true;
}

QJsonArray encodeInputValues(QVector<ProjectInputValue> values) {
    std::sort(values.begin(), values.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });
    QJsonArray encoded;
    for (auto& value : values) {
        std::sort(value.bindings.begin(), value.bindings.end(),
                  [](const auto& left, const auto& right) {
                      if (left.control != right.control) {
                          return left.control < right.control;
                      }
                      if (left.scale != right.scale) {
                          return left.scale < right.scale;
                      }
                      return left.threshold < right.threshold;
                  });
        QJsonArray bindings;
        for (const auto& binding : value.bindings) {
            bindings.append(QJsonObject{{QStringLiteral("control"), binding.control},
                                        {QStringLiteral("scale"), binding.scale},
                                        {QStringLiteral("threshold"), binding.threshold}});
        }
        encoded.append(QJsonObject{{QStringLiteral("name"), value.name},
                                   {QStringLiteral("bindings"), bindings}});
    }
    return encoded;
}

QJsonObject encodePerformanceValues(const fabgl::project::PerformanceBudgetValues& budget) {
    return {{QStringLiteral("frameTotalMs"), budget.frameTotalMilliseconds},
            {QStringLiteral("fixedUpdateMs"), budget.fixedUpdateMilliseconds},
            {QStringLiteral("updateMs"), budget.updateMilliseconds},
            {QStringLiteral("physicsMs"), budget.physicsMilliseconds},
            {QStringLiteral("animationMs"), budget.animationMilliseconds},
            {QStringLiteral("aiMs"), budget.aiMilliseconds},
            {QStringLiteral("renderMs"), budget.renderMilliseconds},
            {QStringLiteral("audioMs"), budget.audioMilliseconds},
            {QStringLiteral("assetStreamingMs"), budget.assetStreamingMilliseconds},
            {QStringLiteral("entities"), static_cast<qint64>(budget.entities)},
            {QStringLiteral("components"), static_cast<qint64>(budget.components)},
            {QStringLiteral("drawCalls"), static_cast<qint64>(budget.drawCalls)},
            {QStringLiteral("sprites"), static_cast<qint64>(budget.sprites)},
            {QStringLiteral("triangles"), static_cast<qint64>(budget.triangles)},
            {QStringLiteral("rays"), static_cast<qint64>(budget.rays)},
            {QStringLiteral("particles"), static_cast<qint64>(budget.particles)},
            {QStringLiteral("audioVoices"), static_cast<qint64>(budget.audioVoices)},
            {QStringLiteral("assetResidentBytes"), static_cast<qint64>(budget.assetResidentBytes)},
            {QStringLiteral("internalRamBytes"), static_cast<qint64>(budget.internalRamBytes)},
            {QStringLiteral("psramBytes"), static_cast<qint64>(budget.psramBytes)},
            {QStringLiteral("flashBytes"), static_cast<qint64>(budget.flashBytes)},
            {QStringLiteral("sdBytes"), static_cast<qint64>(budget.sdBytes)}};
}

} // namespace

bool ProjectDocument::load(const QString& filePath, ProjectData& data, QString& errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QObject::tr("Cannot open %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    if (file.size() <= 0 || file.size() > MaximumProjectBytes) {
        errorMessage = QObject::tr("Project manifest size is outside the 1 MiB limit.");
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QObject::tr("Invalid project JSON at byte %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString());
        return false;
    }

    const auto root = document.object();
    if (!requireField(root, QStringLiteral("kind"), QJsonValue::String, QStringLiteral("project"),
                      errorMessage) ||
        !requireField(root, QStringLiteral("formatVersion"), QJsonValue::Double,
                      QStringLiteral("project"), errorMessage) ||
        !requireField(root, QStringLiteral("name"), QJsonValue::String, QStringLiteral("project"),
                      errorMessage) ||
        !requireField(root, QStringLiteral("projectRoot"), QJsonValue::String,
                      QStringLiteral("project"), errorMessage)) {
        return false;
    }
    if (root.value(QStringLiteral("kind")).toString() != QString::fromLatin1(ProjectKind)) {
        errorMessage = QObject::tr("The file is not a FabGL Studio project.");
        return false;
    }
    const double rawVersion = root.value(QStringLiteral("formatVersion")).toDouble(-1.0);
    if (!std::isfinite(rawVersion) || std::trunc(rawVersion) != rawVersion || rawVersion < 1.0 ||
        rawVersion > static_cast<double>(FormatVersion)) {
        errorMessage = QObject::tr("Unsupported project format version %1; expected 1 or %2.")
                           .arg(rawVersion)
                           .arg(FormatVersion);
        return false;
    }
    const int version = static_cast<int>(rawVersion);
    const QSet<QString> legacyFields = {
        QStringLiteral("kind"),        QStringLiteral("formatVersion"),
        QStringLiteral("projectGuid"), QStringLiteral("name"),
        QStringLiteral("projectRoot"), QStringLiteral("startupScene"),
        QStringLiteral("sceneFile"),   QStringLiteral("previewDemo"),
        QStringLiteral("scene"),       QStringLiteral("build")};
    const QSet<QString> currentFields = {
        QStringLiteral("kind"),        QStringLiteral("formatVersion"),
        QStringLiteral("projectGuid"), QStringLiteral("name"),
        QStringLiteral("projectRoot"), QStringLiteral("startupScene"),
        QStringLiteral("previewDemo"), QStringLiteral("build"),
        QStringLiteral("assets"),      QStringLiteral("input"),
        QStringLiteral("packages"),    QStringLiteral("targetProfiles"),
        QStringLiteral("performance")};
    if (!rejectUnknownFields(root, version == FormatVersion ? currentFields : legacyFields,
                             QStringLiteral("project"), errorMessage)) {
        return false;
    }

    ProjectData decoded;
    decoded.sourceFormatVersion = version;
    decoded.name = root.value(QStringLiteral("name")).toString();
    decoded.relativeRoot = root.value(QStringLiteral("projectRoot")).toString();
    decoded.projectGuid = root.value(QStringLiteral("projectGuid")).toString();
    if (decoded.projectGuid.isEmpty() && version == 1) {
        const QByteArray stableName =
            QStringLiteral("legacy-project:%1").arg(decoded.name).toUtf8();
        decoded.projectGuid = QString::fromStdString(
            fabgl::AssetGuid::fromStableName(
                std::string_view(stableName.constData(),
                                 static_cast<std::size_t>(stableName.size())))
                .toString());
    }
    decoded.sceneFile = root.value(QStringLiteral("startupScene"))
                            .toString(root.value(QStringLiteral("sceneFile"))
                                          .toString(QStringLiteral("Scenes/Main.fglscene")));
    decoded.previewDemo = root.value(QStringLiteral("previewDemo")).toString();
    if (!isValidGuid(decoded.projectGuid) || !isValidText(decoded.name, 160) ||
        !isCanonicalProjectRoot(decoded.relativeRoot) || !isSafeScenePath(decoded.sceneFile) ||
        !isValidText(decoded.previewDemo, 80, true)) {
        errorMessage = QObject::tr("Project identity or path data is invalid.");
        return false;
    }

    const auto build = root.value(QStringLiteral("build"));
    if ((!build.isUndefined() &&
         !decodeBuild(build, decoded, version == FormatVersion, errorMessage)) ||
        (build.isUndefined() && version == FormatVersion)) {
        if (build.isUndefined()) {
            errorMessage = QObject::tr("Project is missing required field 'build'.");
        }
        return false;
    }
    if (version == FormatVersion) {
        if (!requireField(root, QStringLiteral("projectGuid"), QJsonValue::String,
                          QStringLiteral("project"), errorMessage) ||
            !requireField(root, QStringLiteral("startupScene"), QJsonValue::String,
                          QStringLiteral("project"), errorMessage) ||
            !requireField(root, QStringLiteral("input"), QJsonValue::Object,
                          QStringLiteral("project"), errorMessage) ||
            !requireField(root, QStringLiteral("packages"), QJsonValue::Array,
                          QStringLiteral("project"), errorMessage) ||
            !requireField(root, QStringLiteral("targetProfiles"), QJsonValue::Object,
                          QStringLiteral("project"), errorMessage) ||
            (!root.value(QStringLiteral("assets")).isUndefined() &&
             !decodeAssets(root.value(QStringLiteral("assets")), decoded, errorMessage)) ||
            !decodeInput(root.value(QStringLiteral("input")), decoded, errorMessage) ||
            !decodePackages(root.value(QStringLiteral("packages")), decoded, errorMessage) ||
            !decodeTargetProfiles(root.value(QStringLiteral("targetProfiles")), decoded,
                                  errorMessage)) {
            return false;
        }
        if (root.contains(QStringLiteral("performance")) &&
            !decodePerformance(root.value(QStringLiteral("performance")), decoded, errorMessage)) {
            return false;
        }
    }

    if (!validateProjectData(decoded, errorMessage)) {
        return false;
    }
    data = std::move(decoded);
    errorMessage.clear();
    return true;
}

bool ProjectDocument::save(const QString& filePath, const ProjectData& data,
                           QString& errorMessage) {
    const QByteArray bytes = serialized(data, errorMessage);
    if (bytes.isEmpty()) {
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = QObject::tr("Cannot write %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        errorMessage = QObject::tr("Could not completely write %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        errorMessage = QObject::tr("Could not atomically replace %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    errorMessage.clear();
    return true;
}

QByteArray ProjectDocument::serialized(const ProjectData& data, QString& errorMessage) {
    if (!validateProjectData(data, errorMessage)) {
        return {};
    }

    QJsonArray buildArguments;
    for (const auto& argument : data.buildArguments) {
        buildArguments.append(argument);
    }
    const QJsonObject build{{QStringLiteral("program"), data.buildProgram},
                            {QStringLiteral("arguments"), buildArguments}};

    auto assets = data.assets;
    std::sort(assets.begin(), assets.end(), [](const auto& left, const auto& right) {
        return left.guid.toLower() < right.guid.toLower();
    });
    QJsonArray encodedAssets;
    for (const auto& asset : assets) {
        QJsonObject encoded{{QStringLiteral("guid"), asset.guid.toLower()},
                            {QStringLiteral("path"), canonicalAssetPath(asset.path)},
                            {QStringLiteral("type"), asset.type}};
        if (asset.hasImportMetadata) {
            QJsonArray dependencies;
            auto sortedDependencies = asset.dependencies;
            std::sort(sortedDependencies.begin(), sortedDependencies.end());
            for (const auto& dependency : sortedDependencies)
                dependencies.push_back(dependency.toLower());
            const auto settings = QJsonDocument::fromJson(asset.importSettings.toUtf8()).object();
            encoded.insert(QStringLiteral("import"),
                           QJsonObject{{QStringLiteral("settings"), settings},
                                       {QStringLiteral("esp32Target"), asset.esp32Target.toLower()},
                                       {QStringLiteral("dependencies"), dependencies}});
        }
        encodedAssets.append(encoded);
    }

    auto contexts = data.inputContexts;
    std::sort(contexts.begin(), contexts.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });
    QJsonArray encodedContexts;
    for (const auto& context : contexts) {
        encodedContexts.append(
            QJsonObject{{QStringLiteral("name"), context.name},
                        {QStringLiteral("priority"), context.priority},
                        {QStringLiteral("enabled"), context.enabled},
                        {QStringLiteral("actions"), encodeInputValues(context.actions)},
                        {QStringLiteral("axes"), encodeInputValues(context.axes)}});
    }

    auto packages = data.packageDependencies;
    std::sort(packages.begin(), packages.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    QJsonArray encodedPackages;
    for (const auto& dependency : packages) {
        encodedPackages.append(QJsonObject{
            {QStringLiteral("id"), dependency.id},
            {QStringLiteral("version"), canonicalVersionRequirement(dependency.version)}});
    }

    QJsonObject root{{QStringLiteral("kind"), QString::fromLatin1(ProjectKind)},
                     {QStringLiteral("formatVersion"), FormatVersion},
                     {QStringLiteral("projectGuid"), data.projectGuid},
                     {QStringLiteral("name"), data.name},
                     {QStringLiteral("projectRoot"), data.relativeRoot},
                     {QStringLiteral("startupScene"), data.sceneFile}};
    if (!data.previewDemo.isEmpty()) {
        root.insert(QStringLiteral("previewDemo"), data.previewDemo);
    }
    root.insert(QStringLiteral("build"), build);
    root.insert(QStringLiteral("assets"), encodedAssets);
    root.insert(QStringLiteral("input"),
                QJsonObject{{QStringLiteral("contexts"), encodedContexts}});
    root.insert(QStringLiteral("packages"), encodedPackages);
    root.insert(QStringLiteral("targetProfiles"),
                QJsonObject{{QStringLiteral("pc"), data.targetProfiles.pc},
                            {QStringLiteral("esp32"), data.targetProfiles.esp32}});
    root.insert(
        QStringLiteral("performance"),
        QJsonObject{
            {QStringLiteral("version"), fabgl::project::PerformanceBudgetSettings::CurrentVersion},
            {QStringLiteral("pcProfile"),
             QString::fromLatin1(
                 fabgl::project::performanceBudgetProfileId(data.performance.pcProfile).data())},
            {QStringLiteral("esp32Profile"),
             QString::fromLatin1(
                 fabgl::project::performanceBudgetProfileId(data.performance.esp32Profile).data())},
            {QStringLiteral("customPc"), encodePerformanceValues(data.performance.pcCustom)},
            {QStringLiteral("customEsp32"),
             encodePerformanceValues(data.performance.esp32Custom)}});

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumProjectBytes) {
        errorMessage = QObject::tr("Serialized project manifest exceeds the 1 MiB limit.");
        return {};
    }
    errorMessage.clear();
    return bytes;
}

QString ProjectDocument::absoluteProjectRoot(const QString& filePath, const QString& relativeRoot) {
    return QDir::cleanPath(QFileInfo(filePath).absoluteDir().absoluteFilePath(relativeRoot));
}

QString ProjectDocument::absoluteScenePath(const QString& projectFilePath,
                                           const ProjectData& data) {
    const QDir root(absoluteProjectRoot(projectFilePath, data.relativeRoot));
    return QDir::cleanPath(root.absoluteFilePath(data.sceneFile));
}

} // namespace fgl::studio
