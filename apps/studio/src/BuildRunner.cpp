#include "BuildRunner.h"

#include <QDir>
#include <QTimer>

namespace fgl::studio {

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

void BuildRunner::startBuild(const QString& program, const QStringList& arguments,
                             const QString& workingDirectory) {
    if (isRunning()) {
        emit outputReady(tr("A build is already running.\n"), true);
        return;
    }
    if (program.trimmed().isEmpty()) {
        emit outputReady(tr("Build program is empty.\n"), true);
        return;
    }
    if (!QDir(workingDirectory).exists()) {
        emit outputReady(tr("Build working directory does not exist: %1\n")
                             .arg(QDir::toNativeSeparators(workingDirectory)),
                         true);
        return;
    }

    m_process.setWorkingDirectory(workingDirectory);
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    emit buildStarted(program, arguments, workingDirectory);
    m_process.start(QIODevice::ReadOnly);
}

void BuildRunner::stopBuild() {
    if (!isRunning()) {
        return;
    }

    emit outputReady(tr("Stopping build process...\n"), true);
    m_process.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (isRunning()) {
            emit outputReady(tr("Build did not stop; terminating it forcefully.\n"), true);
            m_process.kill();
        }
    });
}

void BuildRunner::readStandardOutput() {
    const auto bytes = m_process.readAllStandardOutput();
    if (!bytes.isEmpty()) {
        emit outputReady(QString::fromLocal8Bit(bytes), false);
    }
}

void BuildRunner::readStandardError() {
    const auto bytes = m_process.readAllStandardError();
    if (!bytes.isEmpty()) {
        emit outputReady(QString::fromLocal8Bit(bytes), true);
    }
}

void BuildRunner::processError(const QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        emit outputReady(tr("Could not start build program '%1': %2\n")
                             .arg(m_process.program(), m_process.errorString()),
                         true);
        emit runningChanged(false);
    } else if (error != QProcess::Crashed) {
        emit outputReady(tr("Build process error: %1\n").arg(m_process.errorString()), true);
    }
}

} // namespace fgl::studio
