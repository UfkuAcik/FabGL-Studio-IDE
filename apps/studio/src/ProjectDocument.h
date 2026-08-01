#pragma once

#include <QString>
#include <QStringList>

namespace fgl::studio {

struct ProjectData final {
    QString projectGuid;
    QString name;
    QString relativeRoot = QStringLiteral(".");
    QString sceneFile = QStringLiteral("Scenes/Main.fglscene");
    QString previewDemo;
    QString buildProgram = QStringLiteral("cmake");
    QStringList buildArguments = {QStringLiteral("--build"), QStringLiteral("out/build/dev")};
};

class ProjectDocument final {
  public:
    static constexpr int FormatVersion = 1;

    [[nodiscard]] static bool load(const QString& filePath, ProjectData& data,
                                   QString& errorMessage);
    [[nodiscard]] static bool save(const QString& filePath, const ProjectData& data,
                                   QString& errorMessage);
    [[nodiscard]] static QString absoluteProjectRoot(const QString& filePath,
                                                     const QString& relativeRoot);
    [[nodiscard]] static QString absoluteScenePath(const QString& projectFilePath,
                                                   const ProjectData& data);

  private:
    ProjectDocument() = delete;
};

} // namespace fgl::studio
