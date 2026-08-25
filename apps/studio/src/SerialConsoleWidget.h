#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace fgl::studio {

class SerialConsoleWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit SerialConsoleWidget(QWidget* parent = nullptr);

    void appendChunk(const QString& text, bool standardError = false);
    void flushPending();
    void setConnected(bool connected);
    [[nodiscard]] QString visibleText() const;

  signals:
    void sendRequested(const QByteArray& bytes);
    void statusMessage(const QString& message);

  private:
    struct LogLine final {
        QDateTime receivedAt;
        QString text;
        bool standardError = false;
    };

    void appendLine(QString line, bool standardError);
    void rebuildVisibleLog();
    void appendVisibleLine(const LogLine& line);
    void sendInput();
    void findNext();
    void saveLog();
    void clearLog();

    QPlainTextEdit* m_output = nullptr;
    QCheckBox* m_autoScroll = nullptr;
    QCheckBox* m_timestamps = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QComboBox* m_lineEndingCombo = nullptr;
    QPushButton* m_sendButton = nullptr;
    QVector<LogLine> m_lines;
    QString m_pendingText;
    bool m_pendingStandardError = false;
};

} // namespace fgl::studio
