#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <performance_budget.h>

namespace fgl::studio {

struct ProjectInputBinding final {
    QString control;
    double scale = 1.0;
    double threshold = 0.5;

    friend bool operator==(const ProjectInputBinding&, const ProjectInputBinding&) = default;
};

struct ProjectInputValue final {
    QString name;
    QVector<ProjectInputBinding> bindings;

    friend bool operator==(const ProjectInputValue&, const ProjectInputValue&) = default;
};

struct ProjectInputContext final {
    QString name;
    int priority = 0;
    bool enabled = true;
    QVector<ProjectInputValue> actions;
    QVector<ProjectInputValue> axes;

    friend bool operator==(const ProjectInputContext&, const ProjectInputContext&) = default;
};

struct ProjectPackageDependency final {
    QString id;
    QString version;

    friend bool operator==(const ProjectPackageDependency&,
                           const ProjectPackageDependency&) = default;
};

struct ProjectAssetEntry final {
    QString guid;
    QString path;
    QString type;
    QString importSettings = QStringLiteral("{}");
    QString esp32Target = QStringLiteral("flash");
    QStringList dependencies;
    bool hasImportMetadata = false;

    friend bool operator==(const ProjectAssetEntry&, const ProjectAssetEntry&) = default;
};

struct ProjectTargetProfiles final {
    QString pc = QStringLiteral("pc.default");
    QString esp32 = QStringLiteral("olimex-esp32-sbc-fabgl-revb");

    friend bool operator==(const ProjectTargetProfiles&, const ProjectTargetProfiles&) = default;
};

struct ProjectData final {
    int sourceFormatVersion = 2;
    QString projectGuid;
    QString name;
    QString relativeRoot = QStringLiteral(".");
    QString sceneFile = QStringLiteral("Scenes/Main.fglscene");
    QString previewDemo;
    QString buildProgram = QStringLiteral("cmake");
    QStringList buildArguments = {QStringLiteral("--build"), QStringLiteral("out/build/dev")};
    QVector<ProjectAssetEntry> assets;
    QVector<ProjectInputContext> inputContexts;
    QVector<ProjectPackageDependency> packageDependencies;
    ProjectTargetProfiles targetProfiles;
    fabgl::project::PerformanceBudgetSettings performance;
};

class ProjectDocument final {
  public:
    static constexpr int FormatVersion = 2;

    [[nodiscard]] static bool load(const QString& filePath, ProjectData& data,
                                   QString& errorMessage);
    [[nodiscard]] static bool save(const QString& filePath, const ProjectData& data,
                                   QString& errorMessage);
    [[nodiscard]] static QByteArray serialized(const ProjectData& data, QString& errorMessage);
    [[nodiscard]] static QString absoluteProjectRoot(const QString& filePath,
                                                     const QString& relativeRoot);
    [[nodiscard]] static QString absoluteScenePath(const QString& projectFilePath,
                                                   const ProjectData& data);

  private:
    ProjectDocument() = delete;
};

} // namespace fgl::studio
