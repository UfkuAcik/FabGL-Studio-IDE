#include "PreviewRestartController.h"

#include <QtTest>

namespace {

using fgl::studio::PreviewKind;
using fgl::studio::PreviewRestartAction;
using fgl::studio::PreviewRestartController;

class PreviewRestartControllerTests final : public QObject {
    Q_OBJECT

  private slots:
    void restartsPlayingStudioPreview();
    void restoresPausedStudioPreview();
    void waitsForExternalPlayerShutdown();
    void coalescesSavesDuringBuildIntoOneExtraBuild();
    void failureClearsPendingRestart();
    void ignoresChangesWithoutAnActivePreview();
};

void PreviewRestartControllerTests::restartsPlayingStudioPreview() {
    PreviewRestartController controller;

    QCOMPARE(controller.request(PreviewKind::StudioPlaying), PreviewRestartAction::StopStudio);
    QCOMPARE(controller.phase(), PreviewRestartController::Phase::StoppingPreview);
    QCOMPARE(controller.previewStopped(), PreviewRestartAction::BuildPc);
    QCOMPARE(controller.phase(), PreviewRestartController::Phase::Building);
    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::StartStudioPlaying);
    QCOMPARE(controller.phase(), PreviewRestartController::Phase::Idle);
    QVERIFY(!controller.pending());
}

void PreviewRestartControllerTests::restoresPausedStudioPreview() {
    PreviewRestartController controller;

    QCOMPARE(controller.request(PreviewKind::StudioPaused), PreviewRestartAction::StopStudio);
    QCOMPARE(controller.previewStopped(), PreviewRestartAction::BuildPc);
    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::StartStudioPaused);
    QCOMPARE(controller.target(), PreviewKind::None);
}

void PreviewRestartControllerTests::waitsForExternalPlayerShutdown() {
    PreviewRestartController controller;

    QCOMPARE(controller.request(PreviewKind::ExternalPlayer),
             PreviewRestartAction::StopExternalPlayer);
    QCOMPARE(controller.phase(), PreviewRestartController::Phase::StoppingPreview);
    QCOMPARE(controller.previewStopped(), PreviewRestartAction::BuildPc);
    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::StartExternalPlayer);
}

void PreviewRestartControllerTests::coalescesSavesDuringBuildIntoOneExtraBuild() {
    PreviewRestartController controller;

    QCOMPARE(controller.request(PreviewKind::StudioPlaying), PreviewRestartAction::StopStudio);
    QCOMPARE(controller.previewStopped(), PreviewRestartAction::BuildPc);

    QCOMPARE(controller.request(PreviewKind::None), PreviewRestartAction::None);
    QCOMPARE(controller.request(PreviewKind::None), PreviewRestartAction::None);
    QVERIFY(controller.rebuildDeferred());

    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::BuildPc);
    QVERIFY(!controller.rebuildDeferred());
    QCOMPARE(controller.request(PreviewKind::None), PreviewRestartAction::None);
    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::BuildPc);
    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::StartStudioPlaying);
    QVERIFY(!controller.pending());
}

void PreviewRestartControllerTests::failureClearsPendingRestart() {
    PreviewRestartController controller;

    QCOMPARE(controller.request(PreviewKind::ExternalPlayer),
             PreviewRestartAction::StopExternalPlayer);
    QCOMPARE(controller.previewStopped(), PreviewRestartAction::BuildPc);
    QCOMPARE(controller.request(PreviewKind::None), PreviewRestartAction::None);
    QCOMPARE(controller.buildFinished(false), PreviewRestartAction::ReportFailure);
    QCOMPARE(controller.phase(), PreviewRestartController::Phase::Idle);
    QCOMPARE(controller.target(), PreviewKind::None);
    QVERIFY(!controller.rebuildDeferred());
}

void PreviewRestartControllerTests::ignoresChangesWithoutAnActivePreview() {
    PreviewRestartController controller;

    QCOMPARE(controller.request(PreviewKind::None), PreviewRestartAction::None);
    QCOMPARE(controller.previewStopped(), PreviewRestartAction::None);
    QCOMPARE(controller.buildFinished(true), PreviewRestartAction::None);
    QVERIFY(!controller.pending());
}

} // namespace

QTEST_APPLESS_MAIN(PreviewRestartControllerTests)

#include "preview_restart_controller_tests.moc"
