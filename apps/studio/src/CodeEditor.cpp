#include "CodeEditor.h"

#include <QAction>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace fgl::studio {

LineNumberArea::LineNumberArea(CodeTextEdit* editor) : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return {m_editor != nullptr ? m_editor->lineNumberAreaWidth() : 0, 0};
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    if (m_editor != nullptr) {
        m_editor->lineNumberAreaPaintEvent(event);
    }
}

CodeTextEdit::CodeTextEdit(QWidget* parent)
    : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)) {
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            &CodeTextEdit::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeTextEdit::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &CodeTextEdit::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeTextEdit::lineNumberAreaWidth() const {
    int digits = 1;
    for (int maximum = std::max(1, blockCount()); maximum >= 10; maximum /= 10) {
        ++digits;
    }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeTextEdit::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), palette().alternateBase());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeTextEdit::goToLine(const int line) {
    const int target = std::clamp(line, 1, std::max(1, blockCount()));
    QTextBlock block = document()->findBlockByNumber(target - 1);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

void CodeTextEdit::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    m_lineNumberArea->setGeometry(contents.left(), contents.top(), lineNumberAreaWidth(),
                                  contents.height());
}

void CodeTextEdit::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeTextEdit::updateLineNumberArea(const QRect& rect, const int dy) {
    if (dy != 0) {
        m_lineNumberArea->scroll(0, dy);
    } else {
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void CodeTextEdit::highlightCurrentLine() {
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(palette().alternateBase().color());
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
}

CppHighlighter::CppHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document), m_commentStart(QStringLiteral("/\\*")),
      m_commentEnd(QStringLiteral("\\*/")) {
    QTextCharFormat keyword;
    keyword.setForeground(QColor(QStringLiteral("#c792ea")));
    keyword.setFontWeight(QFont::Bold);
    const QStringList keywords = {
        QStringLiteral("alignas"),  QStringLiteral("auto"),      QStringLiteral("bool"),
        QStringLiteral("break"),    QStringLiteral("case"),      QStringLiteral("class"),
        QStringLiteral("const"),    QStringLiteral("constexpr"), QStringLiteral("continue"),
        QStringLiteral("default"),  QStringLiteral("delete"),    QStringLiteral("do"),
        QStringLiteral("double"),   QStringLiteral("else"),      QStringLiteral("enum"),
        QStringLiteral("explicit"), QStringLiteral("false"),     QStringLiteral("float"),
        QStringLiteral("for"),      QStringLiteral("if"),        QStringLiteral("include"),
        QStringLiteral("int"),      QStringLiteral("namespace"), QStringLiteral("new"),
        QStringLiteral("noexcept"), QStringLiteral("nullptr"),   QStringLiteral("override"),
        QStringLiteral("private"),  QStringLiteral("protected"), QStringLiteral("public"),
        QStringLiteral("return"),   QStringLiteral("static"),    QStringLiteral("struct"),
        QStringLiteral("switch"),   QStringLiteral("template"),  QStringLiteral("this"),
        QStringLiteral("true"),     QStringLiteral("using"),     QStringLiteral("virtual"),
        QStringLiteral("void"),     QStringLiteral("while")};
    for (const auto& word : keywords) {
        m_rules.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(word)), keyword});
    }

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(QStringLiteral("#c3e88d")));
    m_rules.push_back(
        {QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"")), stringFormat});
    m_rules.push_back({QRegularExpression(QStringLiteral("'(?:\\\\.|[^'\\\\])*'")), stringFormat});

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(QStringLiteral("#f78c6c")));
    m_rules.push_back(
        {QRegularExpression(QStringLiteral("\\b(?:0x[0-9A-Fa-f]+|\\d+(?:\\.\\d+)?)\\b")),
         numberFormat});

    QTextCharFormat preprocessorFormat;
    preprocessorFormat.setForeground(QColor(QStringLiteral("#82aaff")));
    m_rules.push_back({QRegularExpression(QStringLiteral("^\\s*#.*$")), preprocessorFormat});

    QTextCharFormat lineCommentFormat;
    lineCommentFormat.setForeground(QColor(QStringLiteral("#6a9955")));
    m_rules.push_back({QRegularExpression(QStringLiteral("//.*$")), lineCommentFormat});
    m_commentFormat = lineCommentFormat;
}

void CppHighlighter::highlightBlock(const QString& text) {
    for (const auto& rule : m_rules) {
        auto matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(0);
    qsizetype start = previousBlockState() == 1 ? 0 : text.indexOf(m_commentStart);
    while (start >= 0) {
        const auto endMatch = m_commentEnd.match(text, start + 2);
        const qsizetype end = endMatch.capturedStart();
        qsizetype length = 0;
        if (end < 0) {
            setCurrentBlockState(1);
            length = text.size() - start;
        } else {
            length = end - start + endMatch.capturedLength();
        }
        setFormat(static_cast<int>(start), static_cast<int>(length), m_commentFormat);
        start = end < 0 ? -1 : text.indexOf(m_commentStart, start + length);
    }
}

DiagnosticOutputEdit::DiagnosticOutputEdit(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
}

void DiagnosticOutputEdit::mouseDoubleClickEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseDoubleClickEvent(event);
    const auto lineText = cursorForPosition(event->position().toPoint()).block().text();
    static const QRegularExpression diagnosticPattern(
        QStringLiteral("((?:[A-Za-z]:)?[^\\r\\n:()]+?\\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx))"
                       "(?::|\\()(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = diagnosticPattern.match(lineText);
    if (match.hasMatch()) {
        emit diagnosticActivated(match.captured(1).trimmed(), match.captured(2).toInt());
    }
}

CodeEditorWidget::CodeEditorWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tools = new QHBoxLayout;
    auto* newButton = new QToolButton(this);
    newButton->setText(tr("New"));
    auto* openButton = new QToolButton(this);
    openButton->setText(tr("Open"));
    auto* saveButton = new QToolButton(this);
    saveButton->setText(tr("Save"));
    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText(tr("Find"));
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText(tr("Replace"));
    auto* findButton = new QToolButton(this);
    findButton->setText(tr("Next"));
    auto* replaceButton = new QToolButton(this);
    replaceButton->setText(tr("Replace"));
    auto* replaceAllButton = new QToolButton(this);
    replaceAllButton->setText(tr("All"));
    m_lineSpin = new QSpinBox(this);
    m_lineSpin->setRange(1, 1'000'000);
    m_lineSpin->setPrefix(tr("Line "));
    auto* goButton = new QToolButton(this);
    goButton->setText(tr("Go"));
    const std::array<QWidget*, 10> toolWidgets = {
        newButton,  openButton,    saveButton,       m_findEdit, m_replaceEdit,
        findButton, replaceButton, replaceAllButton, m_lineSpin, goButton};
    for (auto* widget : toolWidgets) {
        tools->addWidget(widget);
    }
    layout->addLayout(tools);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    layout->addWidget(m_tabs);

    connect(newButton, &QToolButton::clicked, this, &CodeEditorWidget::createUntitledTab);
    connect(openButton, &QToolButton::clicked, this, &CodeEditorWidget::openFileDialog);
    connect(saveButton, &QToolButton::clicked, this, &CodeEditorWidget::saveCurrent);
    connect(findButton, &QToolButton::clicked, this, [this]() { findNext(); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() { findNext(); });
    connect(replaceButton, &QToolButton::clicked, this, &CodeEditorWidget::replaceOne);
    connect(replaceAllButton, &QToolButton::clicked, this, &CodeEditorWidget::replaceAll);
    connect(goButton, &QToolButton::clicked, this, &CodeEditorWidget::goToRequestedLine);
    connect(m_lineSpin, &QSpinBox::editingFinished, this, &CodeEditorWidget::goToRequestedLine);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &CodeEditorWidget::closeTab);

    auto* findAction = new QAction(this);
    findAction->setShortcut(QKeySequence::Find);
    findAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(findAction);
    connect(findAction, &QAction::triggered, this, [this]() { m_findEdit->setFocus(); });
    auto* saveAction = new QAction(this);
    saveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    saveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(saveAction);
    connect(saveAction, &QAction::triggered, this, &CodeEditorWidget::saveCurrent);
    createUntitledTab();
}

void CodeEditorWidget::openFile(const QString& filePath, const int line) {
    const QFileInfo info(filePath);
    const QString absolutePath = info.absoluteFilePath();
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* editor = editorAt(index);
        if (editor != nullptr &&
            QFileInfo(editor->property("filePath").toString()).absoluteFilePath() == absolutePath) {
            m_tabs->setCurrentIndex(index);
            if (line > 0) {
                editor->goToLine(line);
            }
            return;
        }
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit statusMessage(tr("Cannot open %1: %2")
                               .arg(QDir::toNativeSeparators(absolutePath), file.errorString()));
        return;
    }
    auto* editor = new CodeTextEdit(m_tabs);
    editor->setPlainText(QString::fromUtf8(file.readAll()));
    editor->document()->setModified(false);
    editor->setProperty("filePath", absolutePath);
    editor->setProperty("displayName", info.fileName());
    (void)new CppHighlighter(editor->document());
    const int index = m_tabs->addTab(editor, info.fileName());
    m_tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor]() { updateTabTitle(editor); });
    if (line > 0) {
        editor->goToLine(line);
    }
    emit statusMessage(tr("Opened %1").arg(QDir::toNativeSeparators(absolutePath)));
}

bool CodeEditorWidget::maybeSaveAll() {
    if (!hasUnsavedFiles()) {
        return true;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Code Files"), tr("One or more code tabs have unsaved changes."),
        QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::SaveAll);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Discard) {
        return true;
    }
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* editor = editorAt(index);
        if (editor != nullptr && editor->document()->isModified() && !saveEditor(editor)) {
            return false;
        }
    }
    return true;
}

bool CodeEditorWidget::hasUnsavedFiles() const {
    for (int index = 0; index < m_tabs->count(); ++index) {
        const auto* editor = editorAt(index);
        if (editor != nullptr && editor->document()->isModified()) {
            return true;
        }
    }
    return false;
}

CodeTextEdit* CodeEditorWidget::currentEditor() const {
    return qobject_cast<CodeTextEdit*>(m_tabs->currentWidget());
}

CodeTextEdit* CodeEditorWidget::editorAt(const int index) const {
    return qobject_cast<CodeTextEdit*>(m_tabs->widget(index));
}

void CodeEditorWidget::createUntitledTab() {
    ++m_untitledCounter;
    auto* editor = new CodeTextEdit(m_tabs);
    const auto name = tr("Untitled %1.cpp").arg(m_untitledCounter);
    editor->setProperty("displayName", name);
    (void)new CppHighlighter(editor->document());
    const int index = m_tabs->addTab(editor, name);
    m_tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor]() { updateTabTitle(editor); });
    editor->setFocus();
}

void CodeEditorWidget::openFileDialog() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open Source File"), {},
        tr("C/C++ Sources (*.c *.cc *.cpp *.cxx *.h *.hh *.hpp *.hxx);;All Files (*)"));
    if (!path.isEmpty()) {
        openFile(path);
    }
}

bool CodeEditorWidget::saveEditor(CodeTextEdit* editor, const bool saveAs) {
    if (editor == nullptr) {
        return false;
    }
    QString path = editor->property("filePath").toString();
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Source File"),
            path.isEmpty() ? editor->property("displayName").toString() : path,
            tr("C/C++ Sources (*.c *.cc *.cpp *.cxx *.h *.hpp);;All Files (*)"));
        if (path.isEmpty()) {
            return false;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Save Failed"), file.errorString());
        return false;
    }
    const QByteArray bytes = editor->toPlainText().toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Save Failed"), file.errorString());
        file.cancelWriting();
        return false;
    }
    const QFileInfo info(path);
    editor->setProperty("filePath", info.absoluteFilePath());
    editor->setProperty("displayName", info.fileName());
    editor->document()->setModified(false);
    updateTabTitle(editor);
    emit statusMessage(tr("Saved %1").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    return true;
}

void CodeEditorWidget::saveCurrent() {
    (void)saveEditor(currentEditor());
}

void CodeEditorWidget::closeTab(const int index) {
    auto* editor = editorAt(index);
    if (editor == nullptr) {
        return;
    }
    if (editor->document()->isModified()) {
        const auto answer = QMessageBox::warning(
            this, tr("Unsaved Source"),
            tr("Save changes to %1?").arg(editor->property("displayName").toString()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveEditor(editor))) {
            return;
        }
    }
    m_tabs->removeTab(index);
    editor->deleteLater();
    if (m_tabs->count() == 0) {
        createUntitledTab();
    }
}

void CodeEditorWidget::updateTabTitle(CodeTextEdit* editor) {
    const int index = m_tabs->indexOf(editor);
    if (index < 0) {
        return;
    }
    auto title = editor->property("displayName").toString();
    if (editor->document()->isModified()) {
        title += QLatin1Char('*');
    }
    m_tabs->setTabText(index, title);
}

void CodeEditorWidget::findNext(const bool backwards) {
    auto* editor = currentEditor();
    const QString needle = m_findEdit->text();
    if (editor == nullptr || needle.isEmpty()) {
        return;
    }
    QTextDocument::FindFlags flags;
    if (backwards) {
        flags |= QTextDocument::FindBackward;
    }
    if (editor->find(needle, flags)) {
        return;
    }
    QTextCursor cursor(editor->document());
    cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    editor->setTextCursor(cursor);
    (void)editor->find(needle, flags);
}

void CodeEditorWidget::replaceOne() {
    auto* editor = currentEditor();
    if (editor == nullptr || m_findEdit->text().isEmpty()) {
        return;
    }
    auto cursor = editor->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == m_findEdit->text()) {
        cursor.insertText(m_replaceEdit->text());
    }
    findNext();
}

void CodeEditorWidget::replaceAll() {
    auto* editor = currentEditor();
    if (editor == nullptr || m_findEdit->text().isEmpty()) {
        return;
    }
    int count = 0;
    QTextCursor editCursor(editor->document());
    QTextCursor searchCursor(editor->document());
    editCursor.beginEditBlock();
    while (true) {
        searchCursor = editor->document()->find(m_findEdit->text(), searchCursor);
        if (searchCursor.isNull()) {
            break;
        }
        searchCursor.insertText(m_replaceEdit->text());
        ++count;
    }
    editCursor.endEditBlock();
    emit statusMessage(tr("Replaced %1 occurrence(s).").arg(count));
}

void CodeEditorWidget::goToRequestedLine() {
    if (auto* editor = currentEditor()) {
        editor->goToLine(m_lineSpin->value());
    }
}

} // namespace fgl::studio
