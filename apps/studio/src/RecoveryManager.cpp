#include "RecoveryManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace fgl::studio {
namespace {

constexpr auto RecoveryKind = "FabGLStudioRecovery";
constexpr int RecoveryVersion = 1;
constexpr qint64 MaximumRecoveryFileBytes = 128LL * 1024LL * 1024LL;

QString normalizedPath(const QString& path) {
    if (path.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    QString normalized = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    normalized = QDir::cleanPath(normalized);
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString projectKey(const QString& projectPath) {
    const QByteArray source = projectPath.isEmpty() ? QByteArrayLiteral("untitled")
                                                    : projectPath.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
}

QByteArray sha256(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool safeRelativePath(QString path) {
    if (path.trimmed().isEmpty() || path.contains(QChar::Null) || path.contains(QLatin1Char(':')) ||
        QDir::isAbsolutePath(path)) {
        return false;
    }
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString clean = QDir::cleanPath(path);
    return clean != QStringLiteral(".") && clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

bool atomicWrite(const QString& filePath, const QByteArray& bytes, QString& errorMessage) {
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        errorMessage = QObject::tr("Cannot create the recovery destination directory: %1")
                           .arg(QDir::toNativeSeparators(QFileInfo(filePath).absolutePath()));
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = QObject::tr("Cannot atomically write %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        errorMessage = QObject::tr("Incomplete write to %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        errorMessage = QObject::tr("Cannot commit atomic write to %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    return true;
}

bool rotateFileBackup(const QString& filePath, QString& errorMessage) {
    constexpr int MaximumFileBackups = 3;
    if (!QFileInfo::exists(filePath)) {
        return true;
    }
    const QString oldest = filePath + QStringLiteral(".bak.%1").arg(MaximumFileBackups);
    if (QFileInfo::exists(oldest) && !QFile::remove(oldest)) {
        errorMessage = QObject::tr("Cannot rotate old backup %1")
                           .arg(QDir::toNativeSeparators(oldest));
        return false;
    }
    for (int index = MaximumFileBackups - 1; index >= 1; --index) {
        const QString source = filePath + QStringLiteral(".bak.%1").arg(index);
        const QString destination = filePath + QStringLiteral(".bak.%1").arg(index + 1);
        if (QFileInfo::exists(source) && !QFile::rename(source, destination)) {
            errorMessage = QObject::tr("Cannot rotate backup %1")
                               .arg(QDir::toNativeSeparators(source));
            return false;
        }
    }
    const QString newest = filePath + QStringLiteral(".bak.1");
    if (!QFile::copy(filePath, newest)) {
        errorMessage = QObject::tr("Cannot create backup %1")
                           .arg(QDir::toNativeSeparators(newest));
        return false;
    }
    return true;
}

} // namespace

RecoveryManager::RecoveryManager(QString storageRoot, const int maximumBackupsPerProject)
    : m_storageRoot(storageRoot.trimmed().isEmpty()
                        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                              .filePath(QStringLiteral("recovery"))
                        : QFileInfo(storageRoot).absoluteFilePath()),
      m_maximumBackupsPerProject(std::clamp(maximumBackupsPerProject, 1, 20)) {}

QString RecoveryManager::storageRoot() const {
    return QDir::cleanPath(m_storageRoot);
}

int RecoveryManager::maximumBackupsPerProject() const noexcept {
    return m_maximumBackupsPerProject;
}

bool RecoveryManager::beginSession(QString& errorMessage) {
    if (!ensureStorage(errorMessage)) {
        return false;
    }
    m_previousSessionWasUnclean = QFileInfo::exists(sessionMarkerPath());
    QJsonObject marker;
    marker.insert(QStringLiteral("kind"), QStringLiteral("FabGLStudioSessionMarker"));
    marker.insert(QStringLiteral("startedUtc"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    marker.insert(QStringLiteral("pid"),
                  QString::number(static_cast<qint64>(QCoreApplication::applicationPid())));
    if (!atomicWrite(sessionMarkerPath(), QJsonDocument(marker).toJson(QJsonDocument::Compact),
                     errorMessage)) {
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RecoveryManager::endSession(QString& errorMessage) {
    if (!QFileInfo::exists(sessionMarkerPath())) {
        errorMessage.clear();
        return true;
    }
    if (!QFile::remove(sessionMarkerPath())) {
        errorMessage = QObject::tr("Cannot remove the clean-session marker: %1")
                           .arg(QDir::toNativeSeparators(sessionMarkerPath()));
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RecoveryManager::previousSessionWasUnclean() const noexcept {
    return m_previousSessionWasUnclean;
}

bool RecoveryManager::writeAutosave(const QString& projectPath, const QString& sceneRelativePath,
                                    const QByteArray& projectData, const QByteArray& sceneData,
                                    QString& errorMessage) {
    if (!ensureStorage(errorMessage)) {
        return false;
    }
    if (!safeRelativePath(sceneRelativePath) || projectData.isEmpty() || sceneData.isEmpty() ||
        projectData.size() > MaximumRecoveryFileBytes || sceneData.size() > MaximumRecoveryFileBytes) {
        errorMessage = QObject::tr("Recovery input is empty, oversized, or contains an unsafe scene path.");
        return false;
    }
    QJsonParseError projectParseError;
    const auto projectDocument = QJsonDocument::fromJson(projectData, &projectParseError);
    if (projectParseError.error != QJsonParseError::NoError || !projectDocument.isObject() ||
        !sceneData.startsWith("fglscene ")) {
        errorMessage = QObject::tr("Refusing to autosave corrupt project or scene data.");
        return false;
    }
    const QString normalizedProject = normalizedPath(projectPath);
    const QString key = projectKey(normalizedProject);
    const auto timestamp = QDateTime::currentDateTimeUtc();
    QJsonObject root;
    root.insert(QStringLiteral("kind"), QString::fromLatin1(RecoveryKind));
    root.insert(QStringLiteral("version"), RecoveryVersion);
    root.insert(QStringLiteral("projectKey"), key);
    root.insert(QStringLiteral("projectPath"), normalizedProject);
    root.insert(QStringLiteral("sceneRelativePath"), QDir::cleanPath(sceneRelativePath));
    root.insert(QStringLiteral("timestampUtc"), timestamp.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("projectSha256"), QString::fromLatin1(sha256(projectData)));
    root.insert(QStringLiteral("sceneSha256"), QString::fromLatin1(sha256(sceneData)));
    root.insert(QStringLiteral("projectData"), QString::fromLatin1(projectData.toBase64()));
    root.insert(QStringLiteral("sceneData"), QString::fromLatin1(sceneData.toBase64()));
    const QString fileName =
        QStringLiteral("%1-%2-%3.fglrecovery")
            .arg(key, timestamp.toString(QStringLiteral("yyyyMMddHHmmsszzz")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString destination = QDir(m_storageRoot).filePath(fileName);
    const QByteArray recoveryBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (recoveryBytes.size() > MaximumRecoveryFileBytes) {
        errorMessage = QObject::tr("The encoded recovery snapshot exceeds the size limit.");
        return false;
    }
    if (!atomicWrite(destination, recoveryBytes, errorMessage)) {
        return false;
    }
    rotateProjectBackups(normalizedProject);
    errorMessage.clear();
    return true;
}

QVector<RecoveryEntry> RecoveryManager::entries() const {
    QVector<RecoveryEntry> result;
    const QDir directory(m_storageRoot);
    const auto files = directory.entryInfoList({QStringLiteral("*.fglrecovery")}, QDir::Files,
                                                QDir::Time | QDir::Reversed);
    result.reserve(files.size());
    for (const auto& file : files) {
        result.push_back(readEntry(file.absoluteFilePath()));
    }
    std::sort(result.begin(), result.end(), [](const RecoveryEntry& left, const RecoveryEntry& right) {
        return left.timestamp > right.timestamp;
    });
    return result;
}

bool RecoveryManager::restore(const RecoveryEntry& entry, const QString& destinationProjectPath,
                              QString& errorMessage) const {
    const RecoveryEntry verified = readEntry(entry.filePath);
    if (verified.corrupt) {
        errorMessage = verified.errorMessage;
        return false;
    }
    const QString destination = normalizedPath(destinationProjectPath.trimmed().isEmpty()
                                                   ? verified.projectPath
                                                   : destinationProjectPath);
    if (destination.isEmpty() ||
        !destination.endsWith(QStringLiteral(".fglproject"), Qt::CaseInsensitive) ||
        !safeRelativePath(verified.sceneRelativePath)) {
        errorMessage = QObject::tr("Recovery requires a valid .fglproject destination and safe scene path.");
        return false;
    }
    const QDir projectDirectory(QFileInfo(destination).absolutePath());
    const QString scenePath = projectDirectory.filePath(verified.sceneRelativePath);
    const QString cleanProjectDirectory =
        QDir::fromNativeSeparators(QDir::cleanPath(projectDirectory.absolutePath()));
    const QString cleanScenePath =
        QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(scenePath).absoluteFilePath()));
    const QString projectPrefix = cleanProjectDirectory + QLatin1Char('/');
    if (!cleanScenePath.startsWith(projectPrefix,
#ifdef Q_OS_WIN
                                   Qt::CaseInsensitive
#else
                                   Qt::CaseSensitive
#endif
                                   )) {
        errorMessage = QObject::tr("Recovered scene path escapes the project directory.");
        return false;
    }
    if (!rotateFileBackup(destination, errorMessage) ||
        !rotateFileBackup(cleanScenePath, errorMessage)) {
        return false;
    }
    if (!atomicWrite(cleanScenePath, verified.sceneData, errorMessage) ||
        !atomicWrite(destination, verified.projectData, errorMessage)) {
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RecoveryManager::discard(const RecoveryEntry& entry, QString& errorMessage) const {
    if (!pathIsManaged(entry.filePath)) {
        errorMessage = QObject::tr("Refusing to discard a file outside the recovery directory.");
        return false;
    }
    if (QFileInfo::exists(entry.filePath) && !QFile::remove(entry.filePath)) {
        errorMessage = QObject::tr("Cannot discard recovery file %1")
                           .arg(QDir::toNativeSeparators(entry.filePath));
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RecoveryManager::discardProject(const QString& projectPath, QString& errorMessage) const {
    const QString normalizedProject = normalizedPath(projectPath);
    for (const auto& entry : entries()) {
        if (entry.projectPath == normalizedProject && !discard(entry, errorMessage)) {
            return false;
        }
    }
    errorMessage.clear();
    return true;
}

void RecoveryManager::recordLastProject(const QString& projectPath) const {
    QSettings settings;
    settings.setValue(QStringLiteral("recovery/lastProjectPath"), normalizedPath(projectPath));
}

QString RecoveryManager::lastProjectPath() const {
    QSettings settings;
    return settings.value(QStringLiteral("recovery/lastProjectPath")).toString();
}

RecoveryEntry RecoveryManager::readEntry(const QString& filePath) const {
    RecoveryEntry entry;
    entry.filePath = QFileInfo(filePath).absoluteFilePath();
    entry.id = QFileInfo(filePath).completeBaseName();
    QFile file(filePath);
    if (!pathIsManaged(filePath) || !file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.size() > MaximumRecoveryFileBytes) {
        entry.corrupt = true;
        entry.errorMessage = QObject::tr("Recovery file is inaccessible, empty, oversized, or unmanaged.");
        return entry;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        entry.corrupt = true;
        entry.errorMessage = QObject::tr("Corrupt recovery JSON: %1").arg(parseError.errorString());
        return entry;
    }
    const auto root = document.object();
    entry.projectPath = root.value(QStringLiteral("projectPath")).toString();
    entry.sceneRelativePath = root.value(QStringLiteral("sceneRelativePath")).toString();
    entry.timestamp = QDateTime::fromString(root.value(QStringLiteral("timestampUtc")).toString(),
                                            Qt::ISODateWithMs);
    entry.projectData = QByteArray::fromBase64(
        root.value(QStringLiteral("projectData")).toString().toLatin1());
    entry.sceneData =
        QByteArray::fromBase64(root.value(QStringLiteral("sceneData")).toString().toLatin1());
    QJsonParseError projectParseError;
    const auto projectDocument = QJsonDocument::fromJson(entry.projectData, &projectParseError);
    const bool valid = root.value(QStringLiteral("kind")).toString() ==
                           QString::fromLatin1(RecoveryKind) &&
                       root.value(QStringLiteral("version")).toInt(-1) == RecoveryVersion &&
                       entry.timestamp.isValid() && safeRelativePath(entry.sceneRelativePath) &&
                       projectParseError.error == QJsonParseError::NoError &&
                       projectDocument.isObject() && entry.sceneData.startsWith("fglscene ") &&
                       root.value(QStringLiteral("projectSha256")).toString().toLatin1() ==
                           sha256(entry.projectData) &&
                       root.value(QStringLiteral("sceneSha256")).toString().toLatin1() ==
                           sha256(entry.sceneData);
    if (!valid) {
        entry.corrupt = true;
        entry.errorMessage = QObject::tr("Recovery metadata, payload, or checksum is invalid.");
    }
    return entry;
}

QString RecoveryManager::sessionMarkerPath() const {
    return QDir(m_storageRoot).filePath(QStringLiteral("active-session.json"));
}

bool RecoveryManager::ensureStorage(QString& errorMessage) const {
    if (m_storageRoot.trimmed().isEmpty() || !QDir().mkpath(m_storageRoot)) {
        errorMessage = QObject::tr("Cannot create recovery storage %1")
                           .arg(QDir::toNativeSeparators(m_storageRoot));
        return false;
    }
    errorMessage.clear();
    return true;
}

void RecoveryManager::rotateProjectBackups(const QString& normalizedProjectPath) const {
    QVector<RecoveryEntry> projectEntries;
    for (const auto& entry : entries()) {
        if (entry.projectPath == normalizedProjectPath) {
            projectEntries.push_back(entry);
        }
    }
    std::sort(projectEntries.begin(), projectEntries.end(),
              [](const RecoveryEntry& left, const RecoveryEntry& right) {
                  return left.timestamp > right.timestamp;
              });
    for (qsizetype index = static_cast<qsizetype>(m_maximumBackupsPerProject);
         index < projectEntries.size(); ++index) {
        QString ignoredError;
        (void)discard(projectEntries.at(index), ignoredError);
    }
}

bool RecoveryManager::pathIsManaged(const QString& filePath) const {
    const QString root = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(m_storageRoot).absoluteFilePath()));
    const QString candidate = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(filePath).absoluteFilePath()));
    const QString prefix = root + QLatin1Char('/');
    return candidate.startsWith(prefix,
#ifdef Q_OS_WIN
                                Qt::CaseInsensitive
#else
                                Qt::CaseSensitive
#endif
                                );
}

} // namespace fgl::studio
