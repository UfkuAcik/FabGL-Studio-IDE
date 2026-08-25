#include "ProjectTrustStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSettings>

namespace fgl::studio {

QString ProjectTrustStore::normalizedProjectPath(const QString& projectPath) {
    if (projectPath.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(projectPath);
    QString path = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

QString ProjectTrustStore::decisionKey(const QString& projectPath) {
    const QString normalized = normalizedProjectPath(projectPath);
    if (normalized.isEmpty()) {
        return {};
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool ProjectTrustStore::isTrusted(const QString& projectPath) const {
    const QString key = decisionKey(projectPath);
    if (key.isEmpty()) {
        return false;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("security/projectTrust/%1").arg(key));
    const bool trusted = settings.value(QStringLiteral("trusted"), false).toBool() &&
                         settings.value(QStringLiteral("path")).toString() ==
                             normalizedProjectPath(projectPath);
    settings.endGroup();
    return trusted;
}

bool ProjectTrustStore::setTrusted(const QString& projectPath, const bool trusted,
                                   QString& errorMessage) const {
    const QString path = normalizedProjectPath(projectPath);
    const QString key = decisionKey(path);
    if (path.isEmpty() || key.isEmpty() ||
        !path.endsWith(QStringLiteral(".fglproject"), Qt::CaseInsensitive)) {
        errorMessage = QObject::tr("A valid .fglproject path is required for a trust decision.");
        return false;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("security/projectTrust/%1").arg(key));
    settings.setValue(QStringLiteral("path"), path);
    settings.setValue(QStringLiteral("trusted"), trusted);
    settings.setValue(QStringLiteral("decidedUtc"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        errorMessage = QObject::tr("The project trust decision could not be stored.");
        return false;
    }
    errorMessage.clear();
    return true;
}

bool ProjectTrustStore::clearDecision(const QString& projectPath, QString& errorMessage) const {
    const QString key = decisionKey(projectPath);
    if (key.isEmpty()) {
        errorMessage = QObject::tr("A valid project path is required.");
        return false;
    }
    QSettings settings;
    settings.remove(QStringLiteral("security/projectTrust/%1").arg(key));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        errorMessage = QObject::tr("The project trust decision could not be cleared.");
        return false;
    }
    errorMessage.clear();
    return true;
}

} // namespace fgl::studio
