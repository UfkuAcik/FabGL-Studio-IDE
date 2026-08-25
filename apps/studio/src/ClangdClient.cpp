#include "ClangdClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace fgl::studio {
namespace {

constexpr auto JsonRpcVersion = "2.0";
constexpr int RequestTimeoutMilliseconds = 10'000;
constexpr int InitializeTimeoutMilliseconds = 15'000;
constexpr int ShutdownTimeoutMilliseconds = 1'500;
constexpr qsizetype MaximumCompletionItems = 512;
constexpr qsizetype MaximumLocations = 512;
constexpr qsizetype MaximumDiagnostics = 1'000;
constexpr qsizetype MaximumDisplayTextBytes = 64 * 1024;
constexpr qsizetype MaximumStandardErrorTailBytes = 16 * 1024;

struct TextEdit final {
    qsizetype start = 0;
    qsizetype end = 0;
    QString replacement;
};

struct PreparedFile final {
    QString path;
    QString contents;
    QByteArray encodedContents;
};

bool isLinkLikePath(const QString& filePath) {
    const QFileInfo info(filePath);
    if (info.isSymLink()) {
        return true;
    }
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    return false;
#endif
}

std::optional<int> boundedJsonInteger(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number || number < 0.0 ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

bool parsePosition(const QJsonValue& value, int& line, int& character) {
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject position = value.toObject();
    const auto parsedLine = boundedJsonInteger(position, QStringLiteral("line"));
    const auto parsedCharacter = boundedJsonInteger(position, QStringLiteral("character"));
    if (!parsedLine.has_value() || !parsedCharacter.has_value()) {
        return false;
    }
    line = *parsedLine;
    character = *parsedCharacter;
    return true;
}

std::optional<qsizetype> positionOffset(const QString& text, const int targetLine,
                                        const int targetCharacter) {
    if (targetLine < 0 || targetCharacter < 0) {
        return std::nullopt;
    }

    qsizetype line = 0;
    qsizetype lineStart = 0;
    while (line < static_cast<qsizetype>(targetLine)) {
        const qsizetype newline = text.indexOf(QLatin1Char('\n'), lineStart);
        if (newline < 0) {
            return std::nullopt;
        }
        lineStart = newline + 1;
        ++line;
    }

    qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
    if (lineEnd > lineStart && text.at(lineEnd - 1) == QLatin1Char('\r')) {
        --lineEnd;
    }
    const qsizetype character = static_cast<qsizetype>(targetCharacter);
    if (character > lineEnd - lineStart) {
        return std::nullopt;
    }
    const qsizetype offset = lineStart + character;
    if (offset > lineStart && offset < lineEnd && text.at(offset - 1).isHighSurrogate() &&
        text.at(offset).isLowSurrogate()) {
        return std::nullopt;
    }
    return offset;
}

QString locationUri(const QJsonObject& object) {
    const QJsonValue directUri = object.value(QStringLiteral("uri"));
    if (directUri.isString()) {
        return directUri.toString();
    }
    const QJsonValue targetUri = object.value(QStringLiteral("targetUri"));
    return targetUri.isString() ? targetUri.toString() : QString{};
}

QJsonValue locationRange(const QJsonObject& object) {
    const QJsonValue directRange = object.value(QStringLiteral("range"));
    if (directRange.isObject()) {
        return directRange;
    }
    return object.value(QStringLiteral("targetSelectionRange"));
}

QString markedStringText(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isObject()) {
        const QJsonValue text = value.toObject().value(QStringLiteral("value"));
        return text.isString() ? text.toString() : QString{};
    }
    return {};
}

} // namespace

JsonRpcMessageParser::FeedResult JsonRpcMessageParser::feed(const QByteArray& bytes) {
    if (bytes.size() > MaximumBufferedBytes ||
        m_buffer.size() > MaximumBufferedBytes - bytes.size()) {
        return fail(QStringLiteral("JSON-RPC input exceeded the bounded receive buffer."));
    }
    m_buffer.append(bytes);

    FeedResult result;
    while (result.messages.size() < MaximumMessagesPerFeed) {
        if (!m_expectedPayloadBytes.has_value()) {
            const qsizetype headerEnd = m_buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                if (m_buffer.size() > MaximumHeaderBytes) {
                    return fail(QStringLiteral("JSON-RPC header exceeded %1 bytes.")
                                    .arg(MaximumHeaderBytes));
                }
                break;
            }
            if (headerEnd > MaximumHeaderBytes) {
                return fail(
                    QStringLiteral("JSON-RPC header exceeded %1 bytes.").arg(MaximumHeaderBytes));
            }

            const QByteArray header = m_buffer.left(headerEnd);
            if (header.contains('\0')) {
                return fail(QStringLiteral("JSON-RPC header contained a NUL byte."));
            }
            std::optional<qsizetype> contentLength;
            const QList<QByteArray> lines = header.split('\n');
            for (QByteArray line : lines) {
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                const qsizetype colon = line.indexOf(':');
                if (colon <= 0) {
                    return fail(QStringLiteral("JSON-RPC header contained a malformed field."));
                }
                const QByteArray name = line.left(colon).trimmed();
                const QByteArray fieldValue = line.mid(colon + 1).trimmed();
                if (name.compare("Content-Length", Qt::CaseInsensitive) != 0) {
                    continue;
                }
                if (contentLength.has_value() || fieldValue.isEmpty()) {
                    return fail(QStringLiteral("JSON-RPC Content-Length was duplicated or empty."));
                }
                for (const char character : fieldValue) {
                    if (character < '0' || character > '9') {
                        return fail(
                            QStringLiteral("JSON-RPC Content-Length contained non-digits."));
                    }
                }
                bool converted = false;
                const qlonglong parsed = fieldValue.toLongLong(&converted, 10);
                if (!converted || parsed < 0 || parsed > MaximumPayloadBytes) {
                    return fail(
                        QStringLiteral("JSON-RPC Content-Length was invalid or too large."));
                }
                contentLength = static_cast<qsizetype>(parsed);
            }
            if (!contentLength.has_value()) {
                return fail(QStringLiteral("JSON-RPC message omitted Content-Length."));
            }
            m_buffer.remove(0, headerEnd + 4);
            m_expectedPayloadBytes = contentLength;
        }

        const qsizetype payloadBytes = *m_expectedPayloadBytes;
        if (m_buffer.size() < payloadBytes) {
            break;
        }
        const QByteArray payload = m_buffer.left(payloadBytes);
        m_buffer.remove(0, payloadBytes);
        m_expectedPayloadBytes.reset();

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return fail(QStringLiteral("JSON-RPC payload was not a valid JSON object: %1")
                            .arg(parseError.errorString()));
        }
        result.messages.push_back(document.object());
    }

    if (result.messages.size() == MaximumMessagesPerFeed && !m_buffer.isEmpty()) {
        return fail(QStringLiteral("Too many JSON-RPC messages arrived in one read."));
    }
    return result;
}

void JsonRpcMessageParser::reset() noexcept {
    m_buffer.clear();
    m_expectedPayloadBytes.reset();
}

qsizetype JsonRpcMessageParser::bufferedBytes() const noexcept {
    return m_buffer.size();
}

JsonRpcMessageParser::FeedResult JsonRpcMessageParser::fail(const QString& message) {
    reset();
    FeedResult result;
    result.errorMessage = message;
    return result;
}

ClangdClient::ClangdClient(QObject* parent)
    : QObject(parent), m_process(new QProcess(this)), m_shutdownTimer(new QTimer(this)) {
    qRegisterMetaType<ClangdLocations>();
    qRegisterMetaType<ClangdDiagnostics>();

    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_shutdownTimer->setSingleShot(true);
    connect(m_process, &QProcess::started, this, &ClangdClient::handleProcessStarted);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ClangdClient::handleReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_standardErrorTail.append(m_process->readAllStandardError());
        if (m_standardErrorTail.size() > MaximumStandardErrorTailBytes) {
            m_standardErrorTail.remove(0,
                                       m_standardErrorTail.size() - MaximumStandardErrorTailBytes);
        }
    });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { handleProcessError(); });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](const int exitCode, QProcess::ExitStatus) { handleProcessFinished(exitCode); });
    connect(m_shutdownTimer, &QTimer::timeout, this, [this]() {
        if (m_process->state() == QProcess::NotRunning) {
            return;
        }
        m_process->terminate();
        QTimer::singleShot(250, this, [this]() {
            if (m_process->state() != QProcess::NotRunning) {
                m_process->kill();
            }
        });
    });
}

ClangdClient::~ClangdClient() {
    m_destroying = true;
    m_pendingRestartRoot.clear();
    stop();
    if (m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(500)) {
        m_process->terminate();
        if (!m_process->waitForFinished(250)) {
            m_process->kill();
            (void)m_process->waitForFinished(250);
        }
    }
}

void ClangdClient::configureServer(const QString& program, const QStringList& arguments) {
    if (m_process->state() != QProcess::NotRunning) {
        emit statusMessage(tr("Stop clangd before changing its executable."));
        return;
    }
    m_program = program.trimmed();
    m_arguments = arguments;
}

QString ClangdClient::serverProgram() const {
    return m_program;
}

QStringList ClangdClient::serverArguments() const {
    return m_arguments;
}

bool ClangdClient::start(const QString& projectRoot) {
    const QFileInfo rootInfo(projectRoot);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (!rootInfo.isDir() || canonicalRoot.isEmpty()) {
        setState(State::Failed, tr("clangd was not started: the project root is invalid."));
        return false;
    }
    if (m_program.isEmpty()) {
        setState(State::Failed, tr("clangd was not started: no executable is configured."));
        return false;
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_pendingRestartRoot = canonicalRoot;
        stop();
        return true;
    }

    m_projectRoot = QDir::cleanPath(canonicalRoot);
    m_parser.reset();
    m_standardErrorTail.clear();
    m_pendingRequests.clear();
    m_openDocuments.clear();
    m_nextRequestId = 1;
    QString launchProgram = m_program;
    if (!QFileInfo(launchProgram).isAbsolute()) {
        const QString resolvedProgram = QStandardPaths::findExecutable(launchProgram);
        if (!resolvedProgram.isEmpty()) {
            launchProgram = QFileInfo(resolvedProgram).canonicalFilePath();
        }
    } else if (QFileInfo(launchProgram).isFile()) {
        const QString canonicalProgram = QFileInfo(launchProgram).canonicalFilePath();
        if (!canonicalProgram.isEmpty()) {
            launchProgram = canonicalProgram;
        }
    }
    m_process->setWorkingDirectory(m_projectRoot);
    m_process->setProgram(launchProgram);
    m_process->setArguments(m_arguments);
    setState(State::Starting,
             tr("Starting clangd for %1…").arg(QDir::toNativeSeparators(m_projectRoot)));
    m_process->start(QIODevice::ReadWrite);
    return true;
}

void ClangdClient::stop() {
    m_pendingRestartRoot = m_destroying ? QString{} : m_pendingRestartRoot;
    if (m_process->state() == QProcess::NotRunning) {
        m_openDocuments.clear();
        if (m_state != State::Failed) {
            setState(State::Stopped, tr("clangd stopped."));
        }
        return;
    }
    if (m_state == State::ShuttingDown) {
        return;
    }
    if (m_state == State::Ready) {
        const QList<OpenDocument> documents = m_openDocuments.values();
        for (const OpenDocument& document : documents) {
            QJsonObject textDocument;
            textDocument.insert(QStringLiteral("uri"), documentUri(document.filePath));
            (void)sendNotification(QStringLiteral("textDocument/didClose"),
                                   QJsonObject{{QStringLiteral("textDocument"), textDocument}});
        }
        m_openDocuments.clear();
        setState(State::ShuttingDown, tr("Stopping clangd…"));
        const qint64 requestId =
            sendRequest(RequestKind::Shutdown, QStringLiteral("shutdown"), QJsonObject{});
        if (requestId < 0) {
            finishShutdown();
        }
    } else if (m_state == State::Initializing) {
        setState(State::ShuttingDown, tr("Stopping clangd during initialization…"));
        finishShutdown();
    } else {
        setState(State::ShuttingDown, tr("Stopping clangd…"));
        finishShutdown();
    }
    m_shutdownTimer->start(ShutdownTimeoutMilliseconds);
}

ClangdClient::State ClangdClient::state() const noexcept {
    return m_state;
}

bool ClangdClient::isReady() const noexcept {
    return m_state == State::Ready;
}

QString ClangdClient::projectRoot() const {
    return m_projectRoot;
}

qsizetype ClangdClient::openDocumentCount() const noexcept {
    return m_openDocuments.size();
}

qsizetype ClangdClient::pendingRequestCount() const noexcept {
    return m_pendingRequests.size();
}

bool ClangdClient::openDocument(const QString& filePath, const QString& text) {
    if (!isReady() || text.toUtf8().size() > MaximumDocumentBytes) {
        return false;
    }
    QString canonicalPath;
    if (!isSafeProjectFile(filePath, &canonicalPath)) {
        emit statusMessage(tr("clangd ignored a document outside the canonical project root."));
        return false;
    }
    const QString key = normalizedPath(canonicalPath);
    const auto existing = m_openDocuments.constFind(key);
    if (existing != m_openDocuments.cend()) {
        return existing->text == text ? true : changeDocument(canonicalPath, text);
    }
    if (m_openDocuments.size() >= MaximumOpenDocuments) {
        return false;
    }

    OpenDocument document;
    document.filePath = canonicalPath;
    document.text = text;
    QJsonObject item;
    item.insert(QStringLiteral("uri"), documentUri(canonicalPath));
    item.insert(QStringLiteral("languageId"), languageId(canonicalPath));
    item.insert(QStringLiteral("version"), document.version);
    item.insert(QStringLiteral("text"), text);
    if (!sendNotification(QStringLiteral("textDocument/didOpen"),
                          QJsonObject{{QStringLiteral("textDocument"), item}})) {
        return false;
    }
    m_openDocuments.insert(key, document);
    return true;
}

bool ClangdClient::changeDocument(const QString& filePath, const QString& text) {
    if (!isReady() || text.toUtf8().size() > MaximumDocumentBytes) {
        return false;
    }
    QString canonicalPath;
    if (!isSafeProjectFile(filePath, &canonicalPath)) {
        return false;
    }
    const QString key = normalizedPath(canonicalPath);
    auto existing = m_openDocuments.find(key);
    if (existing == m_openDocuments.end()) {
        return openDocument(canonicalPath, text);
    }
    if (existing->text == text) {
        return true;
    }
    const int nextVersion = existing->version + 1;
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("uri"), documentUri(existing->filePath));
    textDocument.insert(QStringLiteral("version"), nextVersion);
    const QJsonArray changes = {QJsonObject{{QStringLiteral("text"), text}}};
    QJsonObject params;
    params.insert(QStringLiteral("textDocument"), textDocument);
    params.insert(QStringLiteral("contentChanges"), changes);
    if (!sendNotification(QStringLiteral("textDocument/didChange"), params)) {
        return false;
    }
    existing->text = text;
    existing->version = nextVersion;
    return true;
}

void ClangdClient::closeDocument(const QString& filePath) {
    QString canonicalPath;
    if (!isSafeProjectFile(filePath, &canonicalPath)) {
        return;
    }
    const QString key = normalizedPath(canonicalPath);
    const auto existing = m_openDocuments.find(key);
    if (existing == m_openDocuments.end()) {
        return;
    }
    if (isReady()) {
        QJsonObject textDocument;
        textDocument.insert(QStringLiteral("uri"), documentUri(existing->filePath));
        (void)sendNotification(QStringLiteral("textDocument/didClose"),
                               QJsonObject{{QStringLiteral("textDocument"), textDocument}});
    }
    m_openDocuments.erase(existing);
}

qint64 ClangdClient::requestCompletion(const QString& filePath, const int line,
                                       const int character) {
    return sendRequest(RequestKind::Completion, QStringLiteral("textDocument/completion"),
                       documentPositionParams(filePath, line, character), filePath);
}

qint64 ClangdClient::requestDefinition(const QString& filePath, const int line,
                                       const int character) {
    return sendRequest(RequestKind::Definition, QStringLiteral("textDocument/definition"),
                       documentPositionParams(filePath, line, character), filePath);
}

qint64 ClangdClient::requestReferences(const QString& filePath, const int line,
                                       const int character) {
    QJsonObject params = documentPositionParams(filePath, line, character);
    params.insert(QStringLiteral("context"),
                  QJsonObject{{QStringLiteral("includeDeclaration"), true}});
    return sendRequest(RequestKind::References, QStringLiteral("textDocument/references"), params,
                       filePath);
}

qint64 ClangdClient::requestHover(const QString& filePath, const int line, const int character) {
    return sendRequest(RequestKind::Hover, QStringLiteral("textDocument/hover"),
                       documentPositionParams(filePath, line, character), filePath);
}

qint64 ClangdClient::requestRename(const QString& filePath, const int line, const int character,
                                   const QString& newName) {
    if (newName.isEmpty() || newName.toUtf8().size() > 1'024) {
        return -1;
    }
    QJsonObject params = documentPositionParams(filePath, line, character);
    params.insert(QStringLiteral("newName"), newName);
    return sendRequest(RequestKind::Rename, QStringLiteral("textDocument/rename"), params,
                       filePath);
}

qint64 ClangdClient::requestFormatting(const QString& filePath, const int tabSize,
                                       const bool insertSpaces) {
    if (tabSize < 1 || tabSize > 16) {
        return -1;
    }
    QString canonicalPath;
    if (!isReady() || !isSafeProjectFile(filePath, &canonicalPath) ||
        !m_openDocuments.contains(normalizedPath(canonicalPath))) {
        return -1;
    }
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("uri"), documentUri(canonicalPath));
    QJsonObject options;
    options.insert(QStringLiteral("tabSize"), tabSize);
    options.insert(QStringLiteral("insertSpaces"), insertSpaces);
    QJsonObject params;
    params.insert(QStringLiteral("textDocument"), textDocument);
    params.insert(QStringLiteral("options"), options);
    return sendRequest(RequestKind::Formatting, QStringLiteral("textDocument/formatting"), params,
                       canonicalPath);
}

bool ClangdClient::applyWorkspaceEdit(const QJsonObject& workspaceEdit, QString& errorMessage) {
    errorMessage.clear();
    if (m_projectRoot.isEmpty()) {
        errorMessage = tr("No canonical project root is available for the workspace edit.");
        return false;
    }

    QHash<QString, QJsonArray> editsByPath;
    QHash<QString, QString> canonicalPaths;
    int totalEditCount = 0;
    qsizetype totalReplacementBytes = 0;
    const auto addFileEdits = [&](const QString& uri, const QJsonValue& value,
                                  QString& error) -> bool {
        if (!value.isArray()) {
            error = tr("A workspace edit entry was not an edit array.");
            return false;
        }
        QString pathError;
        const QString canonicalPath = safeFilePathFromUri(uri, pathError);
        if (canonicalPath.isEmpty()) {
            error = pathError;
            return false;
        }
        const QString key = normalizedPath(canonicalPath);
        if (editsByPath.contains(key)) {
            error = tr("A workspace edit targeted the same canonical file more than once.");
            return false;
        }
        const QJsonArray edits = value.toArray();
        if (edits.size() > MaximumWorkspaceEdits - totalEditCount) {
            error = tr("A workspace edit exceeded the %1-edit limit.").arg(MaximumWorkspaceEdits);
            return false;
        }
        for (const QJsonValue& editValue : edits) {
            if (!editValue.isObject()) {
                error = tr("A workspace edit contained a non-object edit.");
                return false;
            }
            const QJsonValue replacement = editValue.toObject().value(QStringLiteral("newText"));
            if (!replacement.isString()) {
                error = tr("A workspace edit omitted replacement text.");
                return false;
            }
            const qsizetype replacementBytes = replacement.toString().toUtf8().size();
            if (replacementBytes > MaximumWorkspaceReplacementBytes - totalReplacementBytes) {
                error = tr("Workspace replacement text exceeded the %1-byte limit.")
                            .arg(MaximumWorkspaceReplacementBytes);
                return false;
            }
            totalReplacementBytes += replacementBytes;
        }
        totalEditCount += static_cast<int>(edits.size());
        editsByPath.insert(key, edits);
        canonicalPaths.insert(key, canonicalPath);
        if (editsByPath.size() > MaximumWorkspaceFiles) {
            error = tr("A workspace edit exceeded the %1-file limit.").arg(MaximumWorkspaceFiles);
            return false;
        }
        return true;
    };

    const QJsonValue changesValue = workspaceEdit.value(QStringLiteral("changes"));
    if (!changesValue.isUndefined()) {
        if (!changesValue.isObject()) {
            errorMessage = tr("Workspace 'changes' was not an object.");
            return false;
        }
        const QJsonObject changes = changesValue.toObject();
        for (auto iterator = changes.constBegin(); iterator != changes.constEnd(); ++iterator) {
            if (!addFileEdits(iterator.key(), iterator.value(), errorMessage)) {
                return false;
            }
        }
    }

    const QJsonValue documentChangesValue = workspaceEdit.value(QStringLiteral("documentChanges"));
    if (!documentChangesValue.isUndefined()) {
        if (!documentChangesValue.isArray()) {
            errorMessage = tr("Workspace 'documentChanges' was not an array.");
            return false;
        }
        const QJsonArray documentChanges = documentChangesValue.toArray();
        if (documentChanges.size() > MaximumWorkspaceFiles) {
            errorMessage =
                tr("A workspace edit exceeded the %1-file limit.").arg(MaximumWorkspaceFiles);
            return false;
        }
        for (const QJsonValue& documentChangeValue : documentChanges) {
            if (!documentChangeValue.isObject()) {
                errorMessage = tr("A document change was not an object.");
                return false;
            }
            const QJsonObject documentChange = documentChangeValue.toObject();
            if (documentChange.contains(QStringLiteral("kind"))) {
                errorMessage =
                    tr("File create, rename, and delete operations are not accepted from clangd.");
                return false;
            }
            const QJsonValue textDocumentValue =
                documentChange.value(QStringLiteral("textDocument"));
            const QJsonValue editsValue = documentChange.value(QStringLiteral("edits"));
            if (!textDocumentValue.isObject() || !editsValue.isArray()) {
                errorMessage = tr("A text document edit was malformed.");
                return false;
            }
            const QJsonValue uriValue = textDocumentValue.toObject().value(QStringLiteral("uri"));
            if (!uriValue.isString() ||
                !addFileEdits(uriValue.toString(), editsValue, errorMessage)) {
                if (errorMessage.isEmpty()) {
                    errorMessage = tr("A text document edit omitted its URI.");
                }
                return false;
            }
        }
    }

    if (editsByPath.isEmpty()) {
        return true;
    }

    std::vector<PreparedFile> preparedFiles;
    preparedFiles.reserve(static_cast<std::size_t>(editsByPath.size()));
    for (auto iterator = editsByPath.constBegin(); iterator != editsByPath.constEnd(); ++iterator) {
        const QString key = iterator.key();
        const QString path = canonicalPaths.value(key);
        QString canonicalCheck;
        if (!isSafeProjectFile(path, &canonicalCheck) || normalizedPath(canonicalCheck) != key) {
            errorMessage = tr("A workspace file failed canonical-path revalidation.");
            return false;
        }

        QString contents;
        const auto openDocument = m_openDocuments.constFind(key);
        if (openDocument != m_openDocuments.cend()) {
            contents = openDocument->text;
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumDocumentBytes) {
                errorMessage =
                    tr("Cannot safely read workspace file %1.").arg(QDir::toNativeSeparators(path));
                return false;
            }
            const QByteArray bytes = file.read(MaximumDocumentBytes + 1);
            if (bytes.size() > MaximumDocumentBytes) {
                errorMessage = tr("Workspace file %1 exceeded the size limit.")
                                   .arg(QDir::toNativeSeparators(path));
                return false;
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            contents = decoder.decode(bytes);
            if (decoder.hasError()) {
                errorMessage = tr("Workspace file %1 was not valid UTF-8.")
                                   .arg(QDir::toNativeSeparators(path));
                return false;
            }
        }

        std::vector<TextEdit> parsedEdits;
        parsedEdits.reserve(static_cast<std::size_t>(iterator.value().size()));
        for (const QJsonValue& editValue : iterator.value()) {
            const QJsonObject edit = editValue.toObject();
            const QJsonValue rangeValue = edit.value(QStringLiteral("range"));
            if (!rangeValue.isObject()) {
                errorMessage = tr("A workspace text edit omitted its range.");
                return false;
            }
            const QJsonObject range = rangeValue.toObject();
            int startLine = 0;
            int startCharacter = 0;
            int endLine = 0;
            int endCharacter = 0;
            if (!parsePosition(range.value(QStringLiteral("start")), startLine, startCharacter) ||
                !parsePosition(range.value(QStringLiteral("end")), endLine, endCharacter)) {
                errorMessage = tr("A workspace text edit contained an invalid position.");
                return false;
            }
            const auto start = positionOffset(contents, startLine, startCharacter);
            const auto end = positionOffset(contents, endLine, endCharacter);
            if (!start.has_value() || !end.has_value() || *end < *start) {
                errorMessage = tr("A workspace text edit range was outside its file.");
                return false;
            }
            parsedEdits.push_back(
                TextEdit{*start, *end, edit.value(QStringLiteral("newText")).toString()});
        }
        std::sort(parsedEdits.begin(), parsedEdits.end(),
                  [](const TextEdit& left, const TextEdit& right) {
                      if (left.start != right.start) {
                          return left.start > right.start;
                      }
                      return left.end > right.end;
                  });
        for (std::size_t index = 1; index < parsedEdits.size(); ++index) {
            const TextEdit& higher = parsedEdits[index - 1];
            const TextEdit& lower = parsedEdits[index];
            const bool duplicateInsertion = higher.start == higher.end &&
                                            lower.start == lower.end && higher.start == lower.start;
            if (lower.end > higher.start || duplicateInsertion) {
                errorMessage = tr("Overlapping workspace text edits were rejected.");
                return false;
            }
        }
        for (const TextEdit& edit : parsedEdits) {
            contents.replace(edit.start, edit.end - edit.start, edit.replacement);
        }
        QByteArray encodedContents = contents.toUtf8();
        if (encodedContents.size() > MaximumDocumentBytes) {
            errorMessage = tr("A workspace edit produced an oversized file.");
            return false;
        }
        preparedFiles.push_back(
            PreparedFile{path, std::move(contents), std::move(encodedContents)});
    }

    std::vector<std::unique_ptr<QSaveFile>> saveFiles;
    saveFiles.reserve(preparedFiles.size());
    for (const PreparedFile& prepared : preparedFiles) {
        QString canonicalCheck;
        if (!isSafeProjectFile(prepared.path, &canonicalCheck) ||
            normalizedPath(canonicalCheck) != normalizedPath(prepared.path)) {
            errorMessage = tr("A workspace file changed identity before it could be written.");
            return false;
        }
        auto saveFile = std::make_unique<QSaveFile>(prepared.path);
        saveFile->setDirectWriteFallback(false);
        if (!saveFile->open(QIODevice::WriteOnly) ||
            saveFile->write(prepared.encodedContents) != prepared.encodedContents.size()) {
            errorMessage =
                tr("Cannot atomically prepare workspace file %1: %2")
                    .arg(QDir::toNativeSeparators(prepared.path), saveFile->errorString());
            for (const auto& openedFile : saveFiles) {
                openedFile->cancelWriting();
            }
            saveFile->cancelWriting();
            return false;
        }
        saveFiles.push_back(std::move(saveFile));
    }
    for (std::size_t index = 0; index < saveFiles.size(); ++index) {
        QString canonicalCheck;
        if (!isSafeProjectFile(preparedFiles[index].path, &canonicalCheck) ||
            normalizedPath(canonicalCheck) != normalizedPath(preparedFiles[index].path)) {
            errorMessage = tr("A workspace file changed identity before atomic commit.");
            for (std::size_t remaining = index; remaining < saveFiles.size(); ++remaining) {
                saveFiles[remaining]->cancelWriting();
            }
            return false;
        }
        if (!saveFiles[index]->commit()) {
            errorMessage = tr("Cannot atomically commit workspace file %1: %2")
                               .arg(QDir::toNativeSeparators(preparedFiles[index].path),
                                    saveFiles[index]->errorString());
            for (std::size_t remaining = index + 1; remaining < saveFiles.size(); ++remaining) {
                saveFiles[remaining]->cancelWriting();
            }
            return false;
        }
    }

    for (const PreparedFile& prepared : preparedFiles) {
        const QString key = normalizedPath(prepared.path);
        auto openDocument = m_openDocuments.find(key);
        if (openDocument != m_openDocuments.end()) {
            openDocument->text = prepared.contents;
            ++openDocument->version;
            if (isReady()) {
                QJsonObject textDocument;
                textDocument.insert(QStringLiteral("uri"), documentUri(openDocument->filePath));
                textDocument.insert(QStringLiteral("version"), openDocument->version);
                QJsonObject params;
                params.insert(QStringLiteral("textDocument"), textDocument);
                params.insert(QStringLiteral("contentChanges"),
                              QJsonArray{QJsonObject{{QStringLiteral("text"), prepared.contents}}});
                (void)sendNotification(QStringLiteral("textDocument/didChange"), params);
            }
        }
        emit fileContentChanged(prepared.path, prepared.contents);
    }
    return true;
}

void ClangdClient::setState(const State state, const QString& status) {
    const bool wasReady = isReady();
    const bool changed = m_state != state;
    m_state = state;
    if (changed) {
        emit stateChanged(m_state);
    }
    if (wasReady != isReady()) {
        emit readyChanged(isReady());
    }
    if (!status.isEmpty()) {
        emit statusMessage(status);
    }
}

bool ClangdClient::sendMessage(const QJsonObject& message) {
    if (m_process->state() == QProcess::NotRunning) {
        return false;
    }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > JsonRpcMessageParser::MaximumPayloadBytes) {
        emit statusMessage(tr("clangd message was dropped because an I/O bound was reached."));
        return false;
    }
    const QByteArray header = QByteArrayLiteral("Content-Length: ") +
                              QByteArray::number(payload.size()) + QByteArrayLiteral("\r\n\r\n");
    const QByteArray frame = header + payload;
    const qint64 queuedBytes = m_process->bytesToWrite();
    if (queuedBytes < 0 ||
        queuedBytes > static_cast<qint64>(JsonRpcMessageParser::MaximumBufferedBytes) ||
        frame.size() > JsonRpcMessageParser::MaximumBufferedBytes - queuedBytes) {
        emit statusMessage(tr("clangd message was dropped because an I/O bound was reached."));
        return false;
    }
    return m_process->write(frame) == static_cast<qint64>(frame.size());
}

bool ClangdClient::sendNotification(const QString& method, const QJsonObject& params) {
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QString::fromLatin1(JsonRpcVersion));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    return sendMessage(message);
}

qint64 ClangdClient::sendRequest(const RequestKind kind, const QString& method,
                                 const QJsonObject& params, const QString& filePath) {
    const bool lifecycleRequest = kind == RequestKind::Initialize || kind == RequestKind::Shutdown;
    if ((!lifecycleRequest && !isReady()) || m_process->state() == QProcess::NotRunning ||
        m_pendingRequests.size() >= MaximumPendingRequests ||
        (!lifecycleRequest && params.isEmpty())) {
        return -1;
    }
    const qint64 requestId = m_nextRequestId++;
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QString::fromLatin1(JsonRpcVersion));
    message.insert(QStringLiteral("id"), static_cast<double>(requestId));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    if (!sendMessage(message)) {
        return -1;
    }
    m_pendingRequests.insert(requestId, PendingRequest{kind, filePath});
    const int timeout = kind == RequestKind::Initialize ? InitializeTimeoutMilliseconds
                                                        : RequestTimeoutMilliseconds;
    QTimer::singleShot(timeout, this, [this, requestId]() { handleRequestTimeout(requestId); });
    return requestId;
}

void ClangdClient::sendResponse(const QJsonValue& id, const QJsonValue& result) {
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QString::fromLatin1(JsonRpcVersion));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    (void)sendMessage(response);
}

void ClangdClient::sendErrorResponse(const QJsonValue& id, const int code, const QString& message) {
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QString::fromLatin1(JsonRpcVersion));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("error"), error);
    (void)sendMessage(response);
}

void ClangdClient::handleReadyRead() {
    const QByteArray bytes = m_process->readAllStandardOutput();
    const JsonRpcMessageParser::FeedResult result = m_parser.feed(bytes);
    if (!result.ok()) {
        failProtocol(result.errorMessage);
        return;
    }
    for (const QJsonObject& message : result.messages) {
        handleMessage(message);
        if (m_state == State::Failed) {
            break;
        }
    }
}

void ClangdClient::handleMessage(const QJsonObject& message) {
    if (message.value(QStringLiteral("jsonrpc")).toString() !=
        QString::fromLatin1(JsonRpcVersion)) {
        failProtocol(tr("clangd sent a message with an invalid JSON-RPC version."));
        return;
    }
    const bool hasMethod = message.value(QStringLiteral("method")).isString();
    const bool hasId = message.contains(QStringLiteral("id"));
    if (hasMethod && hasId) {
        handleServerCall(message);
    } else if (hasMethod) {
        handleNotification(message);
    } else if (hasId) {
        handleResponse(message);
    } else {
        failProtocol(tr("clangd sent an unrecognized JSON-RPC object."));
    }
}

void ClangdClient::handleResponse(const QJsonObject& message) {
    const QJsonValue idValue = message.value(QStringLiteral("id"));
    if (!idValue.isDouble()) {
        return;
    }
    const double idNumber = idValue.toDouble();
    if (!std::isfinite(idNumber) || std::trunc(idNumber) != idNumber || idNumber < 0.0 ||
        idNumber > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return;
    }
    const qint64 requestId = static_cast<qint64>(idNumber);
    const auto pending = m_pendingRequests.find(requestId);
    if (pending == m_pendingRequests.end()) {
        return;
    }
    const PendingRequest request = pending.value();
    m_pendingRequests.erase(pending);

    const QJsonValue errorValue = message.value(QStringLiteral("error"));
    if (errorValue.isObject()) {
        const QString serverMessage =
            errorValue.toObject().value(QStringLiteral("message")).toString();
        if (request.kind == RequestKind::Initialize) {
            setState(State::Failed, tr("clangd initialization failed: %1").arg(serverMessage));
            m_process->kill();
            return;
        }
        if (request.kind == RequestKind::Shutdown) {
            finishShutdown();
            return;
        }
        emit workspaceEditFinished(QString{}, false,
                                   tr("clangd request failed: %1").arg(serverMessage));
        return;
    }

    const QJsonValue result = message.value(QStringLiteral("result"));
    switch (request.kind) {
    case RequestKind::Initialize:
        if (!sendNotification(QStringLiteral("initialized"))) {
            failProtocol(tr("the initialized notification could not be sent"));
            return;
        }
        setState(State::Ready, tr("clangd is ready."));
        break;
    case RequestKind::Shutdown:
        finishShutdown();
        break;
    case RequestKind::Completion: {
        QJsonArray items;
        if (result.isArray()) {
            items = result.toArray();
        } else if (result.isObject()) {
            const QJsonValue itemValue = result.toObject().value(QStringLiteral("items"));
            if (itemValue.isArray()) {
                items = itemValue.toArray();
            }
        }
        QStringList labels;
        const qsizetype count = std::min(items.size(), MaximumCompletionItems);
        labels.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            const QJsonValue itemValue = items.at(index);
            const QString label =
                itemValue.isObject()
                    ? itemValue.toObject().value(QStringLiteral("label")).toString()
                    : QString{};
            if (!label.isEmpty() && label.toUtf8().size() <= 1'024 && !labels.contains(label)) {
                labels.push_back(label);
            }
        }
        emit completionReady(request.filePath, labels);
        break;
    }
    case RequestKind::Definition:
        emit locationsReady(QStringLiteral("definition"), parseLocations(result));
        break;
    case RequestKind::References:
        emit locationsReady(QStringLiteral("references"), parseLocations(result));
        break;
    case RequestKind::Hover:
        emit hoverReady(request.filePath, hoverText(result));
        break;
    case RequestKind::Rename: {
        QString error;
        const bool applied =
            result.isNull() || (result.isObject() && applyWorkspaceEdit(result.toObject(), error));
        const QString resultMessage =
            applied ? tr("clangd rename applied.")
                    : (result.isObject() ? error : tr("clangd returned an invalid rename edit."));
        emit workspaceEditFinished(QStringLiteral("rename"), applied, resultMessage);
        break;
    }
    case RequestKind::Formatting: {
        bool applied = false;
        QString error;
        if (result.isArray()) {
            QJsonObject changes;
            changes.insert(documentUri(request.filePath), result.toArray());
            applied = applyWorkspaceEdit(QJsonObject{{QStringLiteral("changes"), changes}}, error);
        } else {
            error = tr("clangd returned an invalid formatting edit.");
        }
        emit workspaceEditFinished(QStringLiteral("formatting"), applied,
                                   applied ? tr("clangd formatting applied.") : error);
        break;
    }
    }
}

void ClangdClient::handleServerCall(const QJsonObject& message) {
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonValue paramsValue = message.value(QStringLiteral("params"));
    if (method == QStringLiteral("workspace/applyEdit")) {
        QString error;
        bool applied = false;
        if (paramsValue.isObject()) {
            const QJsonValue editValue = paramsValue.toObject().value(QStringLiteral("edit"));
            applied = editValue.isObject() && applyWorkspaceEdit(editValue.toObject(), error);
        }
        if (error.isEmpty() && !applied) {
            error = tr("The server supplied an invalid workspace edit.");
        }
        QJsonObject result;
        result.insert(QStringLiteral("applied"), applied);
        if (!applied) {
            result.insert(QStringLiteral("failureReason"), error);
        }
        sendResponse(id, result);
        emit workspaceEditFinished(QStringLiteral("workspace"), applied,
                                   applied ? tr("clangd workspace edit applied.") : error);
        return;
    }
    if (method == QStringLiteral("workspace/configuration")) {
        sendResponse(id, QJsonArray{});
        return;
    }
    if (method == QStringLiteral("client/registerCapability") ||
        method == QStringLiteral("client/unregisterCapability") ||
        method == QStringLiteral("window/workDoneProgress/create")) {
        sendResponse(id, QJsonValue::Null);
        return;
    }
    sendErrorResponse(id, -32601, tr("Method not supported by FabGL Studio."));
}

void ClangdClient::handleNotification(const QJsonObject& message) {
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue paramsValue = message.value(QStringLiteral("params"));
    if (method == QStringLiteral("textDocument/publishDiagnostics") && paramsValue.isObject()) {
        const QJsonObject params = paramsValue.toObject();
        const QJsonValue uriValue = params.value(QStringLiteral("uri"));
        const QJsonValue diagnosticsValue = params.value(QStringLiteral("diagnostics"));
        if (!uriValue.isString() || !diagnosticsValue.isArray()) {
            return;
        }
        QString pathError;
        const QString path = safeFilePathFromUri(uriValue.toString(), pathError);
        if (path.isEmpty()) {
            return;
        }
        ClangdDiagnostics diagnostics;
        const QJsonArray values = diagnosticsValue.toArray();
        const qsizetype count = std::min(values.size(), MaximumDiagnostics);
        diagnostics.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            const QJsonValue value = values.at(index);
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject diagnosticObject = value.toObject();
            const QJsonValue rangeValue = diagnosticObject.value(QStringLiteral("range"));
            if (!rangeValue.isObject()) {
                continue;
            }
            const QJsonObject range = rangeValue.toObject();
            ClangdDiagnostic diagnostic;
            if (!parsePosition(range.value(QStringLiteral("start")), diagnostic.startLine,
                               diagnostic.startCharacter) ||
                !parsePosition(range.value(QStringLiteral("end")), diagnostic.endLine,
                               diagnostic.endCharacter)) {
                continue;
            }
            const QJsonValue severity = diagnosticObject.value(QStringLiteral("severity"));
            if (severity.isDouble()) {
                diagnostic.severity = std::clamp(severity.toInt(1), 1, 4);
            }
            diagnostic.message = diagnosticObject.value(QStringLiteral("message")).toString();
            if (diagnostic.message.toUtf8().size() > MaximumDisplayTextBytes) {
                diagnostic.message.truncate(MaximumDisplayTextBytes);
            }
            diagnostics.push_back(std::move(diagnostic));
        }
        emit diagnosticsPublished(path, diagnostics);
        return;
    }
    if (method == QStringLiteral("window/showMessage") && paramsValue.isObject()) {
        QString text = paramsValue.toObject().value(QStringLiteral("message")).toString();
        if (text.toUtf8().size() > MaximumDisplayTextBytes) {
            text.truncate(MaximumDisplayTextBytes);
        }
        if (!text.isEmpty()) {
            emit statusMessage(tr("clangd: %1").arg(text));
        }
    }
}

void ClangdClient::handleProcessStarted() {
    if (m_state != State::Starting) {
        return;
    }
    setState(State::Initializing, tr("Initializing clangd…"));
    QJsonObject workspaceEdit;
    workspaceEdit.insert(QStringLiteral("documentChanges"), true);
    workspaceEdit.insert(QStringLiteral("normalizesLineEndings"), false);
    QJsonObject workspace;
    workspace.insert(QStringLiteral("applyEdit"), true);
    workspace.insert(QStringLiteral("workspaceEdit"), workspaceEdit);
    workspace.insert(QStringLiteral("configuration"), true);
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("completion"),
                        QJsonObject{{QStringLiteral("completionItem"),
                                     QJsonObject{{QStringLiteral("snippetSupport"), false}}}});
    textDocument.insert(QStringLiteral("definition"), QJsonObject{});
    textDocument.insert(QStringLiteral("references"), QJsonObject{});
    textDocument.insert(QStringLiteral("hover"), QJsonObject{});
    textDocument.insert(QStringLiteral("rename"), QJsonObject{});
    textDocument.insert(QStringLiteral("formatting"), QJsonObject{});
    textDocument.insert(QStringLiteral("publishDiagnostics"),
                        QJsonObject{{QStringLiteral("relatedInformation"), false}});
    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("workspace"), workspace);
    capabilities.insert(QStringLiteral("textDocument"), textDocument);
    capabilities.insert(
        QStringLiteral("general"),
        QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}});
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("FabGL Studio"));
    clientInfo.insert(QStringLiteral("version"), QCoreApplication::applicationVersion().isEmpty()
                                                     ? QStringLiteral("development")
                                                     : QCoreApplication::applicationVersion());
    QJsonObject params;
    params.insert(QStringLiteral("processId"),
                  static_cast<double>(QCoreApplication::applicationPid()));
    params.insert(QStringLiteral("clientInfo"), clientInfo);
    params.insert(QStringLiteral("rootUri"), QUrl::fromLocalFile(m_projectRoot).toString());
    params.insert(QStringLiteral("capabilities"), capabilities);
    params.insert(QStringLiteral("trace"), QStringLiteral("off"));
    if (sendRequest(RequestKind::Initialize, QStringLiteral("initialize"), params) < 0) {
        setState(State::Failed, tr("Failed to send the clangd initialize request."));
        m_process->kill();
    }
}

void ClangdClient::handleProcessError() {
    if (m_process->error() == QProcess::FailedToStart) {
        setState(
            State::Failed,
            tr("clangd is unavailable (%1). Code editing remains available without LSP features.")
                .arg(m_process->errorString()));
        m_pendingRequests.clear();
        m_openDocuments.clear();
    } else if (m_state != State::ShuttingDown) {
        emit statusMessage(tr("clangd process error: %1").arg(m_process->errorString()));
    }
}

void ClangdClient::handleProcessFinished(const int exitCode) {
    m_shutdownTimer->stop();
    const State previousState = m_state;
    m_parser.reset();
    m_pendingRequests.clear();
    m_openDocuments.clear();
    if (previousState == State::ShuttingDown || m_destroying) {
        setState(State::Stopped, m_destroying ? QString{} : tr("clangd stopped."));
    } else if (previousState != State::Failed) {
        QString detail = QString::fromLocal8Bit(m_standardErrorTail).trimmed();
        if (detail.size() > 1'024) {
            detail = detail.right(1'024);
        }
        setState(
            State::Failed,
            detail.isEmpty()
                ? tr("clangd exited unexpectedly with code %1. Code editing remains available.")
                      .arg(exitCode)
                : tr("clangd exited unexpectedly with code %1: %2").arg(exitCode).arg(detail));
    }

    if (!m_destroying && !m_pendingRestartRoot.isEmpty()) {
        const QString restartRoot = std::exchange(m_pendingRestartRoot, QString{});
        QTimer::singleShot(0, this, [this, restartRoot]() { (void)start(restartRoot); });
    }
}

void ClangdClient::handleRequestTimeout(const qint64 requestId) {
    const auto pending = m_pendingRequests.find(requestId);
    if (pending == m_pendingRequests.end()) {
        return;
    }
    const RequestKind kind = pending->kind;
    m_pendingRequests.erase(pending);
    if (kind == RequestKind::Initialize) {
        setState(State::Failed, tr("clangd initialization timed out."));
        m_process->kill();
    } else if (kind == RequestKind::Shutdown) {
        finishShutdown();
    } else {
        emit statusMessage(tr("A clangd request timed out."));
    }
}

void ClangdClient::finishShutdown() {
    if (m_process->state() == QProcess::NotRunning) {
        setState(State::Stopped, tr("clangd stopped."));
        return;
    }
    (void)sendNotification(QStringLiteral("exit"));
    m_process->closeWriteChannel();
    m_shutdownTimer->start(ShutdownTimeoutMilliseconds);
}

void ClangdClient::failProtocol(const QString& message) {
    setState(State::Failed,
             tr("clangd protocol error: %1. The language server was stopped safely.").arg(message));
    m_pendingRequests.clear();
    m_openDocuments.clear();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

QJsonObject ClangdClient::documentPositionParams(const QString& filePath, const int line,
                                                 const int character) const {
    if (!isReady() || line < 0 || character < 0) {
        return {};
    }
    QString canonicalPath;
    if (!isSafeProjectFile(filePath, &canonicalPath) ||
        !m_openDocuments.contains(normalizedPath(canonicalPath))) {
        return {};
    }
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("uri"), documentUri(canonicalPath));
    QJsonObject position;
    position.insert(QStringLiteral("line"), line);
    position.insert(QStringLiteral("character"), character);
    QJsonObject params;
    params.insert(QStringLiteral("textDocument"), textDocument);
    params.insert(QStringLiteral("position"), position);
    return params;
}

QString ClangdClient::safeFilePathFromUri(const QString& uri, QString& errorMessage) const {
    errorMessage.clear();
    if (uri.toUtf8().size() > 32 * 1024) {
        errorMessage = tr("A workspace URI exceeded the size limit.");
        return {};
    }
    const QString decodedUri = QUrl::fromPercentEncoding(uri.toUtf8());
    QString portableDecoded = decodedUri;
    portableDecoded.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList decodedParts = portableDecoded.split(QLatin1Char('/'));
    if (decodedParts.contains(QStringLiteral(".."))) {
        errorMessage = tr("A workspace URI contained path traversal.");
        return {};
    }
    const QUrl url(uri, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0 ||
        !url.host().isEmpty() || url.hasQuery() || url.hasFragment()) {
        errorMessage =
            tr("Only local file URIs without authority, query, or fragment are accepted.");
        return {};
    }
    const QString localPath = url.toLocalFile();
    QString canonicalPath;
    if (localPath.isEmpty() || !isSafeProjectFile(localPath, &canonicalPath)) {
        errorMessage =
            tr("A workspace edit targeted a symlink or a file outside the project root.");
        return {};
    }
    return canonicalPath;
}

bool ClangdClient::isSafeProjectFile(const QString& filePath, QString* canonicalPath) const {
    const QFileInfo rootInfo(m_projectRoot);
    const QFileInfo candidateInfo(filePath);
    if (!rootInfo.isDir() || !candidateInfo.exists() || !candidateInfo.isFile() ||
        isLinkLikePath(filePath)) {
        return false;
    }
    const QString root = QDir::cleanPath(rootInfo.canonicalFilePath());
    const QString candidate = QDir::cleanPath(candidateInfo.canonicalFilePath());
    if (root.isEmpty() || candidate.isEmpty()) {
        return false;
    }
    QString comparisonRoot = QDir::fromNativeSeparators(root);
    QString comparisonCandidate = QDir::fromNativeSeparators(candidate);
#ifdef Q_OS_WIN
    comparisonRoot = comparisonRoot.toCaseFolded();
    comparisonCandidate = comparisonCandidate.toCaseFolded();
#endif
    if (!comparisonRoot.endsWith(QLatin1Char('/'))) {
        comparisonRoot += QLatin1Char('/');
    }
    if (!comparisonCandidate.startsWith(comparisonRoot)) {
        return false;
    }

    const QString lexicalRelative = QDir(root).relativeFilePath(candidateInfo.absoluteFilePath());
    const QStringList parts =
        QDir::fromNativeSeparators(lexicalRelative).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString current = root;
    for (const QString& part : parts) {
        if (part == QStringLiteral(".") || part == QStringLiteral("..")) {
            return false;
        }
        current = QDir(current).filePath(part);
        if (isLinkLikePath(current)) {
            return false;
        }
    }
    if (canonicalPath != nullptr) {
        *canonicalPath = candidate;
    }
    return true;
}

QString ClangdClient::documentUri(const QString& filePath) const {
    return QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded);
}

QString ClangdClient::normalizedPath(const QString& filePath) const {
    QString path = QDir::fromNativeSeparators(QDir::cleanPath(filePath));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

ClangdLocations ClangdClient::parseLocations(const QJsonValue& value) const {
    QJsonArray values;
    if (value.isArray()) {
        values = value.toArray();
    } else if (value.isObject()) {
        values.push_back(value);
    }
    ClangdLocations locations;
    const qsizetype count = std::min(values.size(), MaximumLocations);
    locations.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        const QJsonValue locationValue = values.at(index);
        if (!locationValue.isObject()) {
            continue;
        }
        const QJsonObject object = locationValue.toObject();
        const QString uri = locationUri(object);
        const QJsonValue rangeValue = locationRange(object);
        if (uri.isEmpty() || !rangeValue.isObject()) {
            continue;
        }
        QString pathError;
        const QString path = safeFilePathFromUri(uri, pathError);
        if (path.isEmpty()) {
            continue;
        }
        const QJsonValue start = rangeValue.toObject().value(QStringLiteral("start"));
        ClangdLocation location;
        location.filePath = path;
        if (!parsePosition(start, location.line, location.character)) {
            continue;
        }
        locations.push_back(std::move(location));
    }
    return locations;
}

QString ClangdClient::hoverText(const QJsonValue& value) {
    if (!value.isObject()) {
        return {};
    }
    const QJsonValue contents = value.toObject().value(QStringLiteral("contents"));
    QString text;
    if (contents.isArray()) {
        const QJsonArray parts = contents.toArray();
        for (const QJsonValue& part : parts) {
            const QString piece = markedStringText(part);
            if (!piece.isEmpty()) {
                if (!text.isEmpty()) {
                    text += QStringLiteral("\n\n");
                }
                text += piece;
            }
            if (text.toUtf8().size() > MaximumDisplayTextBytes) {
                break;
            }
        }
    } else {
        text = markedStringText(contents);
    }
    if (text.toUtf8().size() > MaximumDisplayTextBytes) {
        text.truncate(MaximumDisplayTextBytes);
    }
    return text;
}

QString ClangdClient::languageId(const QString& filePath) {
    const QString suffix = QFileInfo(filePath).suffix().toCaseFolded();
    return suffix == QStringLiteral("c") ? QStringLiteral("c") : QStringLiteral("cpp");
}

} // namespace fgl::studio
