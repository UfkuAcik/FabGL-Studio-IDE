#include "SerialConsoleWidget.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <utility>

namespace fgl::studio {
namespace {

constexpr qsizetype MaximumLogLines = 10000;
constexpr qsizetype MaximumPendingLineCharacters = 65536;

QColor colorForLine(const QString& text, const bool standardError) {
    if (standardError || text.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("fatal"), Qt::CaseInsensitive)) {
        return QColor(QStringLiteral("#ff6b6b"));
    }
    if (text.contains(QStringLiteral("warn"), Qt::CaseInsensitive)) {
        return QColor(QStringLiteral("#ffca58"));
    }
    if (text.contains(QStringLiteral("debug"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("trace"), Qt::CaseInsensitive)) {
        return QColor(QStringLiteral("#9aa3ad"));
    }
    return QColor(QStringLiteral("#dce5ee"));
}

} // namespace

SerialConsoleWidget::SerialConsoleWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* options = new QHBoxLayout;
    m_autoScroll = new QCheckBox(tr("Auto-scroll"), this);
    m_autoScroll->setObjectName(QStringLiteral("serialAutoScroll"));
    m_autoScroll->setChecked(true);
    m_timestamps = new QCheckBox(tr("Timestamp"), this);
    m_timestamps->setObjectName(QStringLiteral("serialTimestamp"));
    m_timestamps->setChecked(true);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("serialFilterEdit"));
    m_filterEdit->setPlaceholderText(tr("Filter visible lines"));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("serialSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("Search"));
    auto* findButton = new QPushButton(tr("Find Next"), this);
    findButton->setObjectName(QStringLiteral("serialFindButton"));
    auto* clearButton = new QPushButton(tr("Clear"), this);
    clearButton->setObjectName(QStringLiteral("serialClearButton"));
    auto* saveButton = new QPushButton(tr("Save Log..."), this);
    saveButton->setObjectName(QStringLiteral("serialSaveButton"));
    options->addWidget(m_autoScroll);
    options->addWidget(m_timestamps);
    options->addWidget(new QLabel(tr("Filter:"), this));
    options->addWidget(m_filterEdit, 1);
    options->addWidget(new QLabel(tr("Search:"), this));
    options->addWidget(m_searchEdit, 1);
    options->addWidget(findButton);
    options->addWidget(clearButton);
    options->addWidget(saveButton);

    m_output = new QPlainTextEdit(this);
    m_output->setObjectName(QStringLiteral("serialOutput"));
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(static_cast<int>(MaximumLogLines));
    m_output->setPlaceholderText(tr("Connect the serial monitor to receive runtime logs."));

    auto* sendRow = new QHBoxLayout;
    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setObjectName(QStringLiteral("serialInputEdit"));
    m_inputEdit->setPlaceholderText(tr("Text to send to the connected device"));
    m_lineEndingCombo = new QComboBox(this);
    m_lineEndingCombo->setObjectName(QStringLiteral("serialLineEndingCombo"));
    m_lineEndingCombo->addItem(tr("No line ending"), QByteArray{});
    m_lineEndingCombo->addItem(tr("LF"), QByteArrayLiteral("\n"));
    m_lineEndingCombo->addItem(tr("CRLF"), QByteArrayLiteral("\r\n"));
    m_lineEndingCombo->setCurrentIndex(1);
    m_sendButton = new QPushButton(tr("Send"), this);
    m_sendButton->setObjectName(QStringLiteral("serialSendButton"));
    sendRow->addWidget(m_inputEdit, 1);
    sendRow->addWidget(m_lineEndingCombo);
    sendRow->addWidget(m_sendButton);

    layout->addLayout(options);
    layout->addWidget(m_output, 1);
    layout->addLayout(sendRow);

    connect(m_filterEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { rebuildVisibleLog(); });
    connect(m_timestamps, &QCheckBox::toggled, this,
            [this](const bool) { rebuildVisibleLog(); });
    connect(findButton, &QPushButton::clicked, this, &SerialConsoleWidget::findNext);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &SerialConsoleWidget::findNext);
    connect(clearButton, &QPushButton::clicked, this, &SerialConsoleWidget::clearLog);
    connect(saveButton, &QPushButton::clicked, this, &SerialConsoleWidget::saveLog);
    connect(m_sendButton, &QPushButton::clicked, this, &SerialConsoleWidget::sendInput);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &SerialConsoleWidget::sendInput);
    setConnected(false);
}

void SerialConsoleWidget::appendChunk(const QString& text, const bool standardError) {
    if (text.isEmpty()) {
        return;
    }
    if (m_pendingText.isEmpty()) {
        m_pendingStandardError = standardError;
    } else {
        m_pendingStandardError = m_pendingStandardError || standardError;
    }
    m_pendingText += text;
    for (;;) {
        const qsizetype newline = m_pendingText.indexOf(QLatin1Char('\n'));
        if (newline < 0) {
            break;
        }
        QString line = m_pendingText.left(newline);
        m_pendingText.remove(0, newline + 1);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        appendLine(std::move(line), m_pendingStandardError);
        m_pendingStandardError = standardError;
    }
    while (m_pendingText.size() > MaximumPendingLineCharacters) {
        appendLine(m_pendingText.left(MaximumPendingLineCharacters), m_pendingStandardError);
        m_pendingText.remove(0, MaximumPendingLineCharacters);
    }
}

void SerialConsoleWidget::flushPending() {
    if (m_pendingText.isEmpty()) {
        return;
    }
    QString line = std::move(m_pendingText);
    m_pendingText.clear();
    appendLine(std::move(line), m_pendingStandardError);
    m_pendingStandardError = false;
}

QString SerialConsoleWidget::visibleText() const {
    return m_output->toPlainText();
}

void SerialConsoleWidget::setConnected(const bool connected) {
    m_inputEdit->setEnabled(connected);
    m_lineEndingCombo->setEnabled(connected);
    m_sendButton->setEnabled(connected);
    m_inputEdit->setPlaceholderText(
        connected ? tr("Text to send to the connected device")
                  : tr("Connect the serial monitor before sending text"));
}

void SerialConsoleWidget::appendLine(QString line, const bool standardError) {
    m_lines.push_back({QDateTime::currentDateTime(), std::move(line), standardError});
    if (m_lines.size() > MaximumLogLines) {
        m_lines.remove(0, m_lines.size() - MaximumLogLines);
        rebuildVisibleLog();
        return;
    }
    const auto& stored = m_lines.constLast();
    if (m_filterEdit->text().isEmpty() ||
        stored.text.contains(m_filterEdit->text(), Qt::CaseInsensitive)) {
        appendVisibleLine(stored);
    }
}

void SerialConsoleWidget::rebuildVisibleLog() {
    m_output->clear();
    const auto filter = m_filterEdit->text();
    for (const auto& line : std::as_const(m_lines)) {
        if (filter.isEmpty() || line.text.contains(filter, Qt::CaseInsensitive)) {
            appendVisibleLine(line);
        }
    }
}

void SerialConsoleWidget::appendVisibleLine(const LogLine& line) {
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(colorForLine(line.text, line.standardError));
    const auto prefix = m_timestamps->isChecked()
                            ? line.receivedAt.toString(QStringLiteral("[HH:mm:ss.zzz] "))
                            : QString{};
    if (!m_output->document()->isEmpty()) {
        cursor.insertText(QStringLiteral("\n"), format);
    }
    cursor.insertText(prefix + line.text, format);
    if (m_autoScroll->isChecked()) {
        m_output->setTextCursor(cursor);
        m_output->ensureCursorVisible();
    }
}

void SerialConsoleWidget::sendInput() {
    QByteArray bytes = m_inputEdit->text().toUtf8();
    bytes += m_lineEndingCombo->currentData().toByteArray();
    if (bytes.isEmpty()) {
        return;
    }
    emit sendRequested(bytes);
    m_inputEdit->clear();
}

void SerialConsoleWidget::findNext() {
    const auto search = m_searchEdit->text();
    if (search.isEmpty()) {
        return;
    }
    if (!m_output->find(search)) {
        auto cursor = m_output->textCursor();
        cursor.movePosition(QTextCursor::Start);
        m_output->setTextCursor(cursor);
        if (!m_output->find(search)) {
            emit statusMessage(tr("Serial log text not found: %1").arg(search));
        }
    }
}

void SerialConsoleWidget::saveLog() {
    const auto filePath = QFileDialog::getSaveFileName(
        this, tr("Save Serial Log"), QStringLiteral("fabgl-serial.log"),
        tr("Log files (*.log *.txt);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit statusMessage(tr("Cannot save serial log: %1").arg(file.errorString()));
        return;
    }
    const auto bytes = m_output->toPlainText().toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        emit statusMessage(tr("Could not completely save the serial log: %1")
                               .arg(file.errorString()));
        return;
    }
    emit statusMessage(tr("Serial log saved to %1").arg(filePath));
}

void SerialConsoleWidget::clearLog() {
    m_lines.clear();
    m_pendingText.clear();
    m_pendingStandardError = false;
    m_output->clear();
}

} // namespace fgl::studio
