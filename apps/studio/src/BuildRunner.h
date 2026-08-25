#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace fgl::studio {

enum class BuildOutputSeverity { Info, Warning, Error };

class BuildRunner final : public QObject {
    Q_OBJECT

  public:
    explicit BuildRunner(QObject* parent = nullptr);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString program() const;
    [[nodiscard]] QStringList arguments() const;
    [[nodiscard]] QByteArray standardOutput() const;
    [[nodiscard]] QByteArray standardError() const;
    [[nodiscard]] qint64 writeInput(const QByteArray& bytes);
    [[nodiscard]] static BuildOutputSeverity classifyDiagnosticLine(const QString& line);

  public slots:
    void startBuild(const QString& program, const QStringList& arguments,
                    const QString& workingDirectory);
    void stopBuild();

  signals:
    void outputReady(const QString& text, bool standardError);
    void classifiedOutputReady(const QString& text, fgl::studio::BuildOutputSeverity severity);
    void buildStarted(const QString& program, const QStringList& arguments,
                      const QString& workingDirectory);
    void buildFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void runningChanged(bool running);

  private:
    void readStandardOutput();
    void readStandardError();
    void processError(QProcess::ProcessError error);
    void classifyAndEmit(const QByteArray& bytes, QByteArray& pending);
    void flushPendingDiagnostics();
    void emitSyntheticDiagnostic(const QString& text, BuildOutputSeverity severity);

    QProcess m_process;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    QByteArray m_pendingStandardOutput;
    QByteArray m_pendingStandardError;
    bool m_failedToStartReported = false;
};

} // namespace fgl::studio

Q_DECLARE_METATYPE(fgl::studio::BuildOutputSeverity)
