#include "MainWindow.h"
#include "PrefabEditorPanel.h"

#include <fabgl/scene/entity.h>
#include <fabgl/serialization/prefab_instance_serializer.h>
#include <fabgl/serialization/prefab_serializer.h>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUndoStack>
#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace {

fabgl::PrefabAsset readPrefab(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto bytes = file.readAll();
    const auto parsed = fabgl::PrefabSerializer::deserialize(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    return parsed ? parsed.value() : fabgl::PrefabAsset{};
}

bool writePrefab(const QString& path, const fabgl::PrefabAsset& prefab) {
    const auto serialized = fabgl::PrefabSerializer::serialize(prefab);
    if (!serialized || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const auto& text = serialized.value();
    return file.write(text.data(), static_cast<qint64>(text.size())) ==
           static_cast<qint64>(text.size());
}

struct SceneFixture final {
    fgl::studio::SceneDocument document;
    fabgl::EntityGuid root;
    fabgl::EntityGuid child;
    fabgl::ComponentTypeGuid health;

    SceneFixture() {
        auto rootEntity = document.scene().createEntity("Hero");
        Q_ASSERT(rootEntity);
        root = rootEntity.value()->id();
        rootEntity.value()->transform().setLocalPosition({2.0F, 3.0F, 4.0F});
        auto childEntity = document.scene().createEntity("Weapon");
        Q_ASSERT(childEntity);
        child = childEntity.value()->id();
        childEntity.value()->transform().setLocalPosition({5.0F, 0.0F, 0.0F});
        const auto parented = document.scene().setParent(child, root);
        if (!parented) {
            qFatal("Could not parent prefab test fixture entity");
        }
        QString error;
        const bool added = document.addBuiltinComponent(root, QStringLiteral("Health"), error);
        if (!added) {
            qFatal("Could not add prefab test fixture component");
        }
        const auto* metadata = document.reflectionRegistry().find("fabgl.Health");
        Q_ASSERT(metadata != nullptr);
        health = metadata->typeId;
        const bool propertySet = document.setComponentProperty(
            root, health, "current", fabgl::PropertyValue(std::int64_t{73}), error);
        if (!propertySet) {
            qFatal("Could not set prefab test fixture property");
        }
    }
};

void configurePanel(fgl::studio::PrefabEditorPanel& panel, const QString& root,
                    const fabgl::EntityGuid selected,
                    const QVector<fgl::studio::ProjectAssetEntry>& assets = {}) {
    panel.setProjectContext(root, QStringLiteral("11111111-2222-4333-8444-555555555555"), assets);
    panel.setSelectedEntity(selected);
    panel.resize(900, 1100);
    panel.show();
    QCoreApplication::processEvents();
}

QPushButton* button(QWidget& widget, const char* name) {
    auto* result = widget.findChild<QPushButton*>(QString::fromLatin1(name));
    Q_ASSERT(result != nullptr);
    return result;
}

QLineEdit* lineEdit(QWidget& widget, const char* name) {
    auto* result = widget.findChild<QLineEdit*>(QString::fromLatin1(name));
    Q_ASSERT(result != nullptr);
    return result;
}

QComboBox* combo(QWidget& widget, const char* name) {
    auto* result = widget.findChild<QComboBox*>(QString::fromLatin1(name));
    Q_ASSERT(result != nullptr);
    return result;
}

} // namespace

class PrefabEditorTests final : public QObject {
    Q_OBJECT

  private slots:
    void createSubtreeIsStableAtomicAndUndoable();
    void instantiateFreshHierarchyAndAuthorOverrides();
    void explicitResolverShowsNestedAndMissingDiagnostics();
    void persistedLinkageSurvivesSceneAndPanelReopen();
    void persistedMissingPlaceholderRemainsVisibleAfterReopen();
    void animationShowcaseUsesRealBakedLinkedPrefab();
    void changingProjectClearsOpenPrefabAndTrackedInstances();
    void mainWindowWiresDockSelectionAndProjectMapping();
};

void PrefabEditorTests::createSubtreeIsStableAtomicAndUndoable() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Assets")));
    SceneFixture scene;
    QUndoStack history;
    fgl::studio::PrefabEditorPanel panel(&scene.document, &history);
    configurePanel(panel, project.path(), scene.root);
    lineEdit(panel, "prefabPathEdit")->setText(QStringLiteral("Assets/Hero.fglprefab"));
    lineEdit(panel, "prefabNameEdit")->setText(QStringLiteral("Hero"));

    QVector<fgl::studio::ProjectAssetEntry> mappedAssets;
    connect(&panel, &fgl::studio::PrefabEditorPanel::projectAssetsChanged, &panel,
            [&mappedAssets](const auto& assets) { mappedAssets = assets; });
    QTest::mouseClick(button(panel, "prefabCreateButton"), Qt::LeftButton);

    const auto path = QDir(project.path()).filePath(QStringLiteral("Assets/Hero.fglprefab"));
    QVERIFY2(QFileInfo::exists(path), qPrintable(panel.diagnosticText()));
    QFile raw(path);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const auto firstBytes = raw.readAll();
    raw.close();
    QVERIFY(firstBytes.startsWith("fglprefab 2\n"));
    const auto prefab = readPrefab(path);
    QVERIFY(!prefab.id.isNil());
    QCOMPARE(QString::fromStdString(prefab.name), QStringLiteral("Hero"));
    QCOMPARE(prefab.entities.size(), std::size_t{2});
    const auto rootEntity = std::find_if(
        prefab.entities.cbegin(), prefab.entities.cend(),
        [](const fabgl::PrefabEntityData& entity) { return !entity.parent.has_value(); });
    QVERIFY(rootEntity != prefab.entities.cend());
    QCOMPARE(rootEntity->id, scene.root);
    const auto childEntity = std::find_if(
        prefab.entities.cbegin(), prefab.entities.cend(),
        [](const fabgl::PrefabEntityData& entity) { return entity.parent.has_value(); });
    QVERIFY(childEntity != prefab.entities.cend());
    QCOMPARE(childEntity->parent, std::optional<fabgl::EntityGuid>(scene.root));
    QVERIFY(prefab.components.contains(scene.health));
    QCOMPARE(mappedAssets.size(), qsizetype{1});
    QCOMPARE(mappedAssets.front().guid, QString::fromStdString(prefab.id.toString()));
    QCOMPARE(mappedAssets.front().type, QStringLiteral("prefab"));

    auto* undoButton = button(panel, "prefabUndoButton");
    QVERIFY(undoButton->isEnabled());
    QTest::mouseClick(undoButton, Qt::LeftButton);
    QCOMPARE(history.index(), 0);
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(mappedAssets.isEmpty());
    QTest::mouseClick(button(panel, "prefabRedoButton"), Qt::LeftButton);
    QVERIFY(QFileInfo::exists(path));
    QFile redone(path);
    QVERIFY(redone.open(QIODevice::ReadOnly));
    QCOMPARE(redone.readAll(), firstBytes);
    redone.close();
    QCOMPARE(panel.currentPrefabGuid(), QString::fromStdString(prefab.id.toString()));

    lineEdit(panel, "prefabNameEdit")->setText(QStringLiteral("Hero Updated"));
    QTest::mouseClick(button(panel, "prefabSaveButton"), Qt::LeftButton);
    QCOMPARE(QString::fromStdString(readPrefab(path).name), QStringLiteral("Hero Updated"));
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    QCOMPARE(QString::fromStdString(readPrefab(path).name), QStringLiteral("Hero"));
    QTest::mouseClick(button(panel, "prefabRedoButton"), Qt::LeftButton);
    QCOMPARE(QString::fromStdString(readPrefab(path).name), QStringLiteral("Hero Updated"));
}

void PrefabEditorTests::instantiateFreshHierarchyAndAuthorOverrides() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    SceneFixture scene;
    QUndoStack history;
    fgl::studio::PrefabEditorPanel panel(&scene.document, &history);
    configurePanel(panel, project.path(), scene.root);
    lineEdit(panel, "prefabPathEdit")->setText(QStringLiteral("Assets/Hero.fglprefab"));
    lineEdit(panel, "prefabNameEdit")->setText(QStringLiteral("Hero"));
    QTest::mouseClick(button(panel, "prefabCreateButton"), Qt::LeftButton);
    QVERIFY(panel.hasCurrentPrefab());

    QTest::mouseClick(button(panel, "prefabInstantiateButton"), Qt::LeftButton);
    QCOMPARE(panel.instanceCount(), qsizetype{1});
    const auto firstRoot = panel.selectedInstanceRoot();
    QVERIFY(firstRoot.has_value());
    QVERIFY(*firstRoot != scene.root);
    const auto* firstEntity = scene.document.scene().findEntity(*firstRoot);
    QVERIFY(firstEntity != nullptr);
    QCOMPARE(firstEntity->transform().children().size(), std::size_t{1});
    const auto firstChild = firstEntity->transform().children().front();
    QVERIFY(firstChild != scene.child);
    QCOMPARE(scene.document.scene().findEntity(firstChild)->transform().parent(), firstRoot);
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*firstRoot) == nullptr);
    QCOMPARE(panel.instanceCount(), qsizetype{0});
    QTest::mouseClick(button(panel, "prefabRedoButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*firstRoot) != nullptr);
    QCOMPARE(scene.document.scene().findEntity(firstChild)->transform().parent(), firstRoot);
    QCOMPARE(panel.instanceCount(), qsizetype{1});
    panel.setSelectedInstanceRoot(firstChild);
    QCOMPARE(panel.selectedInstanceRoot(), firstRoot);

    QTest::mouseClick(button(panel, "prefabInstantiateButton"), Qt::LeftButton);
    QCOMPARE(panel.instanceCount(), qsizetype{2});
    const auto secondRoot = panel.selectedInstanceRoot();
    QVERIFY(secondRoot.has_value());
    QVERIFY(*secondRoot != *firstRoot);

    auto* componentCombo = combo(panel, "prefabOverrideComponentCombo");
    const auto healthIndex = componentCombo->findText(QStringLiteral("fabgl.Health"));
    QVERIFY(healthIndex >= 0);
    componentCombo->setCurrentIndex(healthIndex);
    auto* propertyCombo = combo(panel, "prefabOverridePropertyCombo");
    const auto currentIndex = propertyCombo->findText(QStringLiteral("current"));
    QVERIFY(currentIndex >= 0);
    propertyCombo->setCurrentIndex(currentIndex);
    lineEdit(panel, "prefabOverrideValueEdit")->setText(QStringLiteral("25"));
    QTest::mouseClick(button(panel, "prefabSetPropertyOverrideButton"), Qt::LeftButton);
    QString error;
    auto current = scene.document.componentProperty(*secondRoot, scene.health, "current", error);
    QVERIFY2(current.has_value(), qPrintable(error));
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{25});
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    current = scene.document.componentProperty(*secondRoot, scene.health, "current", error);
    QVERIFY(current.has_value());
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{73});
    QTest::mouseClick(button(panel, "prefabRedoButton"), Qt::LeftButton);
    current = scene.document.componentProperty(*secondRoot, scene.health, "current", error);
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{25});

    componentCombo = combo(panel, "prefabOverrideComponentCombo");
    componentCombo->setCurrentIndex(componentCombo->findText(QStringLiteral("fabgl.Health")));
    QTest::mouseClick(button(panel, "prefabRemoveComponentOverrideButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*secondRoot)->getComponent(scene.health) == nullptr);
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*secondRoot)->getComponent(scene.health) != nullptr);
    current = scene.document.componentProperty(*secondRoot, scene.health, "current", error);
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{25});

    auto* addCombo = combo(panel, "prefabAddComponentCombo");
    addCombo->setCurrentIndex(addCombo->findText(QStringLiteral("Light")));
    QTest::mouseClick(button(panel, "prefabAddComponentOverrideButton"), Qt::LeftButton);
    const auto* lightMetadata = scene.document.reflectionRegistry().find("fabgl.Light");
    QVERIFY(lightMetadata != nullptr);
    QVERIFY(scene.document.scene().findEntity(*secondRoot)->getComponent(lightMetadata->typeId) !=
            nullptr);
    componentCombo = combo(panel, "prefabOverrideComponentCombo");
    componentCombo->setCurrentIndex(componentCombo->findText(QStringLiteral("fabgl.Light")));
    QTest::mouseClick(button(panel, "prefabRemoveComponentOverrideButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*secondRoot)->getComponent(lightMetadata->typeId) ==
            nullptr);
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*secondRoot)->getComponent(lightMetadata->typeId) !=
            nullptr);

    QTest::mouseClick(button(panel, "prefabRevertButton"), Qt::LeftButton);
    current = scene.document.componentProperty(*secondRoot, scene.health, "current", error);
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{73});
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    current = scene.document.componentProperty(*secondRoot, scene.health, "current", error);
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{25});

    QTest::mouseClick(button(panel, "prefabApplyButton"), Qt::LeftButton);
    const auto asset = readPrefab(panel.currentPrefabPath());
    QCOMPARE(std::get<std::int64_t>(asset.components.at(scene.health).properties.at("current")),
             std::int64_t{25});
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    const auto priorAsset = readPrefab(panel.currentPrefabPath());
    QCOMPARE(
        std::get<std::int64_t>(priorAsset.components.at(scene.health).properties.at("current")),
        std::int64_t{73});
    QTest::mouseClick(button(panel, "prefabRedoButton"), Qt::LeftButton);

    QTest::mouseClick(button(panel, "prefabUnpackButton"), Qt::LeftButton);
    QVERIFY(!panel.selectedInstanceLinked());
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    QVERIFY(panel.selectedInstanceLinked());
}

void PrefabEditorTests::explicitResolverShowsNestedAndMissingDiagnostics() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    SceneFixture scene;
    const auto baseGuid = fabgl::AssetGuid::fromStableName("tests.prefab.base");
    const auto derivedGuid = fabgl::AssetGuid::fromStableName("tests.prefab.derived");
    fabgl::PrefabAsset base{baseGuid, "Base", std::nullopt, {}, {}};
    fabgl::PrefabAsset derived{derivedGuid, "Derived", baseGuid, {}, {}};
    const auto basePath = QDir(project.path()).filePath(QStringLiteral("Assets/Base.fglprefab"));
    const auto derivedPath =
        QDir(project.path()).filePath(QStringLiteral("Assets/Derived.fglprefab"));
    QVERIFY(writePrefab(basePath, base));
    QVERIFY(writePrefab(derivedPath, derived));
    QVector<fgl::studio::ProjectAssetEntry> assets{{QString::fromStdString(derivedGuid.toString()),
                                                    QStringLiteral("Assets/Derived.fglprefab"),
                                                    QStringLiteral("prefab"),
                                                    QStringLiteral("{}"),
                                                    QStringLiteral("flash"),
                                                    {},
                                                    false}};
    QUndoStack history;
    fgl::studio::PrefabEditorPanel panel(&scene.document, &history);
    configurePanel(panel, project.path(), scene.root, assets);
    lineEdit(panel, "prefabPathEdit")->setText(QStringLiteral("Assets/Derived.fglprefab"));
    QTest::mouseClick(button(panel, "prefabOpenButton"), Qt::LeftButton);
    QVERIFY(panel.hasCurrentPrefab());
    QVERIFY(panel.diagnosticText().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
    auto* dependencyTree = panel.findChild<QTreeWidget*>(QStringLiteral("prefabDependencyTree"));
    QVERIFY(dependencyTree != nullptr);
    QVERIFY(dependencyTree->topLevelItem(0)->child(0)->text(0).contains(QStringLiteral("Missing"),
                                                                        Qt::CaseInsensitive));
    QTest::mouseClick(button(panel, "prefabInstantiateButton"), Qt::LeftButton);
    const auto placeholder = panel.selectedInstanceRoot();
    QVERIFY(placeholder.has_value());
    QVERIFY(QString::fromStdString(scene.document.scene().findEntity(*placeholder)->name())
                .startsWith(QStringLiteral("[Missing Prefab]")));
    QTest::mouseClick(button(panel, "prefabUndoButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*placeholder) == nullptr);
    QTest::mouseClick(button(panel, "prefabRedoButton"), Qt::LeftButton);
    QVERIFY(scene.document.scene().findEntity(*placeholder) != nullptr);

    assets.push_back({QString::fromStdString(baseGuid.toString()),
                      QStringLiteral("Assets/Base.fglprefab"),
                      QStringLiteral("prefab"),
                      QStringLiteral("{}"),
                      QStringLiteral("flash"),
                      {},
                      false});
    panel.setProjectContext(project.path(), QStringLiteral("11111111-2222-4333-8444-555555555555"),
                            assets);
    QString error;
    QVERIFY2(panel.openPrefabGuid(derivedGuid, error), qPrintable(error));
    QCOMPARE(panel.dependencyCount(), qsizetype{1});
    QVERIFY(panel.diagnosticText().contains(QStringLiteral("resolved"), Qt::CaseInsensitive));

    base.nestedBase = derivedGuid;
    QVERIFY(writePrefab(basePath, base));
    QVERIFY(panel.openPrefabGuid(derivedGuid, error));
    QVERIFY(panel.diagnosticText().contains(QStringLiteral("cycle"), Qt::CaseInsensitive));

    const auto unknown = fabgl::AssetGuid::fromStableName("tests.prefab.unknown");
    QVERIFY(!panel.openPrefabGuid(unknown, error));
    QVERIFY(panel.diagnosticText().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
    QTest::mouseClick(button(panel, "prefabInstantiateButton"), Qt::LeftButton);
    const auto missingRoot = panel.selectedInstanceRoot();
    QVERIFY(missingRoot.has_value());
    QVERIFY(QString::fromStdString(scene.document.scene().findEntity(*missingRoot)->name())
                .contains(QString::fromStdString(unknown.toString())));
}

void PrefabEditorTests::persistedLinkageSurvivesSceneAndPanelReopen() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Scenes")));
    SceneFixture scene;
    QString error;
    QVERIFY2(
        scene.document.addBuiltinComponent(scene.root, QStringLiteral("DamageReceiver"), error),
        qPrintable(error));
    const auto* damageMetadata = scene.document.reflectionRegistry().find("fabgl.DamageReceiver");
    const auto* lightMetadata = scene.document.reflectionRegistry().find("fabgl.Light");
    QVERIFY(damageMetadata != nullptr && lightMetadata != nullptr);
    QUndoStack firstHistory;
    fgl::studio::PrefabEditorPanel firstPanel(&scene.document, &firstHistory);
    configurePanel(firstPanel, project.path(), scene.root);
    QVector<fgl::studio::ProjectAssetEntry> assets;
    connect(&firstPanel, &fgl::studio::PrefabEditorPanel::projectAssetsChanged, &firstPanel,
            [&assets](const auto& updated) { assets = updated; });
    lineEdit(firstPanel, "prefabPathEdit")->setText(QStringLiteral("Assets/Hero.fglprefab"));
    lineEdit(firstPanel, "prefabNameEdit")->setText(QStringLiteral("Hero"));
    QTest::mouseClick(button(firstPanel, "prefabCreateButton"), Qt::LeftButton);
    QCOMPARE(assets.size(), qsizetype{1});
    QTest::mouseClick(button(firstPanel, "prefabInstantiateButton"), Qt::LeftButton);
    const auto linkedRoot = firstPanel.selectedInstanceRoot();
    QVERIFY(linkedRoot.has_value());

    auto* componentCombo = combo(firstPanel, "prefabOverrideComponentCombo");
    componentCombo->setCurrentIndex(componentCombo->findText(QStringLiteral("fabgl.Health")));
    auto* propertyCombo = combo(firstPanel, "prefabOverridePropertyCombo");
    propertyCombo->setCurrentIndex(propertyCombo->findText(QStringLiteral("current")));
    lineEdit(firstPanel, "prefabOverrideValueEdit")->setText(QStringLiteral("25"));
    QTest::mouseClick(button(firstPanel, "prefabSetPropertyOverrideButton"), Qt::LeftButton);
    auto* addCombo = combo(firstPanel, "prefabAddComponentCombo");
    addCombo->setCurrentIndex(addCombo->findText(QStringLiteral("Light")));
    QTest::mouseClick(button(firstPanel, "prefabAddComponentOverrideButton"), Qt::LeftButton);
    componentCombo = combo(firstPanel, "prefabOverrideComponentCombo");
    componentCombo->setCurrentIndex(
        componentCombo->findText(QStringLiteral("fabgl.DamageReceiver")));
    QTest::mouseClick(button(firstPanel, "prefabRemoveComponentOverrideButton"), Qt::LeftButton);

    const auto scenePath = QDir(project.path()).filePath(QStringLiteral("Scenes/Main.fglscene"));
    QVERIFY2(scene.document.saveAs(scenePath, error), qPrintable(error));
    QFile persisted(scenePath);
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    const auto persistedBytes = persisted.readAll();
    persisted.close();
    QVERIFY(persistedBytes.contains("type_name \"fabgl.PrefabInstanceLink\""));
    QVERIFY(persistedBytes.contains("fglprefabinstance 1"));

    fgl::studio::SceneDocument reopened;
    QVERIFY2(reopened.load(scenePath, error), qPrintable(error));
    QUndoStack reopenedHistory;
    fgl::studio::PrefabEditorPanel reopenedPanel(&reopened, &reopenedHistory);
    configurePanel(reopenedPanel, project.path(), *linkedRoot, assets);
    QCOMPARE(reopenedPanel.instanceCount(), qsizetype{1});
    reopenedPanel.setSelectedInstanceRoot(*linkedRoot);
    QVERIFY(reopenedPanel.selectedInstanceLinked());
    QVERIFY(reopenedPanel.hasCurrentPrefab());
    auto current = reopened.componentProperty(*linkedRoot, scene.health, "current", error);
    QVERIFY2(current.has_value(), qPrintable(error));
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{25});
    QVERIFY(reopened.scene().findEntity(*linkedRoot)->getComponent(lightMetadata->typeId) !=
            nullptr);
    QVERIFY(reopened.scene().findEntity(*linkedRoot)->getComponent(damageMetadata->typeId) ==
            nullptr);
    const auto linkType =
        fabgl::ComponentTypeGuid::fromStableName("fabgl.component.PrefabInstanceLink.v1");
    const auto* persistedLink = reopened.scene().findEntity(*linkedRoot)->getComponent(linkType);
    QVERIFY(persistedLink != nullptr && persistedLink->metadata() != nullptr);
    const auto encoded = persistedLink->metadata()->findProperty("state")->read(persistedLink);
    QVERIFY(encoded && std::holds_alternative<std::string>(encoded.value()));
    const auto decoded =
        fabgl::PrefabInstanceSerializer::deserialize(std::get<std::string>(encoded.value()));
    QVERIFY(decoded);
    QCOMPARE(decoded.value().state.propertyOverrideCount(), std::size_t{1});
    QCOMPARE(decoded.value().state.addedComponentCount(), std::size_t{1});
    QCOMPARE(decoded.value().state.removedComponentCount(), std::size_t{1});

    QTest::mouseClick(button(reopenedPanel, "prefabRevertButton"), Qt::LeftButton);
    current = reopened.componentProperty(*linkedRoot, scene.health, "current", error);
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{73});
    QVERIFY(reopened.scene().findEntity(*linkedRoot)->getComponent(lightMetadata->typeId) ==
            nullptr);
    QVERIFY(reopened.scene().findEntity(*linkedRoot)->getComponent(damageMetadata->typeId) !=
            nullptr);
    QTest::mouseClick(button(reopenedPanel, "prefabUndoButton"), Qt::LeftButton);
    current = reopened.componentProperty(*linkedRoot, scene.health, "current", error);
    QCOMPARE(std::get<std::int64_t>(*current), std::int64_t{25});
    QTest::mouseClick(button(reopenedPanel, "prefabApplyButton"), Qt::LeftButton);
    const auto applied = readPrefab(reopenedPanel.currentPrefabPath());
    QCOMPARE(std::get<std::int64_t>(applied.components.at(scene.health).properties.at("current")),
             std::int64_t{25});
    QVERIFY(applied.components.contains(lightMetadata->typeId));
    QVERIFY(!applied.components.contains(damageMetadata->typeId));

    QTest::mouseClick(button(reopenedPanel, "prefabUnpackButton"), Qt::LeftButton);
    QCOMPARE(reopenedPanel.instanceCount(), qsizetype{0});
    QVERIFY(!reopenedPanel.selectedInstanceLinked());
    QVERIFY(reopened.scene().findEntity(*linkedRoot)->getComponent(linkType) == nullptr);
    QVERIFY2(reopened.save(error), qPrintable(error));

    fgl::studio::SceneDocument unpackedScene;
    QVERIFY2(unpackedScene.load(scenePath, error), qPrintable(error));
    QUndoStack unpackedHistory;
    fgl::studio::PrefabEditorPanel unpackedPanel(&unpackedScene, &unpackedHistory);
    configurePanel(unpackedPanel, project.path(), *linkedRoot, assets);
    QCOMPARE(unpackedPanel.instanceCount(), qsizetype{0});
    QVERIFY(unpackedScene.scene().findEntity(*linkedRoot) != nullptr);
}

void PrefabEditorTests::persistedMissingPlaceholderRemainsVisibleAfterReopen() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Scenes")));
    SceneFixture scene;
    QUndoStack firstHistory;
    fgl::studio::PrefabEditorPanel firstPanel(&scene.document, &firstHistory);
    configurePanel(firstPanel, project.path(), scene.root);
    const auto missing = fabgl::AssetGuid::fromStableName("tests.prefab.persisted-missing");
    QString error;
    QVERIFY(!firstPanel.openPrefabGuid(missing, error));
    QTest::mouseClick(button(firstPanel, "prefabInstantiateButton"), Qt::LeftButton);
    const auto placeholder = firstPanel.selectedInstanceRoot();
    QVERIFY(placeholder.has_value());
    const auto scenePath = QDir(project.path()).filePath(QStringLiteral("Scenes/Missing.fglscene"));
    QVERIFY2(scene.document.saveAs(scenePath, error), qPrintable(error));

    fgl::studio::SceneDocument reopened;
    QVERIFY2(reopened.load(scenePath, error), qPrintable(error));
    QUndoStack reopenedHistory;
    fgl::studio::PrefabEditorPanel reopenedPanel(&reopened, &reopenedHistory);
    configurePanel(reopenedPanel, project.path(), *placeholder);
    QCOMPARE(reopenedPanel.instanceCount(), qsizetype{1});
    reopenedPanel.setSelectedInstanceRoot(*placeholder);
    QVERIFY(!reopenedPanel.selectedInstanceLinked());
    QVERIFY(
        reopenedPanel.diagnosticText().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
    const auto* entity = reopened.scene().findEntity(*placeholder);
    QVERIFY(entity != nullptr);
    QVERIFY(QString::fromStdString(entity->name()).startsWith(QStringLiteral("[Missing Prefab]")));
    QVERIFY(!button(reopenedPanel, "prefabUnpackButton")->isEnabled());
}

void PrefabEditorTests::animationShowcaseUsesRealBakedLinkedPrefab() {
    const QString root = QDir::cleanPath(QString::fromUtf8(FGL_TEST_REPOSITORY_ROOT) +
                                         QStringLiteral("/examples/animation_showcase"));
    const auto manifestPath = QDir(root).filePath(QStringLiteral("AnimationShowcase.fglproject"));
    fgl::studio::ProjectData project;
    QString error;
    QVERIFY2(fgl::studio::ProjectDocument::load(manifestPath, project, error), qPrintable(error));
    const auto prefabEntry =
        std::find_if(project.assets.cbegin(), project.assets.cend(), [](const auto& asset) {
            return asset.guid == QStringLiteral("54000000-0000-4000-8000-000000000009") &&
                   asset.type == QStringLiteral("prefab");
        });
    QVERIFY(prefabEntry != project.assets.cend());
    QCOMPARE(prefabEntry->path, QStringLiteral("Assets/AnimatedCharacter.fglprefab"));

    fgl::studio::SceneDocument scene;
    const auto scenePath = QDir(root).filePath(project.sceneFile);
    QVERIFY2(scene.load(scenePath, error), qPrintable(error));
    QUndoStack history;
    fgl::studio::PrefabEditorPanel panel(&scene, &history);
    panel.setProjectContext(root, project.projectGuid, project.assets);
    QCOMPARE(panel.instanceCount(), qsizetype{1});
    const auto linkedRoot = fabgl::EntityGuid::parse("30000000-0000-4000-8000-000000000009");
    QVERIFY(linkedRoot);
    panel.setSelectedInstanceRoot(linkedRoot.value());
    QVERIFY(panel.selectedInstanceLinked());
    QCOMPARE(panel.currentPrefabGuid(), QStringLiteral("54000000-0000-4000-8000-000000000009"));

    const auto particleType =
        fabgl::ComponentTypeGuid::parse("cb1e1a1b-bd20-516d-9a6c-53ec2b9445d3");
    QVERIFY(particleType);
    auto rate = scene.componentProperty(linkedRoot.value(), particleType.value(), "rate", error);
    QVERIFY2(rate.has_value(), qPrintable(error));
    QCOMPARE(std::get<double>(*rate), 24.0);
    const auto linkType =
        fabgl::ComponentTypeGuid::fromStableName("fabgl.component.PrefabInstanceLink.v1");
    const auto* link = scene.scene().findEntity(linkedRoot.value())->getComponent(linkType);
    QVERIFY(link != nullptr && link->metadata() != nullptr);
    const auto encoded = link->metadata()->findProperty("state")->read(link);
    QVERIFY(encoded && std::holds_alternative<std::string>(encoded.value()));
    const auto decoded =
        fabgl::PrefabInstanceSerializer::deserialize(std::get<std::string>(encoded.value()));
    QVERIFY(decoded);
    QCOMPARE(decoded.value().entities.size(), std::size_t{2});
    QCOMPARE(decoded.value().sourceToScene.size(), std::size_t{2});
    QCOMPARE(decoded.value().state.propertyOverrideCount(), std::size_t{1});

    const auto prefab = readPrefab(QDir(root).filePath(prefabEntry->path));
    QCOMPARE(prefab.entities.size(), std::size_t{2});
    QCOMPARE(std::get<double>(prefab.components.at(particleType.value()).properties.at("rate")),
             18.0);
    QTest::mouseClick(button(panel, "prefabRevertButton"), Qt::LeftButton);
    rate = scene.componentProperty(linkedRoot.value(), particleType.value(), "rate", error);
    QCOMPARE(std::get<double>(*rate), 18.0);
    QTest::mouseClick(button(panel, "prefabUnpackButton"), Qt::LeftButton);
    QCOMPARE(panel.instanceCount(), qsizetype{0});
    QVERIFY(scene.scene().findEntity(linkedRoot.value())->getComponent(linkType) == nullptr);
}

void PrefabEditorTests::changingProjectClearsOpenPrefabAndTrackedInstances() {
    QTemporaryDir firstProject;
    QTemporaryDir secondProject;
    QVERIFY(firstProject.isValid());
    QVERIFY(secondProject.isValid());
    SceneFixture scene;
    QUndoStack history;
    fgl::studio::PrefabEditorPanel panel(&scene.document, &history);
    configurePanel(panel, firstProject.path(), scene.root);
    lineEdit(panel, "prefabPathEdit")->setText(QStringLiteral("Assets/Hero.fglprefab"));
    lineEdit(panel, "prefabNameEdit")->setText(QStringLiteral("Hero"));
    QTest::mouseClick(button(panel, "prefabCreateButton"), Qt::LeftButton);
    QTest::mouseClick(button(panel, "prefabInstantiateButton"), Qt::LeftButton);
    QVERIFY(panel.hasCurrentPrefab());
    QCOMPARE(panel.instanceCount(), qsizetype{1});

    panel.setProjectContext(secondProject.path(),
                            QStringLiteral("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff"), {});
    QVERIFY(!panel.hasCurrentPrefab());
    QCOMPARE(panel.instanceCount(), qsizetype{0});
    QVERIFY(panel.currentPrefabGuid().isEmpty());
    QVERIFY(!button(panel, "prefabSaveButton")->isEnabled());
    const auto firstPrefabPath =
        QDir(firstProject.path()).filePath(QStringLiteral("Assets/Hero.fglprefab"));
    QVERIFY(QFileInfo::exists(firstPrefabPath));
    history.undo();
    QVERIFY(QFileInfo::exists(firstPrefabPath));
    QVERIFY(!panel.hasCurrentPrefab());
}

void PrefabEditorTests::mainWindowWiresDockSelectionAndProjectMapping() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    QVERIFY(QDir(project.path()).mkpath(QStringLiteral("Scenes")));
    fgl::studio::SceneDocument sourceScene;
    auto entity = sourceScene.scene().createEntity("Prefab Source");
    QVERIFY(entity);
    QString error;
    const auto scenePath = QDir(project.path()).filePath(QStringLiteral("Scenes/Main.fglscene"));
    QVERIFY2(sourceScene.saveAs(scenePath, error), qPrintable(error));
    fgl::studio::ProjectData projectData;
    projectData.projectGuid = QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    projectData.name = QStringLiteral("Prefab UX Test");
    projectData.sceneFile = QStringLiteral("Scenes/Main.fglscene");
    const auto projectPath = QDir(project.path()).filePath(QStringLiteral("PrefabUx.fglproject"));
    QVERIFY2(fgl::studio::ProjectDocument::save(projectPath, projectData, error),
             qPrintable(error));

    fgl::studio::MainWindow window(nullptr, {.safeMode = true,
                                             .pluginsEnabled = false,
                                             .reopenLastProject = false,
                                             .interactiveRecovery = false,
                                             .recoveryRoot = project.path()});
    QVERIFY2(window.openProjectPath(projectPath, error), qPrintable(error));
    window.show();
    QCoreApplication::processEvents();
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("prefabEditorDock"));
    auto* action = window.findChild<QAction*>(QStringLiteral("showPrefabEditorAction"));
    auto* panel =
        window.findChild<fgl::studio::PrefabEditorPanel*>(QStringLiteral("prefabEditorPanel"));
    QVERIFY(dock != nullptr);
    QVERIFY(action != nullptr);
    QVERIFY(panel != nullptr);
    dock->hide();
    action->trigger();
    QCoreApplication::processEvents();
    QVERIFY(dock->isVisible());

    auto* hierarchy = window.findChild<QListView*>();
    QVERIFY(hierarchy != nullptr);
    QVERIFY(hierarchy->model()->rowCount() > 0);
    const auto sourceIndex = hierarchy->model()->index(0, 0);
    hierarchy->scrollTo(sourceIndex);
    QTest::mouseClick(hierarchy->viewport(), Qt::LeftButton, Qt::NoModifier,
                      hierarchy->visualRect(sourceIndex).center());
    QCoreApplication::processEvents();
    QVERIFY(button(*panel, "prefabCreateButton")->isEnabled());
    lineEdit(*panel, "prefabPathEdit")->setText(QStringLiteral("Assets/Integrated.fglprefab"));
    lineEdit(*panel, "prefabNameEdit")->setText(QStringLiteral("Integrated"));
    QTest::mouseClick(button(*panel, "prefabCreateButton"), Qt::LeftButton);
    QVERIFY(QFileInfo::exists(
        QDir(project.path()).filePath(QStringLiteral("Assets/Integrated.fglprefab"))));
    window.findChild<QAction*>(QStringLiteral("saveProjectAction"))->trigger();
    fgl::studio::ProjectData reloaded;
    QVERIFY2(fgl::studio::ProjectDocument::load(projectPath, reloaded, error), qPrintable(error));
    QCOMPARE(reloaded.assets.size(), qsizetype{1});
    QCOMPARE(reloaded.assets.front().path, QStringLiteral("Assets/Integrated.fglprefab"));
    QCOMPARE(reloaded.assets.front().guid, panel->currentPrefabGuid());
}

QTEST_MAIN(PrefabEditorTests)

#include "prefab_editor_tests.moc"
