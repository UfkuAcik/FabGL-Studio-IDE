#include "ClangdClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <functional>

namespace {

QByteArray frame(const QJsonObject& object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return QByteArrayLiteral("Content-Length: ") + QByteArray::number(payload.size()) +
           QByteArrayLiteral("\r\n\r\n") + payload;
}

bool writeFile(const QString& path, const QByteArray& contents) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString fakeServerPath() {
#ifdef Q_OS_WIN
    constexpr auto ServerName = "fabgl_fake_lsp_server.exe";
#else
    constexpr auto ServerName = "fabgl_fake_lsp_server";
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(ServerName));
}

QJsonObject position(const int line, const int character) {
    return QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), character}};
}

QJsonObject range(const int startLine, const int startCharacter, const int endLine,
                  const int endCharacter) {
    return QJsonObject{{QStringLiteral("start"), position(startLine, startCharacter)},
                       {QStringLiteral("end"), position(endLine, endCharacter)}};
}

QJsonObject changesFor(const QString& path, const QJsonArray& edits) {
    QJsonObject changes;
    changes.insert(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), edits);
    return QJsonObject{{QStringLiteral("changes"), changes}};
}

bool waitUntil(const std::function<bool()>& predicate, const int timeoutMilliseconds = 5'000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(10);
    }
    return predicate();
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
    return process.waitForFinished(5'000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0 && QFileInfo(linkPath).exists();
#else
    return QFile::link(targetPath, linkPath);
#endif
}

} // namespace

class ClangdClientTests final : public QObject {
    Q_OBJECT

  private slots:
    void parserHandlesFragmentationAndBatching();
    void parserRejectsMalformedAndOversizedFrames();
    void fakeServerExercisesLifecycleAndFeatures();
    void workspaceEditsStayInsideCanonicalRoot();
    void missingServerDegradesSafely();
};

void ClangdClientTests::parserHandlesFragmentationAndBatching() {
    fgl::studio::JsonRpcMessageParser parser;
    const QJsonObject firstObject = {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                     {QStringLiteral("id"), 1},
                                     {QStringLiteral("result"), QStringLiteral("first")}};
    const QJsonObject secondObject = {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                      {QStringLiteral("method"), QStringLiteral("notice")}};
    const QByteArray firstFrame = frame(firstObject);
    const QByteArray secondFrame = frame(secondObject);

    const auto fragment = parser.feed(firstFrame.left(11));
    QVERIFY(fragment.ok());
    QVERIFY(fragment.messages.isEmpty());
    QVERIFY(parser.bufferedBytes() > 0);

    const auto completed = parser.feed(firstFrame.mid(11) + secondFrame);
    QVERIFY(completed.ok());
    QCOMPARE(completed.messages.size(), 2);
    QCOMPARE(completed.messages.at(0), firstObject);
    QCOMPARE(completed.messages.at(1), secondObject);
    QCOMPARE(parser.bufferedBytes(), qsizetype{0});
}

void ClangdClientTests::parserRejectsMalformedAndOversizedFrames() {
    fgl::studio::JsonRpcMessageParser parser;
    auto result = parser.feed(QByteArrayLiteral("X-Test: 1\r\n\r\n{}"));
    QVERIFY(!result.ok());
    QVERIFY(result.errorMessage.contains(QStringLiteral("Content-Length")));
    QCOMPARE(parser.bufferedBytes(), qsizetype{0});

    result = parser.feed(QByteArrayLiteral("Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}"));
    QVERIFY(!result.ok());
    QVERIFY(result.errorMessage.contains(QStringLiteral("duplicated")));

    result = parser.feed(QByteArrayLiteral("Content-Length: +2\r\n\r\n{}"));
    QVERIFY(!result.ok());
    QVERIFY(result.errorMessage.contains(QStringLiteral("non-digits")));

    const QByteArray oversized =
        QByteArrayLiteral("Content-Length: ") +
        QByteArray::number(fgl::studio::JsonRpcMessageParser::MaximumPayloadBytes + 1) +
        QByteArrayLiteral("\r\n\r\n");
    result = parser.feed(oversized);
    QVERIFY(!result.ok());
    QVERIFY(result.errorMessage.contains(QStringLiteral("too large")));

    result = parser.feed(QByteArrayLiteral("Content-Length: 4\r\n\r\nnull"));
    QVERIFY(!result.ok());
    QVERIFY(result.errorMessage.contains(QStringLiteral("JSON object")));
}

void ClangdClientTests::fakeServerExercisesLifecycleAndFeatures() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    const QString sourcePath = project.filePath(QStringLiteral("source.cpp"));
    const QString logPath = project.filePath(QStringLiteral("lsp.log"));
    QVERIFY(writeFile(sourcePath, QByteArrayLiteral("old value;\n")));
    QVERIFY2(QFileInfo::exists(fakeServerPath()), qPrintable(fakeServerPath()));

    fgl::studio::ClangdClient client;
    client.configureServer(fakeServerPath(), {QStringLiteral("--log"), logPath});
    QSignalSpy diagnosticsSpy(&client, &fgl::studio::ClangdClient::diagnosticsPublished);
    QSignalSpy completionSpy(&client, &fgl::studio::ClangdClient::completionReady);
    QSignalSpy locationsSpy(&client, &fgl::studio::ClangdClient::locationsReady);
    QSignalSpy hoverSpy(&client, &fgl::studio::ClangdClient::hoverReady);
    QSignalSpy editSpy(&client, &fgl::studio::ClangdClient::workspaceEditFinished);
    QSignalSpy fileChangedSpy(&client, &fgl::studio::ClangdClient::fileContentChanged);

    QVERIFY(client.start(project.path()));
    QVERIFY(waitUntil([&client]() { return client.isReady(); }));
    QCOMPARE(client.projectRoot(), QFileInfo(project.path()).canonicalFilePath());
    QVERIFY(client.openDocument(sourcePath, QStringLiteral("old value;\n")));
    QCOMPARE(client.openDocumentCount(), qsizetype{1});
    QVERIFY(waitUntil([&diagnosticsSpy]() { return diagnosticsSpy.size() == 1; }));
    const auto diagnostics =
        qvariant_cast<fgl::studio::ClangdDiagnostics>(diagnosticsSpy.at(0).at(1));
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.front().severity, 2);
    QCOMPARE(diagnostics.front().message, QStringLiteral("deterministic fake diagnostic"));

    QVERIFY(client.changeDocument(sourcePath, QStringLiteral("old changed;\n")));
    QVERIFY(client.requestCompletion(sourcePath, 0, 3) > 0);
    QVERIFY(waitUntil([&completionSpy]() { return completionSpy.size() == 1; }));
    QCOMPARE(completionSpy.at(0).at(0).toString(), QFileInfo(sourcePath).canonicalFilePath());
    QCOMPARE(completionSpy.at(0).at(1).toStringList(),
             QStringList({QStringLiteral("Alpha"), QStringLiteral("Beta")}));

    QVERIFY(client.requestDefinition(sourcePath, 0, 1) > 0);
    QVERIFY(waitUntil([&locationsSpy]() { return locationsSpy.size() == 1; }));
    QCOMPARE(locationsSpy.at(0).at(0).toString(), QStringLiteral("definition"));
    auto locations = qvariant_cast<fgl::studio::ClangdLocations>(locationsSpy.at(0).at(1));
    QCOMPARE(locations.size(), 1);
    QCOMPARE(locations.front().filePath, QFileInfo(sourcePath).canonicalFilePath());

    QVERIFY(client.requestReferences(sourcePath, 0, 1) > 0);
    QVERIFY(waitUntil([&locationsSpy]() { return locationsSpy.size() == 2; }));
    locations = qvariant_cast<fgl::studio::ClangdLocations>(locationsSpy.at(1).at(1));
    QCOMPARE(locations.size(), 2);

    QVERIFY(client.requestHover(sourcePath, 0, 1) > 0);
    QVERIFY(waitUntil([&hoverSpy]() { return hoverSpy.size() == 1; }));
    QCOMPARE(hoverSpy.at(0).at(1).toString(), QStringLiteral("fake hover"));

    QVERIFY(client.requestRename(sourcePath, 0, 1, QStringLiteral("renamed")) > 0);
    QVERIFY(waitUntil([&editSpy]() { return editSpy.size() >= 1; }));
    QVERIFY(waitUntil([&fileChangedSpy]() { return fileChangedSpy.size() >= 1; }));
    QCOMPARE(readFile(sourcePath), QByteArrayLiteral("renamed changed;\n"));

    QVERIFY(client.requestFormatting(sourcePath) > 0);
    QVERIFY(waitUntil([&editSpy]() { return editSpy.size() >= 2; }));
    QVERIFY(waitUntil([&fileChangedSpy]() { return fileChangedSpy.size() >= 2; }));
    QCOMPARE(readFile(sourcePath), QByteArrayLiteral("// formatted\nrenamed changed;\n"));

    client.closeDocument(sourcePath);
    QCOMPARE(client.openDocumentCount(), qsizetype{0});
    client.stop();
    QVERIFY(waitUntil(
        [&client]() { return client.state() == fgl::studio::ClangdClient::State::Stopped; }));

    const QString log = QString::fromUtf8(readFile(logPath));
    QVERIFY(log.contains(QStringLiteral("cwd=") + QFileInfo(project.path()).canonicalFilePath()));
    const QStringList requiredMethods = {QStringLiteral("initialize"),
                                         QStringLiteral("initialized"),
                                         QStringLiteral("textDocument/didOpen"),
                                         QStringLiteral("textDocument/didChange"),
                                         QStringLiteral("textDocument/completion"),
                                         QStringLiteral("textDocument/definition"),
                                         QStringLiteral("textDocument/references"),
                                         QStringLiteral("textDocument/hover"),
                                         QStringLiteral("textDocument/rename"),
                                         QStringLiteral("textDocument/formatting"),
                                         QStringLiteral("textDocument/didClose"),
                                         QStringLiteral("shutdown"),
                                         QStringLiteral("exit")};
    for (const QString& method : requiredMethods) {
        QVERIFY2(log.contains(method),
                 qPrintable(QStringLiteral("Missing fake-server log entry: ") + method));
    }
}

void ClangdClientTests::workspaceEditsStayInsideCanonicalRoot() {
    QTemporaryDir project;
    QTemporaryDir outside;
    QVERIFY(project.isValid());
    QVERIFY(outside.isValid());
    const QString sourcePath = project.filePath(QStringLiteral("source.cpp"));
    const QString outsidePath = outside.filePath(QStringLiteral("outside.cpp"));
    const QString logPath = project.filePath(QStringLiteral("security-lsp.log"));
    QVERIFY(writeFile(sourcePath, QByteArrayLiteral("old value;\n")));
    QVERIFY(writeFile(outsidePath, QByteArrayLiteral("outside\n")));

    fgl::studio::ClangdClient client;
    client.configureServer(fakeServerPath(), {QStringLiteral("--log"), logPath});
    QVERIFY(client.start(project.path()));
    QVERIFY(waitUntil([&client]() { return client.isReady(); }));

    QString error;
    const QJsonArray validEdits = {QJsonObject{{QStringLiteral("range"), range(0, 0, 0, 3)},
                                               {QStringLiteral("newText"), QStringLiteral("new")}}};
    QVERIFY2(client.applyWorkspaceEdit(changesFor(sourcePath, validEdits), error),
             qPrintable(error));
    QCOMPARE(readFile(sourcePath), QByteArrayLiteral("new value;\n"));

    const QJsonArray outsideEdits = {
        QJsonObject{{QStringLiteral("range"), range(0, 0, 0, 7)},
                    {QStringLiteral("newText"), QStringLiteral("changed")}}};
    QVERIFY(!client.applyWorkspaceEdit(changesFor(outsidePath, outsideEdits), error));
    QVERIFY(error.contains(QStringLiteral("outside")) || error.contains(QStringLiteral("symlink")));
    QCOMPARE(readFile(outsidePath), QByteArrayLiteral("outside\n"));

    const QString rootUri = QUrl::fromLocalFile(project.path()).toString(QUrl::FullyEncoded);
    QJsonObject traversalChanges;
    traversalChanges.insert(rootUri + QStringLiteral("/nested/%2e%2e/source.cpp"), validEdits);
    QVERIFY(!client.applyWorkspaceEdit(QJsonObject{{QStringLiteral("changes"), traversalChanges}},
                                       error));
    QVERIFY(error.contains(QStringLiteral("traversal")));

    const QJsonArray overlapping = {
        QJsonObject{{QStringLiteral("range"), range(0, 0, 0, 3)},
                    {QStringLiteral("newText"), QStringLiteral("one")}},
        QJsonObject{{QStringLiteral("range"), range(0, 2, 0, 5)},
                    {QStringLiteral("newText"), QStringLiteral("two")}}};
    QVERIFY(!client.applyWorkspaceEdit(changesFor(sourcePath, overlapping), error));
    QVERIFY(error.contains(QStringLiteral("Overlapping")));
    QCOMPARE(readFile(sourcePath), QByteArrayLiteral("new value;\n"));

    const QString realDirectory = project.filePath(QStringLiteral("real-directory"));
    QVERIFY(QDir().mkpath(realDirectory));
    const QString linkedSource = QDir(realDirectory).filePath(QStringLiteral("linked.cpp"));
    QVERIFY(writeFile(linkedSource, QByteArrayLiteral("linked-original\n")));
    const QString linkedDirectory = project.filePath(QStringLiteral("directory-link"));
    QVERIFY2(createDirectoryLinkForTest(linkedDirectory, realDirectory),
             "The test fixture could not create a directory link/junction.");
    const QString throughLink = QDir(linkedDirectory).filePath(QStringLiteral("linked.cpp"));
    QVERIFY(!client.applyWorkspaceEdit(changesFor(throughLink, outsideEdits), error));
    QVERIFY(error.contains(QStringLiteral("symlink"), Qt::CaseInsensitive) ||
            error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
    QCOMPARE(readFile(linkedSource), QByteArrayLiteral("linked-original\n"));

    QJsonArray tooManyEdits;
    for (int index = 0; index <= fgl::studio::ClangdClient::MaximumWorkspaceEdits; ++index) {
        tooManyEdits.push_back(QJsonObject{{QStringLiteral("range"), range(0, 0, 0, 0)},
                                           {QStringLiteral("newText"), QString{}}});
    }
    QVERIFY(!client.applyWorkspaceEdit(changesFor(sourcePath, tooManyEdits), error));
    QVERIFY(error.contains(QStringLiteral("limit")));

    const QJsonArray resourceChanges = {QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("delete")},
        {QStringLiteral("uri"), QUrl::fromLocalFile(sourcePath).toString(QUrl::FullyEncoded)}}};
    QVERIFY(!client.applyWorkspaceEdit(
        QJsonObject{{QStringLiteral("documentChanges"), resourceChanges}}, error));
    QVERIFY(error.contains(QStringLiteral("create, rename, and delete")));

    client.stop();
    QVERIFY(waitUntil(
        [&client]() { return client.state() == fgl::studio::ClangdClient::State::Stopped; }));
}

void ClangdClientTests::missingServerDegradesSafely() {
    QTemporaryDir project;
    QVERIFY(project.isValid());
    fgl::studio::ClangdClient client;
    client.configureServer(project.filePath(QStringLiteral("missing-clangd")));
    QSignalSpy statusSpy(&client, &fgl::studio::ClangdClient::statusMessage);
    QVERIFY(client.start(project.path()));
    QVERIFY(waitUntil(
        [&client]() { return client.state() == fgl::studio::ClangdClient::State::Failed; }));
    QVERIFY(!client.isReady());
    bool visibleUnavailableStatus = false;
    for (const QList<QVariant>& arguments : statusSpy) {
        const QString message = arguments.front().toString();
        if (message.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive)) {
            visibleUnavailableStatus = true;
            break;
        }
    }
    QVERIFY(visibleUnavailableStatus);
}

QTEST_GUILESS_MAIN(ClangdClientTests)

#include "clangd_client_tests.moc"
