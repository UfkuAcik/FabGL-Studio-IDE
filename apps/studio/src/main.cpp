#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FabGLStudio"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("fabgl-studio.org"));
    QCoreApplication::setApplicationName(QStringLiteral("FabGLStudio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FABGL_STUDIO_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr("FabGL Studio game editor for ESP32-SBC-FabGL projects"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption safeModeOption(
        QStringLiteral("safe-mode"),
        QObject::tr("Start with plugins and automatic last-project reopening disabled."));
    const QCommandLineOption disablePluginsOption(
        QStringLiteral("disable-plugins"),
        QObject::tr("Disable all editor plugins for this session."));
    const QCommandLineOption noReopenOption(
        QStringLiteral("no-reopen-last-project"),
        QObject::tr("Do not reopen the last project when no project argument is provided."));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QObject::tr("Capture the real Studio window to a PNG after loading the optional project, "
                    "then exit."),
        QStringLiteral("output.png"));
    parser.addOption(safeModeOption);
    parser.addOption(disablePluginsOption);
    parser.addOption(noReopenOption);
    parser.addOption(screenshotOption);
    parser.addPositionalArgument(QStringLiteral("project"), QObject::tr(".fglproject file to open"),
                                 QStringLiteral("[project.fglproject]"));
    parser.process(application);

    fgl::studio::StudioLaunchOptions launchOptions;
    launchOptions.safeMode = parser.isSet(safeModeOption);
    launchOptions.pluginsEnabled = !launchOptions.safeMode && !parser.isSet(disablePluginsOption);
    launchOptions.reopenLastProject = !launchOptions.safeMode && !parser.isSet(noReopenOption);
    launchOptions.interactiveRecovery = !parser.isSet(screenshotOption);
    fgl::studio::MainWindow window(nullptr, launchOptions);

    const auto positionalArguments = parser.positionalArguments();
    if (parser.isSet(screenshotOption) && !positionalArguments.isEmpty()) {
        QString errorMessage;
        if (!window.openProjectPath(positionalArguments.constFirst(), errorMessage)) {
            QTextStream(stderr)
                << QObject::tr("Could not load screenshot project: %1\n").arg(errorMessage);
            return 2;
        }
    }
    window.show();

    if (parser.isSet(screenshotOption)) {
        const QString requestedPath = parser.value(screenshotOption);
        if (requestedPath.trimmed().isEmpty() ||
            !requestedPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
            QTextStream(stderr) << QObject::tr("--screenshot requires a .png output path.\n");
            return 2;
        }
        const QString outputPath = QFileInfo(requestedPath).absoluteFilePath();
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
            QTextStream(stderr) << QObject::tr("Could not create screenshot directory: %1\n")
                                       .arg(QFileInfo(outputPath).absolutePath());
            return 2;
        }
        QTimer::singleShot(350, &window, [&application, &window, outputPath]() {
            window.repaint();
            if (!window.grab().save(outputPath, "PNG")) {
                QTextStream(stderr)
                    << QObject::tr("Could not save Studio screenshot: %1\n").arg(outputPath);
                application.exit(2);
                return;
            }
            QTextStream(stdout) << QObject::tr("Saved Studio screenshot: %1\n").arg(outputPath);
            application.quit();
        });
    } else if (!positionalArguments.isEmpty()) {
        const auto projectPath = positionalArguments.constFirst();
        QTimer::singleShot(0, &window,
                           [&window, projectPath]() { window.openProjectPath(projectPath); });
    } else if (launchOptions.reopenLastProject) {
        const QString projectPath = window.lastProjectPath();
        if (!projectPath.isEmpty() && QFileInfo::exists(projectPath)) {
            QTimer::singleShot(0, &window,
                               [&window, projectPath]() { window.openProjectPath(projectPath); });
        }
    }

    return application.exec();
}
