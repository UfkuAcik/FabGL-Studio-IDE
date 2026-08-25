#include "ExtensionServicePanel.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>
#include <QLabel>

namespace {

fabgl::project::ProjectExtensionServiceState
service(const fabgl::PackageEntryPointKind kind, const std::string& id,
        const fabgl::project::ProjectExtensionServiceStateKind state =
            fabgl::project::ProjectExtensionServiceStateKind::Ready) {
    fabgl::project::ProjectExtensionServiceState value;
    value.service.extensionId = "test.extension/fixture";
    value.service.serviceId = id;
    value.service.displayName = "Fixture " + id;
    value.service.dispatchCapability = "fixture.dispatch";
    value.service.kind = kind;
    value.state = state;
    if (!value.enabled())
        value.lastError = "fixture failed";
    return value;
}

class ExtensionServicePanelTests final : public QObject {
    Q_OBJECT

  private slots:
    void listsServicesAndOnlyInvokesInteractiveReadyRows();
    void explainsExecutionDisabledState();
};

void ExtensionServicePanelTests::listsServicesAndOnlyInvokesInteractiveReadyRows() {
    fgl::studio::ExtensionServicePanel panel;
    const std::vector<fabgl::project::ProjectExtensionServiceState> services{
        service(fabgl::PackageEntryPointKind::CustomWindow, "window"),
        service(fabgl::PackageEntryPointKind::BuildStep, "build"),
        service(fabgl::PackageEntryPointKind::CustomInspector, "inspector",
                fabgl::project::ProjectExtensionServiceStateKind::Disabled),
    };
    panel.setServices(services, true);
    auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("extensionServiceTree"));
    auto* button =
        panel.findChild<QPushButton*>(QStringLiteral("extensionServiceInvokeButton"));
    QVERIFY(tree != nullptr);
    QVERIFY(button != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 3);

    QSignalSpy invoked(&panel, &fgl::studio::ExtensionServicePanel::serviceInvocationRequested);
    tree->setCurrentItem(tree->topLevelItem(0));
    QVERIFY(button->isEnabled());
    button->click();
    QCOMPARE(invoked.count(), 1);
    QCOMPARE(invoked.front().front().toString(),
             QStringLiteral("test.extension/fixture:window"));

    tree->setCurrentItem(tree->topLevelItem(1));
    QVERIFY(!button->isEnabled());
    tree->setCurrentItem(tree->topLevelItem(2));
    QVERIFY(!button->isEnabled());
}

void ExtensionServicePanelTests::explainsExecutionDisabledState() {
    fgl::studio::ExtensionServicePanel panel;
    panel.setServices({}, false);
    auto* summary =
        panel.findChild<QLabel*>(QStringLiteral("extensionServiceSummary"));
    QVERIFY(summary != nullptr);
    QVERIFY(summary->text().contains(QStringLiteral("disabled"), Qt::CaseInsensitive));
}

} // namespace

QTEST_MAIN(ExtensionServicePanelTests)

#include "extension_service_panel_tests.moc"
