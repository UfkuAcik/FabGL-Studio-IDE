#include "BuildRunner.h"

#include <QDir>
#include <QRegularExpression>
#include <QTimer>

namespace fgl::studio {
namespace {

constexpr qsizetype MaximumCapturedBytes = 4 * 1024 * 1024;

void appendBounded(QByteArray& destination, const QByteArray& bytes) {
    destination.append(bytes);
    if (destination.size() > MaximumCapturedBytes) {
        destination.remove(0, destination.size() - MaximumCapturedBytes);
    }
}

} // namespace

BuildRunner::BuildRunner(QObject* parent) : QObject(parent) {
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &BuildRunner::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &BuildRunner::readStandardError);
    connect(&m_process, &QProcess::started, this, [this]() { emit runningChanged(true); });
    connect(&m_process, &QProcess::errorOccurred, this, &BuildRunner::processError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                readStandardOutput();
                readStandardError();
                flushPendingDiagnostics();
                emit buildFinished(exitCode, exitStatus);
                emit runningChanged(false);
            });
}

bool BuildRunner::isRunning() const {
    return m_process.state() != QProcess::NotRunning;
}

QString BuildRunner::program() const {
    return m_process.program();
}

QStringList BuildRunner::arguments() const {
    return m_process.arguments();
}

QByteArray BuildRunner::standardOutput() const {
    return m_standardOutput;
}

QByteArray BuildRunner::standardError() const {
    return m_standardError;
}

qint64 BuildRunner::writeInput(const QByteArray& bytes) {
    if (!isRunning() || bytes.isEmpty()) {
        return 0;
    }
    const auto written = m_process.write(bytes);
    if (written < 0) {
        emitSyntheticDiagnostic(
            tr("Could not write process input: %1\n").arg(m_process.errorString()),
            BuildOutputSeverity::Error);
    }
    return written;
}

BuildOutputSeverity BuildRunner::classifyDiagnosticLine(const QString& line) {
    static const QRegularExpression ErrorPattern(
        QStringLiteral("(?:^|[\\s:])(?:fatal\\s+)?error(?:\\[[^\\]\\r\\n]+\\])?\\s*:|"
                       "^\\s*CMake\\s+Error(?:\\s+at)?\\b|"
                       "undefined reference to|collect2(?:\\.exe)?: error:|"
                       "ld(?:\\.exe)?: .*failed|ninja: build stopped|"
                       "Error during build:"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression WarningPattern(
        QStringLiteral("(?:^|[\\s:])warning(?:\\[[^\\]\\r\\n]+\\])?\\s*:|"
                       "^\\s*CMake\\s+Warning(?:\\s+at)?\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (ErrorPattern.match(line).hasMatch()) {
        return BuildOutputSeverity::Error;
    }
    if (WarningPattern.match(line).hasMatch()) {
        return BuildOutputSeverity::Warning;
    }
    return BuildOutputSeverity::Info;
}

void BuildRunner::startBuild(const QString& program, const QStringList& arguments,
                             const QString& workingDirectory) {
    if (isRunning()) {
        emitSyntheticDiagnostic(tr("A process is already running.\n"), BuildOutputSeverity::Error);
        return;
    }
    if (program.trimmed().isEmpty()) {
        emitSyntheticDiagnostic(tr("Process program is empty.\n"), BuildOutputSeverity::Error);
        return;
    }
    if (!QDir(workingDirectory).exists()) {
        emitSyntheticDiagnostic(tr("Process working directory does not exist: %1\n")
                                    .arg(QDir::toNativeSeparators(workingDirectory)),
                                BuildOutputSeverity::Error);
        return;
    }

    m_standardOutput.clear();
    m_standardError.clear();
    m_pendingStandardOutput.clear();
    m_pendingStandardError.clear();
    m_failedToStartReported = false;
    m_process.setWorkingDirectory(workingDirectory);
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    emit buildStarted(program, arguments, workingDirectory);
    m_process.start(QIODevice::ReadWrite);
}

void BuildRunner::stopBuild() {
    if (!isRunning()) {
        return;
    }

    emitSyntheticDiagnostic(tr("Stopping process...\n"), BuildOutputSeverity::Warning);
    m_process.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (isRunning()) {
            emitSyntheticDiagnostic(tr("Process did not stop; terminating it forcefully.\n"),
                                    BuildOutputSeverity::Warning);
            m_process.kill();
        }
    });
}

void BuildRunner::readStandardOutput() {
    const auto bytes = m_process.readAllStandardOutput();
    if (!bytes.isEmpty()) {
        appendBounded(m_standardOutput, bytes);
        emit outputReady(QString::fromLocal8Bit(bytes), false);
        classifyAndEmit(bytes, m_pendingStandardOutput);
    }
}

void BuildRunner::readStandardError() {
    const auto bytes = m_process.readAllStandardError();
    if (!bytes.isEmpty()) {
        appendBounded(m_standardError, bytes);
        emit outputReady(QString::fromLocal8Bit(bytes), true);
        classifyAndEmit(bytes, m_pendingStandardError);
    }
}

void BuildRunner::classifyAndEmit(const QByteArray& bytes, QByteArray& pending) {
    pending.append(bytes);
    constexpr qsizetype MaximumPendingLineBytes = 64 * 1024;
    while (true) {
        const auto newline = pending.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const auto lineBytes = pending.first(newline + 1);
        pending.remove(0, newline + 1);
        const auto text = QString::fromLocal8Bit(lineBytes);
        emit classifiedOutputReady(text, classifyDiagnosticLine(text));
    }
    if (pending.size() > MaximumPendingLineBytes) {
        const auto text = QString::fromLocal8Bit(pending);
        pending.clear();
        emit classifiedOutputReady(text, classifyDiagnosticLine(text));
    }
}

void BuildRunner::flushPendingDiagnostics() {
    const auto flush = [this](QByteArray& pending) {
        if (pending.isEmpty()) {
            return;
        }
        const auto text = QString::fromLocal8Bit(pending);
        pending.clear();
        emit classifiedOutputReady(text, classifyDiagnosticLine(text));
    };
    flush(m_pendingStandardOutput);
    flush(m_pendingStandardError);
}

void BuildRunner::emitSyntheticDiagnostic(const QString& text, const BuildOutputSeverity severity) {
    emit outputReady(text, severity != BuildOutputSeverity::Info);
    emit classifiedOutputReady(text, severity);
}

void BuildRunner::processError(const QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        if (m_failedToStartReported) {
            return;
        }
        m_failedToStartReported = true;
        emitSyntheticDiagnostic(tr("Could not start process program '%1': %2\n")
                                    .arg(m_process.program(), m_process.errorString()),
                                BuildOutputSeverity::Error);
        emit buildFinished(-1, QProcess::CrashExit);
        emit runningChanged(false);
    } else if (error != QProcess::Crashed) {
        emitSyntheticDiagnostic(tr("Process error: %1\n").arg(m_process.errorString()),
                                BuildOutputSeverity::Error);
    }
}

} // namespace fgl::studio
