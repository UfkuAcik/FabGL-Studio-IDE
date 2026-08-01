#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FabGLStudio"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("fabgl-studio.org"));
    QCoreApplication::setApplicationName(QStringLiteral("FabGLStudio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr("FabGL Studio game editor for ESP32-SBC-FabGL projects"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QObject::tr(".fglproject file to open"),
                                 QStringLiteral("[project.fglproject]"));
    parser.process(application);

    fgl::studio::MainWindow window;
    window.show();

    const auto positionalArguments = parser.positionalArguments();
    if (!positionalArguments.isEmpty()) {
        const auto projectPath = positionalArguments.constFirst();
        QTimer::singleShot(0, &window,
                           [&window, projectPath]() { window.openProjectPath(projectPath); });
    }

    return application.exec();
}
