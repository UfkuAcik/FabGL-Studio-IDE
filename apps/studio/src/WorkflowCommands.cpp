#include "WorkflowCommands.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QRegularExpression>

#include <utility>

namespace fgl::studio {

bool ProcessCommand::isValid() const noexcept {
    return !operation.trimmed().isEmpty() && !program.trimmed().isEmpty() &&
           !workingDirectory.trimmed().isEmpty();
}

QString WorkflowCommands::powerShellProgram() {
#ifdef Q_OS_WIN
    return QStringLiteral("powershell.exe");
#else
    return QStringLiteral("pwsh");
#endif
}

QStringList WorkflowCommands::powerShellScriptArguments(const QString& scriptPath) {
    return {QStringLiteral("-NoLogo"),         QStringLiteral("-NoProfile"),
            QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
            QStringLiteral("Bypass"),          QStringLiteral("-File"),
            QDir::cleanPath(scriptPath)};
}

ProcessCommand WorkflowCommands::projectBuild(const QString& repositoryRoot,
                                              const QString& projectFile, const QString& target,
                                              const QString& configuration,
                                              const QString& outputRoot) {
    if (target != QStringLiteral("Pc") && target != QStringLiteral("Esp32")) {
        return {};
    }
    auto arguments = powerShellScriptArguments(
        QDir(repositoryRoot).filePath(QStringLiteral("scripts/build_project.ps1")));
    arguments << QStringLiteral("-ProjectPath") << QDir::cleanPath(projectFile)
              << QStringLiteral("-Target") << target << QStringLiteral("-OutputRoot")
              << QDir::cleanPath(outputRoot);
    if (target == QStringLiteral("Pc")) {
        if (configuration != QStringLiteral("Debug") &&
            configuration != QStringLiteral("Release")) {
            return {};
        }
        arguments << QStringLiteral("-Configuration") << configuration;
    } else {
        auto profile = configuration;
        if (configuration == QStringLiteral("ReleasePsram")) {
            profile = QStringLiteral("Release");
            arguments << QStringLiteral("-EnablePsram");
        }
        static const QStringList Esp32Profiles{QStringLiteral("Debug"), QStringLiteral("Release"),
                                               QStringLiteral("SizeOptimized"),
                                               QStringLiteral("PerformanceOptimized")};
        if (!Esp32Profiles.contains(profile)) {
            return {};
        }
        arguments << QStringLiteral("-Esp32BuildProfile") << profile << QStringLiteral("-Clean");
    }
    return {target == QStringLiteral("Pc")
                ? QStringLiteral("Unified PC project build (%1)").arg(configuration)
                : QStringLiteral("Unified ESP32 project build (%1)").arg(configuration),
            powerShellProgram(), arguments, QDir::cleanPath(repositoryRoot)};
}

ProcessCommand WorkflowCommands::esp32BuildUploadDiagnostics(
    const QString& repositoryRoot, const QString& projectFile, const QString& configuration,
    const QString& outputRoot, const QString& port, const int baud, const QString& diagnosticCheck,
    const int diagnosticDurationSeconds) {
    static const QStringList DiagnosticChecks{
        QStringLiteral("all"),   QStringLiteral("vga"),       QStringLiteral("keyboard"),
        QStringLiteral("mouse"), QStringLiteral("audio"),     QStringLiteral("sd"),
        QStringLiteral("psram"), QStringLiteral("frame-rate")};
    if (!isSafeSerialPort(port) || baud < 1200 || baud > 3000000 ||
        !DiagnosticChecks.contains(diagnosticCheck) || diagnosticDurationSeconds < 3 ||
        diagnosticDurationSeconds > 300) {
        return {};
    }
    auto command = projectBuild(repositoryRoot, projectFile, QStringLiteral("Esp32"), configuration,
                                outputRoot);
    if (!command.isValid()) {
        return {};
    }
    command.operation = QObject::tr("ESP32 build, upload, and %1 diagnostics (%2)")
                            .arg(diagnosticCheck, configuration);
    command.arguments << QStringLiteral("-Upload") << QStringLiteral("-RuntimeDiagnostics")
                      << QStringLiteral("-Port") << port << QStringLiteral("-ConfirmBoardProfile")
                      << QString::fromLatin1(BoardProfile) << QStringLiteral("-MonitorBaud")
                      << QString::number(baud) << QStringLiteral("-DiagnosticCheck")
                      << diagnosticCheck << QStringLiteral("-DiagnosticDurationSeconds")
                      << QString::number(diagnosticDurationSeconds);
    if ((diagnosticCheck == QStringLiteral("psram") || diagnosticCheck == QStringLiteral("all")) &&
        !command.arguments.contains(QStringLiteral("-EnablePsram"))) {
        command.arguments << QStringLiteral("-EnablePsram");
    }
    return command;
}

bool WorkflowCommands::parseProjectBuildResult(
    const QByteArray& bytes, const QString& expectedProjectGuid, const QString& expectedProjectFile,
    const QString& expectedTarget, const QString& expectedOutputRoot,
    const QString& expectedScriptRoot, QString& scriptModule, QString& esp32BuildResult,
    QString& errorMessage) {
    scriptModule.clear();
    esp32BuildResult.clear();
    constexpr qsizetype MaximumBuildResultBytes = 1024 * 1024;
    if (bytes.isEmpty() || bytes.size() > MaximumBuildResultBytes || bytes.contains('\0')) {
        errorMessage =
            QObject::tr("Project build result is empty, contains NUL, or exceeds 1 MiB.");
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QObject::tr("Invalid project build-result JSON at byte %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString());
        return false;
    }
    const auto root = document.object();
    const QString expectedGuid = expectedProjectGuid.toLower();
    static const QRegularExpression CanonicalGuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1 ||
        root.value(QStringLiteral("kind")).toString() !=
            QStringLiteral("FabGLStudioProjectBuildResult") ||
        !root.value(QStringLiteral("success")).isBool() ||
        !root.value(QStringLiteral("success")).toBool() ||
        root.value(QStringLiteral("dryRun")).toBool(true) ||
        !CanonicalGuid.match(expectedGuid).hasMatch() ||
        root.value(QStringLiteral("projectGuid")).toString().toLower() != expectedGuid ||
        root.value(QStringLiteral("target")).toString() != expectedTarget) {
        errorMessage = QObject::tr("Project build result has an unsupported schema, kind, status, "
                                   "project GUID, or target.");
        return false;
    }

    const QFileInfo expectedProjectInfo(expectedProjectFile);
    const QFileInfo resultProjectInfo(root.value(QStringLiteral("project")).toString());
    if (expectedProjectInfo.canonicalFilePath().isEmpty() ||
        resultProjectInfo.canonicalFilePath() != expectedProjectInfo.canonicalFilePath()) {
        errorMessage = QObject::tr("Project build result belongs to a different project path.");
        return false;
    }

    const QFileInfo outputRootInfo(expectedOutputRoot);
    const QString canonicalOutputRoot = outputRootInfo.canonicalFilePath();
    if (canonicalOutputRoot.isEmpty() || !outputRootInfo.isDir() || outputRootInfo.isSymLink()) {
        errorMessage = QObject::tr("Expected project build output root is missing or unsafe.");
        return false;
    }
    const auto isInside = [](const QString& child, const QString& rootPath) {
        QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
        QString normalizedChild = QDir::fromNativeSeparators(QDir::cleanPath(child));
        if (!normalizedRoot.endsWith(QLatin1Char('/'))) {
            normalizedRoot += QLatin1Char('/');
        }
#ifdef Q_OS_WIN
        constexpr auto PathCaseSensitivity = Qt::CaseInsensitive;
#else
        constexpr auto PathCaseSensitivity = Qt::CaseSensitive;
#endif
        return normalizedChild.startsWith(normalizedRoot, PathCaseSensitivity);
    };

    if (expectedTarget == QStringLiteral("Pc")) {
        const auto pc = root.value(QStringLiteral("pc")).toObject();
        const auto scripts = root.value(QStringLiteral("nativeScripts")).toObject();
        if (pc.isEmpty() || !pc.value(QStringLiteral("success")).toBool(false) ||
            scripts.isEmpty() || !scripts.value(QStringLiteral("built")).isBool()) {
            errorMessage =
                QObject::tr("PC project build result has no valid runtime smoke result.");
            return false;
        }
        if (scripts.value(QStringLiteral("built")).toBool()) {
            const QFileInfo scriptRootInfo(expectedScriptRoot);
            const QFileInfo moduleInfo(scripts.value(QStringLiteral("module")).toString());
            const QString canonicalScriptRoot = scriptRootInfo.canonicalFilePath();
            const QString canonicalModule = moduleInfo.canonicalFilePath();
            const QString expectedHash =
                scripts.value(QStringLiteral("moduleSha256")).toString().toLower();
            if (canonicalScriptRoot.isEmpty() || !scriptRootInfo.isDir() ||
                scriptRootInfo.isSymLink() || canonicalModule.isEmpty() ||
                !moduleInfo.isAbsolute() || !moduleInfo.isFile() || moduleInfo.isSymLink() ||
                !isInside(canonicalModule, canonicalScriptRoot) ||
                !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                     .match(expectedHash)
                     .hasMatch()) {
                errorMessage = QObject::tr("Native module in the project build result is missing, "
                                           "unsafe, or outside its build root.");
                return false;
            }
            QFile moduleFile(canonicalModule);
            constexpr qint64 MaximumNativeModuleBytes = 256LL * 1024LL * 1024LL;
            QCryptographicHash moduleHash(QCryptographicHash::Sha256);
            if (!moduleFile.open(QIODevice::ReadOnly) || moduleFile.size() <= 0 ||
                moduleFile.size() > MaximumNativeModuleBytes || !moduleHash.addData(&moduleFile) ||
                QString::fromLatin1(moduleHash.result().toHex()) != expectedHash) {
                errorMessage =
                    QObject::tr("Native module SHA-256 does not match the build result.");
                return false;
            }
            scriptModule = canonicalModule;
        }
    } else if (expectedTarget == QStringLiteral("Esp32")) {
        const auto esp32 = root.value(QStringLiteral("esp32")).toObject();
        const QFileInfo nestedResultInfo(esp32.value(QStringLiteral("result")).toString());
        const QString canonicalNestedResult = nestedResultInfo.canonicalFilePath();
        if (esp32.isEmpty() || !esp32.value(QStringLiteral("success")).toBool(false) ||
            canonicalNestedResult.isEmpty() || !nestedResultInfo.isAbsolute() ||
            !nestedResultInfo.isFile() || nestedResultInfo.isSymLink() ||
            !isInside(canonicalNestedResult, canonicalOutputRoot)) {
            errorMessage = QObject::tr(
                "ESP32 project build result is missing or escapes the expected output root.");
            return false;
        }
        esp32BuildResult = canonicalNestedResult;
    } else {
        errorMessage = QObject::tr("Unsupported expected project build target.");
        return false;
    }
    errorMessage.clear();
    return true;
}

bool WorkflowCommands::parseEsp32DeploymentResult(
    const QByteArray& bytes, const QString& expectedProjectGuid, const QString& expectedProjectFile,
    const QString& expectedOutputRoot, const QString& expectedPort,
    const QString& expectedDiagnosticCheck, QString& esp32BuildResult,
    Esp32DeploymentSummary& summary, QString& errorMessage) {
    summary = {};
    QString unusedScriptModule;
    if (!isSafeSerialPort(expectedPort) ||
        !parseProjectBuildResult(bytes, expectedProjectGuid, expectedProjectFile,
                                 QStringLiteral("Esp32"), expectedOutputRoot, QString{},
                                 unusedScriptModule, esp32BuildResult, errorMessage)) {
        return false;
    }

    QJsonParseError parseError;
    const auto root = QJsonDocument::fromJson(bytes, &parseError).object();
    const auto upload = root.value(QStringLiteral("upload")).toObject();
    const auto detection = root.value(QStringLiteral("portDetection")).toObject();
    const auto monitor = root.value(QStringLiteral("monitor")).toObject();
    const auto diagnostics = root.value(QStringLiteral("runtimeDiagnostics")).toObject();
    const auto esp32 = root.value(QStringLiteral("esp32")).toObject();
    if (!upload.value(QStringLiteral("requested")).toBool(false) ||
        !upload.value(QStringLiteral("performed")).toBool(false) ||
        upload.value(QStringLiteral("port")).toString() != expectedPort ||
        upload.value(QStringLiteral("boardProfile")).toString() !=
            QString::fromLatin1(BoardProfile) ||
        !detection.value(QStringLiteral("performed")).toBool(false) ||
        !detection.value(QStringLiteral("readOnly")).toBool(false) ||
        detection.value(QStringLiteral("selectedPort")).toString() != expectedPort ||
        !monitor.value(QStringLiteral("requested")).toBool(false) ||
        !monitor.value(QStringLiteral("performed")).toBool(false) ||
        !monitor.value(QStringLiteral("bounded")).toBool(false) ||
        !diagnostics.value(QStringLiteral("requested")).toBool(false) ||
        !diagnostics.value(QStringLiteral("performed")).toBool(false) ||
        diagnostics.value(QStringLiteral("diagnosticCheck")).toString() !=
            expectedDiagnosticCheck ||
        diagnostics.value(QStringLiteral("automatedResult")).toString() != QStringLiteral("PASS") ||
        diagnostics.value(QStringLiteral("hardwareVerified")).toBool(true)) {
        errorMessage =
            QObject::tr("ESP32 deployment result does not prove detection, guarded upload, bounded "
                        "serial capture, and automated diagnostics for the requested port/check.");
        return false;
    }

    const QFileInfo outputRootInfo(expectedOutputRoot);
    const QString canonicalOutputRoot = outputRootInfo.canonicalFilePath();
    const auto isInside = [](const QString& child, const QString& rootPath) {
        QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
        QString normalizedChild = QDir::fromNativeSeparators(QDir::cleanPath(child));
        if (!normalizedRoot.endsWith(QLatin1Char('/'))) {
            normalizedRoot += QLatin1Char('/');
        }
#ifdef Q_OS_WIN
        constexpr auto PathCaseSensitivity = Qt::CaseInsensitive;
#else
        constexpr auto PathCaseSensitivity = Qt::CaseSensitive;
#endif
        return normalizedChild.startsWith(normalizedRoot, PathCaseSensitivity);
    };
    const QFileInfo uploadResultInfo(upload.value(QStringLiteral("result")).toString());
    const QFileInfo diagnosticResultInfo(diagnostics.value(QStringLiteral("result")).toString());
    const QFileInfo memoryMapInfo(esp32.value(QStringLiteral("map")).toString());
    const QString canonicalUploadResult = uploadResultInfo.canonicalFilePath();
    const QString canonicalDiagnosticResult = diagnosticResultInfo.canonicalFilePath();
    const QString canonicalMemoryMap = memoryMapInfo.canonicalFilePath();
    if (canonicalOutputRoot.isEmpty() || canonicalUploadResult.isEmpty() ||
        canonicalDiagnosticResult.isEmpty() || canonicalMemoryMap.isEmpty() ||
        !uploadResultInfo.isFile() || uploadResultInfo.isSymLink() ||
        !diagnosticResultInfo.isFile() || diagnosticResultInfo.isSymLink() ||
        !memoryMapInfo.isFile() || memoryMapInfo.isSymLink() || memoryMapInfo.size() <= 0 ||
        !isInside(canonicalUploadResult, canonicalOutputRoot) ||
        !isInside(canonicalDiagnosticResult, canonicalOutputRoot) ||
        !isInside(canonicalMemoryMap, canonicalOutputRoot)) {
        errorMessage = QObject::tr(
            "ESP32 deployment evidence is missing, unsafe, or outside its build output root.");
        return false;
    }

    QFile diagnosticResultFile(canonicalDiagnosticResult);
    constexpr qint64 MaximumDiagnosticResultBytes = 1024 * 1024;
    if (!diagnosticResultFile.open(QIODevice::ReadOnly) || diagnosticResultFile.size() <= 0 ||
        diagnosticResultFile.size() > MaximumDiagnosticResultBytes) {
        errorMessage =
            QObject::tr("ESP32 structured diagnostic result is unreadable or oversized.");
        return false;
    }
    const auto diagnosticDocument =
        QJsonDocument::fromJson(diagnosticResultFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !diagnosticDocument.isObject()) {
        errorMessage = QObject::tr("ESP32 structured diagnostic result is invalid JSON.");
        return false;
    }
    const auto diagnosticRoot = diagnosticDocument.object();
    if (diagnosticRoot.value(QStringLiteral("schemaVersion")).toInt(-1) != 1 ||
        diagnosticRoot.value(QStringLiteral("operation")).toString() !=
            QStringLiteral("bounded-hardware-diagnostic-capture") ||
        diagnosticRoot.value(QStringLiteral("dryRun")).toBool(true) ||
        diagnosticRoot.value(QStringLiteral("fixtureMode")).toBool(true) ||
        !diagnosticRoot.value(QStringLiteral("portOpened")).toBool(false) ||
        diagnosticRoot.value(QStringLiteral("uploadPerformed")).toBool(true) ||
        diagnosticRoot.value(QStringLiteral("profile")).toString() !=
            QString::fromLatin1(BoardProfile) ||
        diagnosticRoot.value(QStringLiteral("port")).toString() != expectedPort ||
        diagnosticRoot.value(QStringLiteral("diagnosticCheck")).toString() !=
            expectedDiagnosticCheck ||
        diagnosticRoot.value(QStringLiteral("automatedResult")).toString() !=
            QStringLiteral("PASS") ||
        diagnosticRoot.value(QStringLiteral("hardwareVerified")).toBool(true)) {
        errorMessage = QObject::tr("ESP32 structured diagnostics are dry-run/fixture data, "
                                   "mismatched, unsafe, or failed.");
        return false;
    }

    const auto binaryBytes = esp32.value(QStringLiteral("binaryBytes")).toDouble(-1.0);
    const auto programBytes = esp32.value(QStringLiteral("programStorageBytes")).toDouble(-1.0);
    const auto ramBytes = esp32.value(QStringLiteral("globalStaticRamBytes")).toDouble(-1.0);
    if (binaryBytes < 0.0 || programBytes < 0.0 || ramBytes < 0.0) {
        errorMessage = QObject::tr("ESP32 deployment result is missing size/memory analysis.");
        return false;
    }
    summary.port = expectedPort;
    summary.diagnosticCheck = expectedDiagnosticCheck;
    summary.diagnosticResult = canonicalDiagnosticResult;
    summary.memoryMap = canonicalMemoryMap;
    summary.binaryBytes = static_cast<quint64>(binaryBytes);
    summary.programStorageBytes = static_cast<quint64>(programBytes);
    summary.globalStaticRamBytes = static_cast<quint64>(ramBytes);
    summary.selectedPortDetected =
        detection.value(QStringLiteral("selectedPortDetected")).toBool(false);
    summary.manualVerificationPending =
        diagnosticRoot.value(QStringLiteral("manualVerificationPending")).toBool(true);
    errorMessage.clear();
    return true;
}

ProcessCommand WorkflowCommands::pcBuild(const QString& repositoryRoot, const QString& projectFile,
                                         const QString& configuration) {
    auto arguments = powerShellScriptArguments(
        QDir(repositoryRoot).filePath(QStringLiteral("scripts/build_project_scripts.ps1")));
    arguments << QStringLiteral("-ProjectPath") << QDir::cleanPath(projectFile)
              << QStringLiteral("-Configuration") << configuration;
    return {QStringLiteral("PC gameplay build (%1)").arg(configuration), powerShellProgram(),
            arguments, QDir::cleanPath(repositoryRoot)};
}

ProcessCommand WorkflowCommands::pcPlay(const QString& playerExecutable, const QString& projectFile,
                                        const QString& workingDirectory,
                                        const QString& scriptModule) {
    QStringList arguments{QStringLiteral("--project"), QDir::cleanPath(projectFile)};
    if (!scriptModule.trimmed().isEmpty()) {
        arguments << QStringLiteral("--script-module") << QDir::cleanPath(scriptModule);
    }
    return {QStringLiteral("PC play"), QDir::cleanPath(playerExecutable), arguments,
            QDir::cleanPath(workingDirectory)};
}

bool WorkflowCommands::parsePcBuildResult(const QByteArray& bytes,
                                          const QString& expectedProjectGuid,
                                          const QString& expectedOutputRoot, QString& scriptModule,
                                          QString& errorMessage) {
    scriptModule.clear();
    constexpr qsizetype MaximumBuildResultBytes = 1024 * 1024;
    if (bytes.isEmpty() || bytes.size() > MaximumBuildResultBytes || bytes.contains('\0')) {
        errorMessage =
            QObject::tr("Gameplay build result is empty, contains NUL, or exceeds 1 MiB.");
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QObject::tr("Invalid gameplay build-result JSON at byte %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString());
        return false;
    }
    const auto root = document.object();
    const QString projectGuid = root.value(QStringLiteral("projectGuid")).toString().toLower();
    const QString expectedGuid = expectedProjectGuid.toLower();
    static const QRegularExpression CanonicalGuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    const auto successValue = root.value(QStringLiteral("success"));
    const auto moduleValue = root.value(QStringLiteral("module"));
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 2 ||
        root.value(QStringLiteral("kind")).toString() !=
            QStringLiteral("FabGLStudioGameplayBuildResult") ||
        !successValue.isBool() || !successValue.toBool() ||
        !CanonicalGuid.match(expectedGuid).hasMatch() || projectGuid != expectedGuid ||
        !moduleValue.isString() || moduleValue.toString().trimmed().isEmpty() ||
        moduleValue.toString().contains(QChar::Null)) {
        errorMessage = QObject::tr("Gameplay build result has an unsupported schema, kind, status, "
                                   "project GUID, or module.");
        return false;
    }

    const QFileInfo outputRootInfo(expectedOutputRoot);
    const QFileInfo moduleInfo(moduleValue.toString());
    const QString canonicalRoot = outputRootInfo.canonicalFilePath();
    const QString canonicalModule = moduleInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalModule.isEmpty() || !outputRootInfo.isDir() ||
        outputRootInfo.isSymLink() || !moduleInfo.isAbsolute() || !moduleInfo.isFile() ||
        moduleInfo.isSymLink()) {
        errorMessage = QObject::tr(
            "Gameplay build result does not reference an existing regular module file.");
        return false;
    }

    QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(canonicalRoot));
    QString normalizedModule = QDir::fromNativeSeparators(QDir::cleanPath(canonicalModule));
    if (!normalizedRoot.endsWith(QLatin1Char('/'))) {
        normalizedRoot += QLatin1Char('/');
    }
#ifdef Q_OS_WIN
    constexpr auto PathCaseSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto PathCaseSensitivity = Qt::CaseSensitive;
#endif
    if (!normalizedModule.startsWith(normalizedRoot, PathCaseSensitivity)) {
        errorMessage =
            QObject::tr("Gameplay module escapes the expected per-project build output directory.");
        return false;
    }

    scriptModule = canonicalModule;
    errorMessage.clear();
    return true;
}

ProcessCommand WorkflowCommands::esp32Export(const QString& projectCliExecutable,
                                             const QString& projectFile,
                                             const QString& firmwareTemplate,
                                             const QString& outputSketch,
                                             const QString& workingDirectory) {
    return {QStringLiteral("ESP32 export"),
            QDir::cleanPath(projectCliExecutable),
            {QStringLiteral("export-esp32"), QDir::cleanPath(projectFile),
             QDir::cleanPath(firmwareTemplate), QDir::cleanPath(outputSketch)},
            QDir::cleanPath(workingDirectory)};
}

ProcessCommand WorkflowCommands::esp32Build(const QString& repositoryRoot,
                                            const QString& sketchDirectory,
                                            const QString& outputRoot, const bool enablePsram) {
    auto arguments = powerShellScriptArguments(
        QDir(repositoryRoot).filePath(QStringLiteral("scripts/build_esp32.ps1")));
    arguments << QStringLiteral("-SketchDirectory") << QDir::cleanPath(sketchDirectory)
              << QStringLiteral("-OutputRoot") << QDir::cleanPath(outputRoot)
              << QStringLiteral("-Clean");
    if (enablePsram) {
        arguments << QStringLiteral("-EnablePsram");
    }
    return {enablePsram ? QStringLiteral("ESP32 build (experimental PSRAM)")
                        : QStringLiteral("ESP32 build (reference)"),
            powerShellProgram(), arguments, QDir::cleanPath(repositoryRoot)};
}

ProcessCommand WorkflowCommands::detectSerialPorts(const QString& repositoryRoot,
                                                   const QString& fixturePath) {
    auto arguments = powerShellScriptArguments(
        QDir(repositoryRoot).filePath(QStringLiteral("scripts/detect_serial_ports.ps1")));
    if (!fixturePath.isEmpty()) {
        arguments << QStringLiteral("-FixturePath") << QDir::cleanPath(fixturePath);
    }
    return {QStringLiteral("Read-only serial port detection"), powerShellProgram(), arguments,
            QDir::cleanPath(repositoryRoot)};
}

ProcessCommand WorkflowCommands::uploadEsp32(const QString& repositoryRoot, const QString& port,
                                             const QString& buildResultPath) {
    if (!isSafeSerialPort(port)) {
        return {};
    }
    auto arguments = powerShellScriptArguments(
        QDir(repositoryRoot).filePath(QStringLiteral("scripts/upload_esp32.ps1")));
    arguments << QStringLiteral("-Port") << port << QStringLiteral("-ConfirmBoardProfile")
              << QString::fromLatin1(BoardProfile) << QStringLiteral("-BuildResultPath")
              << QDir::cleanPath(buildResultPath);
    return {QStringLiteral("ESP32 upload"), powerShellProgram(), arguments,
            QDir::cleanPath(repositoryRoot)};
}

ProcessCommand WorkflowCommands::serialMonitor(const QString& repositoryRoot, const QString& port,
                                               const int baud) {
    if (!isSafeSerialPort(port) || baud < 1200 || baud > 3000000) {
        return {};
    }
    auto arguments = powerShellScriptArguments(
        QDir(repositoryRoot).filePath(QStringLiteral("scripts/serial_monitor.ps1")));
    arguments << QStringLiteral("-Port") << port << QStringLiteral("-Baud") << QString::number(baud)
              << QStringLiteral("-ConfirmBoardProfile") << QString::fromLatin1(BoardProfile);
    return {QStringLiteral("Serial monitor"), powerShellProgram(), arguments,
            QDir::cleanPath(repositoryRoot)};
}

bool WorkflowCommands::isSafeSerialPort(const QString& port) {
    static const QRegularExpression WindowsPort(QStringLiteral("^COM[1-9][0-9]{0,2}$"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression UnixPort(
        QStringLiteral("^/dev/(tty(USB|ACM|S)[A-Za-z0-9._-]*|cu\\.[A-Za-z0-9._-]+)$"));
    return WindowsPort.match(port).hasMatch() || UnixPort.match(port).hasMatch();
}

bool WorkflowCommands::parseSerialPortReport(const QByteArray& bytes,
                                             QVector<SerialPortCandidate>& candidates,
                                             QString& errorMessage) {
    candidates.clear();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errorMessage = QObject::tr("Invalid serial-port detector JSON at byte %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString());
        return false;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1 ||
        root.value(QStringLiteral("operation")).toString() !=
            QStringLiteral("read-only-port-detection") ||
        root.value(QStringLiteral("uploadPerformed")).toBool(true) ||
        root.value(QStringLiteral("portOpened")).toBool(true) ||
        !root.value(QStringLiteral("ports")).isArray()) {
        errorMessage =
            QObject::tr("Serial-port detector returned an unsafe or unsupported report.");
        return false;
    }
    const auto ports = root.value(QStringLiteral("ports")).toArray();
    candidates.reserve(ports.size());
    for (const auto& value : ports) {
        if (!value.isObject()) {
            errorMessage = QObject::tr("Serial-port report contains a non-object entry.");
            candidates.clear();
            return false;
        }
        const auto object = value.toObject();
        SerialPortCandidate candidate;
        candidate.port = object.value(QStringLiteral("port")).toString().trimmed();
        candidate.displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
        candidate.confidence = object.value(QStringLiteral("confidence")).toString().trimmed();
        candidate.reason = object.value(QStringLiteral("reason")).toString().trimmed();
        candidate.boardCandidate = object.value(QStringLiteral("boardCandidate")).toBool(false);
        const auto confirmationValue = object.value(QStringLiteral("requiresUserConfirmation"));
        candidate.requiresUserConfirmation = confirmationValue.toBool(false);
        if (!confirmationValue.isBool() || !isSafeSerialPort(candidate.port) ||
            !candidate.requiresUserConfirmation) {
            errorMessage =
                QObject::tr("Serial-port report contains an invalid or unsafe port entry.");
            candidates.clear();
            return false;
        }
        candidates.push_back(std::move(candidate));
    }
    errorMessage.clear();
    return true;
}

} // namespace fgl::studio
