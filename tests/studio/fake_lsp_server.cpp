#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstring>
#include <optional>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {

constexpr qsizetype MaximumMessageBytes = 4 * 1024 * 1024;

QByteArray frame(const QJsonObject& object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return QByteArrayLiteral("Content-Length: ") + QByteArray::number(payload.size()) +
           QByteArrayLiteral("\r\n\r\n") + payload;
}

bool writeAll(const QByteArray& bytes) {
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const qsizetype remaining = bytes.size() - offset;
        const std::size_t written =
            std::fwrite(bytes.constData() + offset, 1, static_cast<std::size_t>(remaining), stdout);
        if (written == 0U) {
            return false;
        }
        offset += static_cast<qsizetype>(written);
    }
    return std::fflush(stdout) == 0;
}

void sendObject(const QJsonObject& object) {
    const QByteArray bytes = frame(object);
    (void)writeAll(bytes);
}

void sendResult(const QJsonValue& id, const QJsonValue& result) {
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    sendObject(response);
}

QJsonObject position(const int line, const int character) {
    return QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), character}};
}

QJsonObject range(const int startLine, const int startCharacter, const int endLine,
                  const int endCharacter) {
    return QJsonObject{{QStringLiteral("start"), position(startLine, startCharacter)},
                       {QStringLiteral("end"), position(endLine, endCharacter)}};
}

void appendLog(FILE* log, const QString& entry) {
    const QByteArray bytes = entry.toUtf8() + '\n';
    (void)std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), log);
    (void)std::fflush(log);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3 ||
        application.arguments().at(1) != QStringLiteral("--log")) {
        return 64;
    }

#ifdef Q_OS_WIN
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif

    FILE* log = nullptr;
#ifdef Q_OS_WIN
    if (_wfopen_s(&log, reinterpret_cast<const wchar_t*>(application.arguments().at(2).utf16()),
                  L"wb") != 0 ||
        log == nullptr) {
        return 66;
    }
#else
    log = std::fopen(application.arguments().at(2).toUtf8().constData(), "wb");
    if (log == nullptr) {
        return 66;
    }
#endif
    appendLog(log, QStringLiteral("cwd=") + QDir::currentPath());

    QByteArray buffer;
    std::optional<qsizetype> expectedBytes;
    QString documentUri;
    bool receivedExit = false;
    while (!receivedExit) {
        char byte = '\0';
        const std::size_t read = std::fread(&byte, 1, 1, stdin);
        if (read == 0U) {
            break;
        }
        if (buffer.size() >= MaximumMessageBytes) {
            return 67;
        }
        buffer.append(byte);

        while (true) {
            if (!expectedBytes.has_value()) {
                const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
                if (headerEnd < 0) {
                    break;
                }
                const QByteArray header = buffer.left(headerEnd);
                const QList<QByteArray> lines = header.split('\n');
                for (QByteArray line : lines) {
                    line = line.trimmed();
                    if (!line.toLower().startsWith("content-length:")) {
                        continue;
                    }
                    bool converted = false;
                    const qlonglong length =
                        line.mid(line.indexOf(':') + 1).trimmed().toLongLong(&converted, 10);
                    if (!converted || length < 0 || length > MaximumMessageBytes) {
                        return 68;
                    }
                    expectedBytes = static_cast<qsizetype>(length);
                }
                if (!expectedBytes.has_value()) {
                    return 69;
                }
                buffer.remove(0, headerEnd + 4);
            }
            if (buffer.size() < *expectedBytes) {
                break;
            }
            const QByteArray payload = buffer.left(*expectedBytes);
            buffer.remove(0, *expectedBytes);
            expectedBytes.reset();
            const QJsonDocument parsed = QJsonDocument::fromJson(payload);
            if (!parsed.isObject()) {
                return 70;
            }
            const QJsonObject message = parsed.object();
            const QString method = message.value(QStringLiteral("method")).toString();
            const QJsonValue id = message.value(QStringLiteral("id"));
            const QJsonObject params = message.value(QStringLiteral("params")).toObject();
            if (!method.isEmpty()) {
                appendLog(log, method);
            }

            if (method == QStringLiteral("initialize")) {
                sendResult(id, QJsonObject{{QStringLiteral("capabilities"), QJsonObject{}}});
            } else if (method == QStringLiteral("textDocument/didOpen")) {
                documentUri = params.value(QStringLiteral("textDocument"))
                                  .toObject()
                                  .value(QStringLiteral("uri"))
                                  .toString();
                QJsonObject diagnostic;
                diagnostic.insert(QStringLiteral("range"), range(0, 0, 0, 3));
                diagnostic.insert(QStringLiteral("severity"), 2);
                diagnostic.insert(QStringLiteral("message"),
                                  QStringLiteral("deterministic fake diagnostic"));
                QJsonObject diagnosticParams;
                diagnosticParams.insert(QStringLiteral("uri"), documentUri);
                diagnosticParams.insert(QStringLiteral("diagnostics"), QJsonArray{diagnostic});
                QJsonObject notification;
                notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
                notification.insert(QStringLiteral("method"),
                                    QStringLiteral("textDocument/publishDiagnostics"));
                notification.insert(QStringLiteral("params"), diagnosticParams);
                sendObject(notification);
            } else if (method == QStringLiteral("textDocument/completion")) {
                const QJsonArray items = {
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Alpha")}},
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Beta")}}};
                sendResult(id, QJsonObject{{QStringLiteral("isIncomplete"), false},
                                           {QStringLiteral("items"), items}});
            } else if (method == QStringLiteral("textDocument/definition")) {
                sendResult(id, QJsonObject{{QStringLiteral("uri"), documentUri},
                                           {QStringLiteral("range"), range(0, 0, 0, 3)}});
            } else if (method == QStringLiteral("textDocument/references")) {
                const QJsonObject location = {{QStringLiteral("uri"), documentUri},
                                              {QStringLiteral("range"), range(0, 0, 0, 3)}};
                sendResult(id, QJsonArray{location, location});
            } else if (method == QStringLiteral("textDocument/hover")) {
                sendResult(
                    id,
                    QJsonObject{
                        {QStringLiteral("contents"),
                         QJsonObject{{QStringLiteral("kind"), QStringLiteral("markdown")},
                                     {QStringLiteral("value"), QStringLiteral("fake hover")}}}});
            } else if (method == QStringLiteral("textDocument/rename")) {
                const QString newName = params.value(QStringLiteral("newName")).toString();
                QJsonObject changes;
                changes.insert(documentUri,
                               QJsonArray{QJsonObject{{QStringLiteral("range"), range(0, 0, 0, 3)},
                                                      {QStringLiteral("newText"), newName}}});
                sendResult(id, QJsonObject{{QStringLiteral("changes"), changes}});
            } else if (method == QStringLiteral("textDocument/formatting")) {
                sendResult(id, QJsonArray{QJsonObject{
                                   {QStringLiteral("range"), range(0, 0, 0, 0)},
                                   {QStringLiteral("newText"), QStringLiteral("// formatted\n")}}});
            } else if (method == QStringLiteral("shutdown")) {
                sendResult(id, QJsonValue::Null);
            } else if (method == QStringLiteral("exit")) {
                receivedExit = true;
                break;
            } else if (!id.isUndefined()) {
                sendResult(id, QJsonValue::Null);
            }
        }
    }
    (void)std::fclose(log);
    return receivedExit ? 0 : 71;
}
