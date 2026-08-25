#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

class QProcess;
class QTimer;

namespace fgl::studio {

struct ClangdLocation final {
    QString filePath;
    int line = 0;
    int character = 0;
};

using ClangdLocations = QList<ClangdLocation>;

struct ClangdDiagnostic final {
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    int severity = 1;
    QString message;
};

using ClangdDiagnostics = QList<ClangdDiagnostic>;

class JsonRpcMessageParser final {
  public:
    static constexpr qsizetype MaximumHeaderBytes = 8 * 1024;
    static constexpr qsizetype MaximumPayloadBytes = 4 * 1024 * 1024;
    static constexpr qsizetype MaximumBufferedBytes = MaximumHeaderBytes + MaximumPayloadBytes;
    static constexpr int MaximumMessagesPerFeed = 256;

    struct FeedResult final {
        QList<QJsonObject> messages;
        QString errorMessage;

        [[nodiscard]] bool ok() const noexcept {
            return errorMessage.isEmpty();
        }
    };

    [[nodiscard]] FeedResult feed(const QByteArray& bytes);
    void reset() noexcept;
    [[nodiscard]] qsizetype bufferedBytes() const noexcept;

  private:
    [[nodiscard]] FeedResult fail(const QString& message);

    QByteArray m_buffer;
    std::optional<qsizetype> m_expectedPayloadBytes;
};

class ClangdClient final : public QObject {
    Q_OBJECT

  public:
    enum class State {
        Stopped,
        Starting,
        Initializing,
        Ready,
        ShuttingDown,
        Failed,
    };
    Q_ENUM(State)

    // Keeps even worst-case JSON string escaping below the 4 MiB protocol frame limit.
    static constexpr qsizetype MaximumDocumentBytes = 512 * 1024;
    static constexpr qsizetype MaximumWorkspaceReplacementBytes = 4 * 1024 * 1024;
    static constexpr int MaximumOpenDocuments = 128;
    static constexpr int MaximumPendingRequests = 128;
    static constexpr int MaximumWorkspaceFiles = 64;
    static constexpr int MaximumWorkspaceEdits = 4096;

    explicit ClangdClient(QObject* parent = nullptr);
    ~ClangdClient() override;

    ClangdClient(const ClangdClient&) = delete;
    ClangdClient& operator=(const ClangdClient&) = delete;

    void configureServer(const QString& program, const QStringList& arguments = {});
    [[nodiscard]] QString serverProgram() const;
    [[nodiscard]] QStringList serverArguments() const;
    [[nodiscard]] bool start(const QString& projectRoot);
    void stop();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] QString projectRoot() const;
    [[nodiscard]] qsizetype openDocumentCount() const noexcept;
    [[nodiscard]] qsizetype pendingRequestCount() const noexcept;

    [[nodiscard]] bool openDocument(const QString& filePath, const QString& text);
    [[nodiscard]] bool changeDocument(const QString& filePath, const QString& text);
    void closeDocument(const QString& filePath);

    [[nodiscard]] qint64 requestCompletion(const QString& filePath, int line, int character);
    [[nodiscard]] qint64 requestDefinition(const QString& filePath, int line, int character);
    [[nodiscard]] qint64 requestReferences(const QString& filePath, int line, int character);
    [[nodiscard]] qint64 requestHover(const QString& filePath, int line, int character);
    [[nodiscard]] qint64 requestRename(const QString& filePath, int line, int character,
                                       const QString& newName);
    [[nodiscard]] qint64 requestFormatting(const QString& filePath, int tabSize = 4,
                                           bool insertSpaces = true);

    [[nodiscard]] bool applyWorkspaceEdit(const QJsonObject& workspaceEdit, QString& errorMessage);

  signals:
    void stateChanged(fgl::studio::ClangdClient::State state);
    void readyChanged(bool ready);
    void statusMessage(const QString& message);
    void completionReady(const QString& filePath, const QStringList& completionItems);
    void locationsReady(const QString& operation, const fgl::studio::ClangdLocations& locations);
    void hoverReady(const QString& filePath, const QString& contents);
    void diagnosticsPublished(const QString& filePath,
                              const fgl::studio::ClangdDiagnostics& diagnostics);
    void workspaceEditFinished(const QString& operation, bool applied, const QString& message);
    void fileContentChanged(const QString& filePath, const QString& contents);

  private:
    enum class RequestKind {
        Initialize,
        Shutdown,
        Completion,
        Definition,
        References,
        Hover,
        Rename,
        Formatting,
    };

    struct PendingRequest final {
        RequestKind kind = RequestKind::Completion;
        QString filePath;
    };

    struct OpenDocument final {
        QString filePath;
        QString text;
        int version = 1;
    };

    void setState(State state, const QString& status = {});
    [[nodiscard]] bool sendMessage(const QJsonObject& message);
    [[nodiscard]] bool sendNotification(const QString& method, const QJsonObject& params = {});
    [[nodiscard]] qint64 sendRequest(RequestKind kind, const QString& method,
                                     const QJsonObject& params, const QString& filePath = {});
    void sendResponse(const QJsonValue& id, const QJsonValue& result);
    void sendErrorResponse(const QJsonValue& id, int code, const QString& message);
    void handleReadyRead();
    void handleMessage(const QJsonObject& message);
    void handleResponse(const QJsonObject& message);
    void handleServerCall(const QJsonObject& message);
    void handleNotification(const QJsonObject& message);
    void handleProcessStarted();
    void handleProcessError();
    void handleProcessFinished(int exitCode);
    void handleRequestTimeout(qint64 requestId);
    void finishShutdown();
    void failProtocol(const QString& message);

    [[nodiscard]] QJsonObject documentPositionParams(const QString& filePath, int line,
                                                     int character) const;
    [[nodiscard]] QString safeFilePathFromUri(const QString& uri, QString& errorMessage) const;
    [[nodiscard]] bool isSafeProjectFile(const QString& filePath,
                                         QString* canonicalPath = nullptr) const;
    [[nodiscard]] QString documentUri(const QString& filePath) const;
    [[nodiscard]] QString normalizedPath(const QString& filePath) const;
    [[nodiscard]] ClangdLocations parseLocations(const QJsonValue& value) const;
    [[nodiscard]] static QString hoverText(const QJsonValue& value);
    [[nodiscard]] static QString languageId(const QString& filePath);

    QProcess* m_process = nullptr;
    QTimer* m_shutdownTimer = nullptr;
    JsonRpcMessageParser m_parser;
    State m_state = State::Stopped;
    QString m_program = QStringLiteral("clangd");
    QStringList m_arguments = {QStringLiteral("--background-index"), QStringLiteral("--clang-tidy"),
                               QStringLiteral("--header-insertion=never")};
    QString m_projectRoot;
    QString m_pendingRestartRoot;
    QHash<QString, OpenDocument> m_openDocuments;
    QHash<qint64, PendingRequest> m_pendingRequests;
    QByteArray m_standardErrorTail;
    qint64 m_nextRequestId = 1;
    bool m_destroying = false;
};

} // namespace fgl::studio

Q_DECLARE_METATYPE(fgl::studio::ClangdLocation)
Q_DECLARE_METATYPE(fgl::studio::ClangdLocations)
Q_DECLARE_METATYPE(fgl::studio::ClangdDiagnostic)
Q_DECLARE_METATYPE(fgl::studio::ClangdDiagnostics)
