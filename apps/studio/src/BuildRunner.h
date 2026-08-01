#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace fgl::studio {

class BuildRunner final : public QObject {
    Q_OBJECT

  public:
    explicit BuildRunner(QObject* parent = nullptr);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString program() const;
    [[nodiscard]] QStringList arguments() const;

  public slots:
    void startBuild(const QString& program, const QStringList& arguments,
                    const QString& workingDirectory);
    void stopBuild();

  signals:
    void outputReady(const QString& text, bool standardError);
    void buildStarted(const QString& program, const QStringList& arguments,
                      const QString& workingDirectory);
    void buildFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void runningChanged(bool running);

  private:
    void readStandardOutput();
    void readStandardError();
    void processError(QProcess::ProcessError error);

    QProcess m_process;
};

} // namespace fgl::studio
