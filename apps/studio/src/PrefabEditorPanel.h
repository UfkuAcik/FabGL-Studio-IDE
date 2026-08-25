#pragma once

#include "ProjectDocument.h"
#include "SceneDocument.h"

#include <fabgl/prefab/prefab.h>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <map>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QUndoStack;

namespace fgl::studio {

class PrefabEditorPanel final : public QWidget {
    Q_OBJECT

  public:
    explicit PrefabEditorPanel(SceneDocument* document, QUndoStack* undoStack = nullptr,
                               QWidget* parent = nullptr);

    void setProjectContext(const QString& projectRoot, const QString& projectGuid,
                           const QVector<ProjectAssetEntry>& assets);
    void setSelectedEntity(std::optional<fabgl::EntityGuid> entity);
    void setSelectedInstanceRoot(std::optional<fabgl::EntityGuid> entity);

    [[nodiscard]] bool createFromSelection(const QString& projectRelativePath,
                                           const QString& prefabName,
                                           std::optional<fabgl::AssetGuid> nestedBase,
                                           QString& errorMessage);
    [[nodiscard]] bool openPrefabRelativePath(const QString& projectRelativePath,
                                              QString& errorMessage);
    [[nodiscard]] bool openPrefabGuid(fabgl::AssetGuid guid, QString& errorMessage);
    [[nodiscard]] bool saveCurrentPrefab(QString& errorMessage);
    [[nodiscard]] bool instantiateCurrentPrefab(QString& errorMessage);

    [[nodiscard]] bool hasCurrentPrefab() const noexcept;
    [[nodiscard]] QString currentPrefabPath() const;
    [[nodiscard]] QString currentPrefabGuid() const;
    [[nodiscard]] QString diagnosticText() const;
    [[nodiscard]] qsizetype instanceCount() const noexcept;
    [[nodiscard]] qsizetype dependencyCount() const noexcept;
    [[nodiscard]] bool selectedInstanceLinked() const noexcept;
    [[nodiscard]] std::optional<fabgl::EntityGuid> selectedInstanceRoot() const noexcept;

  signals:
    void projectAssetsChanged(const QVector<ProjectAssetEntry>& assets);
    void sceneSelectionRequested(const QStringList& entityGuids);
    void statusMessage(const QString& message);

  private:
    using InstanceRecord = fabgl::PrefabSceneInstance;

    struct FileSnapshot final {
        QString path;
        QByteArray bytes;
        bool existed = false;
    };

    struct StateSnapshot final {
        QString projectRoot;
        QString projectGuid;
        QByteArray scene;
        bool sceneModified = false;
        bool hasCurrent = false;
        fabgl::PrefabAsset current;
        QString currentPath;
        std::optional<fabgl::AssetGuid> missingGuid;
        QVector<ProjectAssetEntry> assets;
        std::vector<InstanceRecord> instances;
        std::optional<fabgl::EntityGuid> selectedInstance;
        std::vector<FileSnapshot> files;
    };

    void buildUi();
    void connectUi();
    void refreshUi();
    void refreshDependencyUi();
    void refreshInstanceUi();
    void refreshOverrideUi();
    void reportFailure(const QString& errorMessage);
    void reportSuccess(const QString& message);

    [[nodiscard]] std::optional<QString> safeProjectPath(const QString& relativePath,
                                                         QString& errorMessage) const;
    [[nodiscard]] std::optional<ProjectAssetEntry> assetEntry(fabgl::AssetGuid guid) const;
    [[nodiscard]] std::optional<ProjectAssetEntry> assetEntry(const QString& relativePath) const;
    [[nodiscard]] bool readPrefab(const QString& absolutePath, fabgl::PrefabAsset& prefab,
                                  QString& errorMessage) const;
    [[nodiscard]] bool writePrefab(const QString& absolutePath, const fabgl::PrefabAsset& prefab,
                                   QString& errorMessage) const;
    [[nodiscard]] bool rebuildLibrary(QString& errorMessage);
    void discoverSceneInstances();
    [[nodiscard]] bool persistInstanceLink(const InstanceRecord& record, QString& errorMessage);
    [[nodiscard]] bool removeInstanceLink(fabgl::EntityGuid root, QString& errorMessage);
    [[nodiscard]] std::optional<fabgl::ComponentTypeGuid> instanceLinkType() const;
    [[nodiscard]] bool buildPrefabFromSelection(const QString& prefabName,
                                                std::optional<fabgl::AssetGuid> nestedBase,
                                                fabgl::PrefabAsset& prefab,
                                                QString& errorMessage) const;
    [[nodiscard]] bool instantiateResolved(const fabgl::ResolvedPrefab& resolved,
                                           InstanceRecord& record, QString& errorMessage);
    [[nodiscard]] bool instantiateMissing(fabgl::AssetGuid missingGuid, QString& errorMessage);
    [[nodiscard]] bool applyResolvedRootComponents(InstanceRecord& record, QString& errorMessage);
    [[nodiscard]] bool setPropertyOverrideFromUi(QString& errorMessage);
    [[nodiscard]] bool addComponentOverrideFromUi(QString& errorMessage);
    [[nodiscard]] bool removeComponentOverrideFromUi(QString& errorMessage);
    [[nodiscard]] bool revertSelectedInstance(QString& errorMessage);
    [[nodiscard]] bool applySelectedInstance(QString& errorMessage);
    [[nodiscard]] bool unpackSelectedInstance(QString& errorMessage);
    [[nodiscard]] InstanceRecord* selectedInstanceRecord();
    [[nodiscard]] const InstanceRecord* selectedInstanceRecord() const;

    [[nodiscard]] StateSnapshot captureState(const std::vector<QString>& affectedFiles,
                                             QString& errorMessage) const;
    [[nodiscard]] bool restoreState(const StateSnapshot& state, QString& errorMessage);
    [[nodiscard]] bool runSnapshotEdit(const QString& description,
                                       const std::vector<QString>& affectedFiles,
                                       const std::function<bool(QString&)>& edit,
                                       QString& errorMessage);

    SceneDocument* m_document = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QString m_projectRoot;
    QString m_projectGuid;
    QVector<ProjectAssetEntry> m_projectAssets;
    std::optional<fabgl::EntityGuid> m_selectedEntity;
    bool m_hasCurrent = false;
    fabgl::PrefabAsset m_currentPrefab;
    QString m_currentPath;
    std::optional<fabgl::AssetGuid> m_missingGuid;
    fabgl::PrefabLibrary m_library;
    QString m_diagnostic;
    qsizetype m_dependencyCount = 0;
    std::vector<InstanceRecord> m_instances;
    std::optional<fabgl::EntityGuid> m_selectedInstance;
    bool m_restoring = false;

    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_guidEdit = nullptr;
    QLineEdit* m_nestedBaseEdit = nullptr;
    QPushButton* m_createButton = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_instantiateButton = nullptr;
    QTreeWidget* m_dependencyTree = nullptr;
    QLabel* m_diagnosticLabel = nullptr;
    QTreeWidget* m_instancesTree = nullptr;
    QComboBox* m_overrideComponentCombo = nullptr;
    QComboBox* m_overridePropertyCombo = nullptr;
    QLineEdit* m_overrideValueEdit = nullptr;
    QPushButton* m_setPropertyButton = nullptr;
    QComboBox* m_addComponentCombo = nullptr;
    QPushButton* m_addComponentButton = nullptr;
    QPushButton* m_removeComponentButton = nullptr;
    QPushButton* m_revertButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_unpackButton = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
};

} // namespace fgl::studio
