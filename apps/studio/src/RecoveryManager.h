#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace fgl::studio {

struct RecoveryEntry final {
    QString id;
    QString filePath;
    QString projectPath;
    QString sceneRelativePath;
    QDateTime timestamp;
    QByteArray projectData;
    QByteArray sceneData;
    bool corrupt = false;
    QString errorMessage;
};

class RecoveryManager final {
  public:
    explicit RecoveryManager(QString storageRoot = {}, int maximumBackupsPerProject = 5);

    [[nodiscard]] QString storageRoot() const;
    [[nodiscard]] int maximumBackupsPerProject() const noexcept;
    [[nodiscard]] bool beginSession(QString& errorMessage);
    bool endSession(QString& errorMessage);
    [[nodiscard]] bool previousSessionWasUnclean() const noexcept;

    [[nodiscard]] bool writeAutosave(const QString& projectPath,
                                     const QString& sceneRelativePath,
                                     const QByteArray& projectData, const QByteArray& sceneData,
                                     QString& errorMessage);
    [[nodiscard]] QVector<RecoveryEntry> entries() const;
    [[nodiscard]] bool restore(const RecoveryEntry& entry,
                               const QString& destinationProjectPath,
                               QString& errorMessage) const;
    [[nodiscard]] bool discard(const RecoveryEntry& entry, QString& errorMessage) const;
    [[nodiscard]] bool discardProject(const QString& projectPath, QString& errorMessage) const;

    void recordLastProject(const QString& projectPath) const;
    [[nodiscard]] QString lastProjectPath() const;

  private:
    [[nodiscard]] RecoveryEntry readEntry(const QString& filePath) const;
    [[nodiscard]] QString sessionMarkerPath() const;
    [[nodiscard]] bool ensureStorage(QString& errorMessage) const;
    void rotateProjectBackups(const QString& normalizedProjectPath) const;
    [[nodiscard]] bool pathIsManaged(const QString& filePath) const;

    QString m_storageRoot;
    int m_maximumBackupsPerProject = 5;
    bool m_previousSessionWasUnclean = false;
};

} // namespace fgl::studio
