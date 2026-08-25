#include "CodeEditor.h"

#include <QAction>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

namespace {

bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool createDirectoryLinkForTest(const QString& linkPath, const QString& targetPath) {
#ifdef Q_OS_WIN
    const QString nativeLink = QDir::toNativeSeparators(linkPath);
    const QString nativeTarget = QDir::toNativeSeparators(targetPath);
    if (nativeLink.contains(QLatin1Char('"')) || nativeTarget.contains(QLatin1Char('"'))) {
        return false;
    }
    QProcess process;
    process.setProgram(
        qEnvironmentVariable("COMSPEC", QStringLiteral("C:\\Windows\\System32\\cmd.exe")));
    process.setNativeArguments(
        QStringLiteral("/d /c mklink /J \"%1\" \"%2\"").arg(nativeLink, nativeTarget));
    process.start();
    const bool finished = process.waitForFinished(5'000);
    if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !QFileInfo(linkPath).exists()) {
        qWarning().noquote() << "junction fixture failed:" << process.errorString()
                             << process.readAllStandardOutput() << process.readAllStandardError();
        return false;
    }
    return true;
#else
    return QFile::link(targetPath, linkPath);
#endif
}

} // namespace

class CodeEditorSecurityTests final : public QObject {
    Q_OBJECT

  private slots:
    void languageServerControlsAreExposed();
    void projectRootConfinesOpenAndAtomicSave();
};

void CodeEditorSecurityTests::languageServerControlsAreExposed() {
    fgl::studio::CodeEditorWidget widget;
    const QStringList buttonNames = {
        QStringLiteral("codeSaveAsButton"),       QStringLiteral("clangdCompletionButton"),
        QStringLiteral("clangdDefinitionButton"), QStringLiteral("clangdReferencesButton"),
        QStringLiteral("clangdHoverButton"),      QStringLiteral("clangdRenameButton"),
        QStringLiteral("clangdFormatButton")};
    for (const QString& name : buttonNames) {
        QVERIFY2(widget.findChild<QToolButton*>(name) != nullptr, qPrintable(name));
    }
    const auto* status = widget.findChild<QLabel*>(QStringLiteral("clangdStatusLabel"));
    QVERIFY(status != nullptr);
    QVERIFY(status->text().contains(QStringLiteral("clangd")));

    QList<QKeySequence> shortcuts;
    const QList<QAction*> actions = widget.actions();
    shortcuts.reserve(actions.size());
    for (const QAction* action : actions) {
        shortcuts.push_back(action->shortcut());
    }
    const QList<QKeySequence> requiredShortcuts = {QKeySequence::SaveAs,
                                                   QKeySequence(Qt::CTRL | Qt::Key_Space),
                                                   QKeySequence(Qt::Key_F12),
                                                   QKeySequence(Qt::SHIFT | Qt::Key_F12),
                                                   QKeySequence(Qt::CTRL | Qt::Key_I),
                                                   QKeySequence(Qt::Key_F2),
                                                   QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F)};
    for (const QKeySequence& shortcut : requiredShortcuts) {
        QVERIFY2(shortcuts.contains(shortcut), qPrintable(shortcut.toString()));
    }
}

void CodeEditorSecurityTests::projectRootConfinesOpenAndAtomicSave() {
    QTemporaryDir project;
    QTemporaryDir outside;
    QVERIFY(project.isValid());
    QVERIFY(outside.isValid());
    const QString outsideFile = outside.filePath(QStringLiteral("outside.cpp"));
    QVERIFY(writeFile(outsideFile, QByteArrayLiteral("outside-original\n")));

    fgl::studio::CodeEditorWidget widget;
    widget.clangdClient()->configureServer(
        project.filePath(QStringLiteral("deliberately-missing-clangd")));
    widget.setProjectRoot(project.path());
    QCOMPARE(widget.projectRoot(), QFileInfo(project.path()).canonicalFilePath());

    auto* editor = widget.findChild<fgl::studio::CodeTextEdit*>();
    QVERIFY(editor != nullptr);
    editor->setPlainText(QStringLiteral("int in_project = 1;\n"));
    const QString insideFile = project.filePath(QStringLiteral("source.cpp"));
    QSignalSpy savedSpy(&widget, &fgl::studio::CodeEditorWidget::fileSaved);
    QString error;
    QVERIFY2(widget.saveCurrentFileAs(insideFile, error), qPrintable(error));
    QCOMPARE(readFile(insideFile), QByteArrayLiteral("int in_project = 1;\n"));
    QCOMPARE(savedSpy.size(), 1);
    QCOMPARE(QFileInfo(savedSpy.constFirst().constFirst().toString()).absoluteFilePath(),
             QFileInfo(insideFile).absoluteFilePath());

    editor->setPlainText(QStringLiteral("int must_not_escape = 2;\n"));
    QVERIFY(!widget.saveCurrentFileAs(outsideFile, error));
    QCOMPARE(savedSpy.size(), 1);
    QVERIFY(error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
    QCOMPARE(readFile(outsideFile), QByteArrayLiteral("outside-original\n"));

    const QString missingParentFile = project.filePath(QStringLiteral("missing/subdirectory.cpp"));
    QVERIFY(!widget.saveCurrentFileAs(missingParentFile, error));
    QVERIFY(error.contains(QStringLiteral("missing"), Qt::CaseInsensitive) ||
            error.contains(QStringLiteral("canonical"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(missingParentFile));

    auto* tabs = widget.findChild<QTabWidget*>(QStringLiteral("codeEditorTabs"));
    QVERIFY(tabs != nullptr);
    const int tabCount = tabs->count();
    widget.openFile(outsideFile);
    QCOMPARE(tabs->count(), tabCount);

    const QString linkedDirectory = project.filePath(QStringLiteral("linked-outside"));
    QVERIFY2(createDirectoryLinkForTest(linkedDirectory, outside.path()),
             "The test fixture could not create a directory link/junction.");
    const QString escapedFile = QDir(linkedDirectory).filePath(QStringLiteral("escape.cpp"));
    QVERIFY(!widget.saveCurrentFileAs(escapedFile, error));
    QVERIFY(error.contains(QStringLiteral("symlink"), Qt::CaseInsensitive) ||
            error.contains(QStringLiteral("junction"), Qt::CaseInsensitive) ||
            error.contains(QStringLiteral("reparse"), Qt::CaseInsensitive) ||
            error.contains(QStringLiteral("resolves outside"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(outside.filePath(QStringLiteral("escape.cpp"))));
}

QTEST_MAIN(CodeEditorSecurityTests)

#include "code_editor_security_tests.moc"
