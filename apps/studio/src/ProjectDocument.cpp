#include "ProjectDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSaveFile>

#include <utility>

namespace fgl::studio {
namespace {

constexpr auto ProjectKind = "FabGLStudioProject";

bool containsUnsafePathSyntax(const QString& path) {
    if (path.isEmpty() || path.contains(QLatin1Char(':')) || path.contains(QChar::Null)) {
        return true;
    }
    QString portablePath = path;
    portablePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (QDir::isAbsolutePath(portablePath) || portablePath.startsWith(QStringLiteral("//"))) {
        return true;
    }
    const auto segments = portablePath.split(QLatin1Char('/'));
    return segments.contains(QStringLiteral(".."));
}

bool isCanonicalProjectRoot(const QString& path) {
    return path == QStringLiteral(".") && !containsUnsafePathSyntax(path);
}

bool isSafeScenePath(const QString& path) {
    if (containsUnsafePathSyntax(path)) {
        return false;
    }
    const auto clean = QDir::cleanPath(path);
    return clean != QStringLiteral(".") && clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

} // namespace

bool ProjectDocument::load(const QString& filePath, ProjectData& data, QString& errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QObject::tr("Cannot open %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
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
    if (root.value(QStringLiteral("kind")).toString() != QString::fromLatin1(ProjectKind)) {
        errorMessage = QObject::tr("The file is not a FabGL Studio project.");
        return false;
    }
    const auto version = root.value(QStringLiteral("formatVersion")).toInt(-1);
    if (version != FormatVersion) {
        errorMessage = QObject::tr("Unsupported project format version %1; expected %2.")
                           .arg(version)
                           .arg(FormatVersion);
        return false;
    }

    ProjectData decoded;
    decoded.projectGuid = root.value(QStringLiteral("projectGuid")).toString().trimmed();
    decoded.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (decoded.name.isEmpty()) {
        decoded.name = QFileInfo(filePath).completeBaseName();
    }
    decoded.relativeRoot = root.value(QStringLiteral("projectRoot")).toString(QStringLiteral("."));
    decoded.sceneFile = root.value(QStringLiteral("startupScene"))
                            .toString(root.value(QStringLiteral("sceneFile"))
                                          .toString(QStringLiteral("Scenes/Main.fglscene")));
    decoded.previewDemo = root.value(QStringLiteral("previewDemo")).toString().trimmed();
    if (!isCanonicalProjectRoot(decoded.relativeRoot) || !isSafeScenePath(decoded.sceneFile)) {
        errorMessage =
            QObject::tr("projectRoot must be '.' and startupScene must be a safe relative "
                        "path without '..', drive, or ADS syntax.");
        return false;
    }

    const auto buildValue = root.value(QStringLiteral("build"));
    if (buildValue.isObject()) {
        const auto build = buildValue.toObject();
        const auto program = build.value(QStringLiteral("program")).toString().trimmed();
        if (!program.isEmpty()) {
            decoded.buildProgram = program;
        }
        const auto argumentsValue = build.value(QStringLiteral("arguments"));
        if (!argumentsValue.isUndefined()) {
            if (!argumentsValue.isArray()) {
                errorMessage = QObject::tr("build.arguments must be an array of strings.");
                return false;
            }
            decoded.buildArguments.clear();
            for (const auto& argument : argumentsValue.toArray()) {
                if (!argument.isString()) {
                    errorMessage = QObject::tr("Every build argument must be a string.");
                    return false;
                }
                decoded.buildArguments.push_back(argument.toString());
            }
        }
    }

    data = std::move(decoded);
    return true;
}

bool ProjectDocument::save(const QString& filePath, const ProjectData& data,
                           QString& errorMessage) {
    if (!isCanonicalProjectRoot(data.relativeRoot) || !isSafeScenePath(data.sceneFile)) {
        errorMessage =
            QObject::tr("projectRoot must be '.' and startupScene must be a safe relative "
                        "path without '..', drive, or ADS syntax.");
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QString::fromLatin1(ProjectKind));
    root.insert(QStringLiteral("formatVersion"), FormatVersion);
    if (!data.projectGuid.isEmpty()) {
        root.insert(QStringLiteral("projectGuid"), data.projectGuid);
    }
    root.insert(QStringLiteral("name"), data.name);
    root.insert(QStringLiteral("projectRoot"), data.relativeRoot);
    root.insert(QStringLiteral("startupScene"), data.sceneFile);
    if (!data.previewDemo.isEmpty()) {
        root.insert(QStringLiteral("previewDemo"), data.previewDemo);
    }

    QJsonArray buildArguments;
    for (const auto& argument : data.buildArguments) {
        buildArguments.append(argument);
    }
    QJsonObject build;
    build.insert(QStringLiteral("program"), data.buildProgram);
    build.insert(QStringLiteral("arguments"), buildArguments);
    root.insert(QStringLiteral("build"), build);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = QObject::tr("Cannot write %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
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
    return true;
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
