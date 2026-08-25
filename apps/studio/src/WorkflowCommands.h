#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fgl::studio {

struct ProcessCommand final {
    QString operation;
    QString program;
    QStringList arguments;
    QString workingDirectory;

    [[nodiscard]] bool isValid() const noexcept;
};

struct SerialPortCandidate final {
    QString port;
    QString displayName;
    QString confidence;
    QString reason;
    bool boardCandidate = false;
    bool requiresUserConfirmation = true;
};

struct Esp32DeploymentSummary final {
    QString port;
    QString diagnosticCheck;
    QString diagnosticResult;
    QString memoryMap;
    quint64 binaryBytes = 0;
    quint64 programStorageBytes = 0;
    quint64 globalStaticRamBytes = 0;
    bool selectedPortDetected = false;
    bool manualVerificationPending = true;
};

class WorkflowCommands final {
  public:
    static constexpr auto BoardProfile = "olimex-esp32-sbc-fabgl-revb";

    [[nodiscard]] static ProcessCommand
    projectBuild(const QString& repositoryRoot, const QString& projectFile, const QString& target,
                 const QString& configuration, const QString& outputRoot);
    [[nodiscard]] static ProcessCommand
    esp32BuildUploadDiagnostics(const QString& repositoryRoot, const QString& projectFile,
                                const QString& configuration, const QString& outputRoot,
                                const QString& port, int baud, const QString& diagnosticCheck,
                                int diagnosticDurationSeconds = 12);
    [[nodiscard]] static bool
    parseProjectBuildResult(const QByteArray& bytes, const QString& expectedProjectGuid,
                            const QString& expectedProjectFile, const QString& expectedTarget,
                            const QString& expectedOutputRoot, const QString& expectedScriptRoot,
                            QString& scriptModule, QString& esp32BuildResult,
                            QString& errorMessage);
    [[nodiscard]] static bool
    parseEsp32DeploymentResult(const QByteArray& bytes, const QString& expectedProjectGuid,
                               const QString& expectedProjectFile,
                               const QString& expectedOutputRoot, const QString& expectedPort,
                               const QString& expectedDiagnosticCheck, QString& esp32BuildResult,
                               Esp32DeploymentSummary& summary, QString& errorMessage);
    [[nodiscard]] static ProcessCommand pcBuild(const QString& repositoryRoot,
                                                const QString& projectFile,
                                                const QString& configuration);
    [[nodiscard]] static ProcessCommand pcPlay(const QString& playerExecutable,
                                               const QString& projectFile,
                                               const QString& workingDirectory,
                                               const QString& scriptModule = {});
    [[nodiscard]] static bool parsePcBuildResult(const QByteArray& bytes,
                                                 const QString& expectedProjectGuid,
                                                 const QString& expectedOutputRoot,
                                                 QString& scriptModule, QString& errorMessage);
    [[nodiscard]] static ProcessCommand esp32Export(const QString& projectCliExecutable,
                                                    const QString& projectFile,
                                                    const QString& firmwareTemplate,
                                                    const QString& outputSketch,
                                                    const QString& workingDirectory);
    [[nodiscard]] static ProcessCommand esp32Build(const QString& repositoryRoot,
                                                   const QString& sketchDirectory,
                                                   const QString& outputRoot, bool enablePsram);
    [[nodiscard]] static ProcessCommand detectSerialPorts(const QString& repositoryRoot,
                                                          const QString& fixturePath = {});
    [[nodiscard]] static ProcessCommand
    uploadEsp32(const QString& repositoryRoot, const QString& port, const QString& buildResultPath);
    [[nodiscard]] static ProcessCommand serialMonitor(const QString& repositoryRoot,
                                                      const QString& port, int baud);

    [[nodiscard]] static bool isSafeSerialPort(const QString& port);
    [[nodiscard]] static bool parseSerialPortReport(const QByteArray& bytes,
                                                    QVector<SerialPortCandidate>& candidates,
                                                    QString& errorMessage);

  private:
    WorkflowCommands() = delete;

    [[nodiscard]] static QString powerShellProgram();
    [[nodiscard]] static QStringList powerShellScriptArguments(const QString& scriptPath);
};

} // namespace fgl::studio
