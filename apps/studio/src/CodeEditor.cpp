#include "CodeEditor.h"

#include "BuildRunner.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <array>

namespace fgl::studio {
namespace {

QString normalizedEditorPath(const QString& path) {
    if (path.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    QString normalized = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    normalized = QDir::cleanPath(normalized);
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

} // namespace

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

std::optional<std::pair<int, int>> CodeTextEdit::matchingBracketPositions() const {
    const QString text = document()->toPlainText();
    const int cursorPosition = textCursor().position();
    int bracketPosition = -1;
    if (cursorPosition < text.size() &&
        QStringLiteral("()[]{}").contains(text.at(cursorPosition))) {
        bracketPosition = cursorPosition;
    } else if (cursorPosition > 0 &&
               QStringLiteral("()[]{}").contains(text.at(cursorPosition - 1))) {
        bracketPosition = cursorPosition - 1;
    }
    if (bracketPosition < 0) {
        return std::nullopt;
    }

    const QChar bracket = text.at(bracketPosition);
    const QString openings = QStringLiteral("([{");
    const QString closings = QStringLiteral(")]}");
    const int openingIndex = static_cast<int>(openings.indexOf(bracket));
    const bool scansForward = openingIndex >= 0;
    const int pairIndex = scansForward ? openingIndex : static_cast<int>(closings.indexOf(bracket));
    if (pairIndex < 0) {
        return std::nullopt;
    }
    const QChar opening = openings.at(pairIndex);
    const QChar closing = closings.at(pairIndex);
    int depth = 0;
    if (scansForward) {
        for (int position = bracketPosition; position < text.size(); ++position) {
            if (text.at(position) == opening) {
                ++depth;
            } else if (text.at(position) == closing && --depth == 0) {
                return std::pair{bracketPosition, position};
            }
        }
    } else {
        for (int position = bracketPosition; position >= 0; --position) {
            if (text.at(position) == closing) {
                ++depth;
            } else if (text.at(position) == opening && --depth == 0) {
                return std::pair{position, bracketPosition};
            }
        }
    }
    return std::nullopt;
}

void CodeTextEdit::setLspDiagnostics(const ClangdDiagnostics& diagnostics) {
    m_lspDiagnostics = diagnostics;
    highlightCurrentLine();
}

qsizetype CodeTextEdit::lspDiagnosticCount() const noexcept {
    return m_lspDiagnostics.size();
}

void CodeTextEdit::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QTextCursor cursor = textCursor();
        const QTextBlock block = cursor.block();
        const QString line = block.text();
        const int column = cursor.position() - block.position();
        const QString before = line.left(column);
        const QString after = line.mid(column);
        const QRegularExpression leadingWhitespace(QStringLiteral("^\\s*"));
        QString indentation = leadingWhitespace.match(line).captured();
        const QString trimmedBefore = before.trimmed();
        const QString trimmedAfter = after.trimmed();
        const bool opensBlock =
            !trimmedBefore.isEmpty() && QStringLiteral("{[(").contains(trimmedBefore.back());
        const bool closesBlock =
            !trimmedAfter.isEmpty() && QStringLiteral("}])").contains(trimmedAfter.front());
        constexpr auto Indent = "    ";
        cursor.beginEditBlock();
        if (opensBlock && closesBlock) {
            cursor.insertText(QStringLiteral("\n") + indentation + QString::fromLatin1(Indent) +
                              QStringLiteral("\n") + indentation);
            cursor.movePosition(QTextCursor::Up);
            cursor.movePosition(QTextCursor::EndOfLine);
        } else {
            if (opensBlock) {
                indentation += QString::fromLatin1(Indent);
            }
            cursor.insertText(QStringLiteral("\n") + indentation);
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
    }
    if (event->text() == QStringLiteral("}")) {
        QTextCursor cursor = textCursor();
        const QTextBlock block = cursor.block();
        const int column = cursor.position() - block.position();
        const QString prefix = block.text().left(column);
        if (!prefix.isEmpty() && prefix.trimmed().isEmpty()) {
            const int removeCount = std::min(4, static_cast<int>(prefix.size()));
            cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, removeCount);
            cursor.removeSelectedText();
            setTextCursor(cursor);
        }
    }
    QPlainTextEdit::keyPressEvent(event);
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
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection lineSelection;
    lineSelection.format.setBackground(palette().alternateBase().color());
    lineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    lineSelection.cursor = textCursor();
    lineSelection.cursor.clearSelection();
    selections.push_back(lineSelection);
    for (const ClangdDiagnostic& diagnostic : m_lspDiagnostics) {
        const QTextBlock startBlock = document()->findBlockByNumber(diagnostic.startLine);
        const QTextBlock endBlock = document()->findBlockByNumber(diagnostic.endLine);
        if (!startBlock.isValid() || !endBlock.isValid()) {
            continue;
        }
        const int startCharacter =
            std::clamp(diagnostic.startCharacter, 0, static_cast<int>(startBlock.text().size()));
        const int endCharacter =
            std::clamp(diagnostic.endCharacter, 0, static_cast<int>(endBlock.text().size()));
        const int startPosition = startBlock.position() + startCharacter;
        int endPosition = endBlock.position() + endCharacter;
        if (endPosition <= startPosition) {
            endPosition = std::min(startPosition + 1, document()->characterCount() - 1);
        }
        if (endPosition <= startPosition) {
            continue;
        }
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(startPosition);
        selection.cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
        const QColor underline =
            diagnostic.severity == 1
                ? QColor(QStringLiteral("#e53935"))
                : (diagnostic.severity == 2 ? QColor(QStringLiteral("#fb8c00"))
                                            : QColor(QStringLiteral("#42a5f5")));
        selection.format.setUnderlineColor(underline);
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setToolTip(diagnostic.message);
        selections.push_back(selection);
    }
    if (const auto match = matchingBracketPositions()) {
        for (const int position : {match->first, match->second}) {
            QTextEdit::ExtraSelection bracketSelection;
            bracketSelection.format.setBackground(QColor(QStringLiteral("#4f6b8a")));
            bracketSelection.format.setForeground(QColor(QStringLiteral("#ffffff")));
            bracketSelection.format.setFontWeight(QFont::Bold);
            bracketSelection.cursor = QTextCursor(document());
            bracketSelection.cursor.setPosition(position);
            bracketSelection.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            selections.push_back(bracketSelection);
        }
    }
    setExtraSelections(selections);
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
            setFormat(static_cast<int>(match.capturedStart()),
                      static_cast<int>(match.capturedLength()), rule.format);
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

void DiagnosticOutputEdit::appendOutput(const QString& text, const BuildOutputSeverity severity) {
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    switch (severity) {
    case BuildOutputSeverity::Info:
        break;
    case BuildOutputSeverity::Warning:
        format.setForeground(QColor(QStringLiteral("#f5a623")));
        break;
    case BuildOutputSeverity::Error:
        format.setForeground(QColor(QStringLiteral("#ff6b6b")));
        break;
    }
    cursor.insertText(text, format);
    setTextCursor(cursor);
    ensureCursorVisible();
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
    auto* saveAsButton = new QToolButton(this);
    saveAsButton->setObjectName(QStringLiteral("codeSaveAsButton"));
    saveAsButton->setText(tr("Save As"));
    m_findEdit = new QLineEdit(this);
    m_findEdit->setObjectName(QStringLiteral("codeFindEdit"));
    m_findEdit->setPlaceholderText(tr("Find"));
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setObjectName(QStringLiteral("codeReplaceEdit"));
    m_replaceEdit->setPlaceholderText(tr("Replace"));
    auto* findButton = new QToolButton(this);
    findButton->setText(tr("Next"));
    auto* findInFilesButton = new QToolButton(this);
    findInFilesButton->setObjectName(QStringLiteral("findInFilesButton"));
    findInFilesButton->setText(tr("In Files"));
    auto* replaceButton = new QToolButton(this);
    replaceButton->setText(tr("Replace"));
    auto* replaceAllButton = new QToolButton(this);
    replaceAllButton->setText(tr("All"));
    m_lineSpin = new QSpinBox(this);
    m_lineSpin->setObjectName(QStringLiteral("codeLineSpin"));
    m_lineSpin->setRange(1, 1'000'000);
    m_lineSpin->setPrefix(tr("Line "));
    auto* goButton = new QToolButton(this);
    goButton->setText(tr("Go"));
    auto* completionButton = new QToolButton(this);
    completionButton->setObjectName(QStringLiteral("clangdCompletionButton"));
    completionButton->setText(tr("Complete"));
    completionButton->setToolTip(tr("clangd completion (Ctrl+Space)"));
    auto* definitionButton = new QToolButton(this);
    definitionButton->setObjectName(QStringLiteral("clangdDefinitionButton"));
    definitionButton->setText(tr("Definition"));
    definitionButton->setToolTip(tr("Go to definition (F12)"));
    auto* referencesButton = new QToolButton(this);
    referencesButton->setObjectName(QStringLiteral("clangdReferencesButton"));
    referencesButton->setText(tr("References"));
    referencesButton->setToolTip(tr("Find references (Shift+F12)"));
    auto* hoverButton = new QToolButton(this);
    hoverButton->setObjectName(QStringLiteral("clangdHoverButton"));
    hoverButton->setText(tr("Hover"));
    hoverButton->setToolTip(tr("Show clangd hover (Ctrl+I)"));
    auto* renameButton = new QToolButton(this);
    renameButton->setObjectName(QStringLiteral("clangdRenameButton"));
    renameButton->setText(tr("Rename"));
    renameButton->setToolTip(tr("Rename symbol (F2)"));
    auto* formatButton = new QToolButton(this);
    formatButton->setObjectName(QStringLiteral("clangdFormatButton"));
    formatButton->setText(tr("Format"));
    formatButton->setToolTip(tr("Format document (Shift+Alt+F)"));
    m_clangdStatus = new QLabel(tr("clangd: no project"), this);
    m_clangdStatus->setObjectName(QStringLiteral("clangdStatusLabel"));
    m_clangdStatus->setToolTip(tr("C/C++ language server status"));
    const std::array<QWidget*, 18> toolWidgets = {
        newButton,     openButton,   saveButton,        saveAsButton,     m_findEdit,
        m_replaceEdit, findButton,   findInFilesButton, replaceButton,    replaceAllButton,
        m_lineSpin,    goButton,     completionButton,  definitionButton, referencesButton,
        hoverButton,   renameButton, formatButton};
    for (auto* widget : toolWidgets) {
        tools->addWidget(widget);
    }
    tools->addStretch(1);
    tools->addWidget(m_clangdStatus);
    layout->addLayout(tools);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->setObjectName(QStringLiteral("codeEditorContentSplitter"));
    auto* navigator = new QTabWidget(contentSplitter);
    navigator->setObjectName(QStringLiteral("codeNavigatorTabs"));
    m_projectFilesTree = new QTreeWidget(navigator);
    m_projectFilesTree->setObjectName(QStringLiteral("codeProjectFileTree"));
    m_projectFilesTree->setHeaderLabels({tr("Project Files")});
    m_projectFilesTree->setUniformRowHeights(true);
    m_symbolTree = new QTreeWidget(navigator);
    m_symbolTree->setObjectName(QStringLiteral("codeSymbolTree"));
    m_symbolTree->setHeaderLabels({tr("Symbol"), tr("Kind"), tr("File")});
    m_symbolTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    navigator->addTab(m_projectFilesTree, tr("Files"));
    navigator->addTab(m_symbolTree, tr("Symbols"));

    m_tabs = new QTabWidget(contentSplitter);
    m_tabs->setObjectName(QStringLiteral("codeEditorTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    contentSplitter->addWidget(navigator);
    contentSplitter->addWidget(m_tabs);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes({260, 900});
    layout->addWidget(contentSplitter, 1);

    m_findResults = new QTableWidget(0, 3, this);
    m_findResults->setObjectName(QStringLiteral("findInFilesResults"));
    m_findResults->setHorizontalHeaderLabels({tr("File"), tr("Line"), tr("Match")});
    m_findResults->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_findResults->setSelectionMode(QAbstractItemView::SingleSelection);
    m_findResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_findResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_findResults->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_findResults->setMaximumHeight(190);
    m_findResults->hide();
    layout->addWidget(m_findResults);

    m_fileWatcher = new QFileSystemWatcher(this);
    m_projectRefreshTimer = new QTimer(this);
    m_projectRefreshTimer->setSingleShot(true);
    m_projectRefreshTimer->setInterval(150);
    m_symbolRefreshTimer = new QTimer(this);
    m_symbolRefreshTimer->setSingleShot(true);
    m_symbolRefreshTimer->setInterval(200);
    m_clangd = new ClangdClient(this);

    connect(newButton, &QToolButton::clicked, this, &CodeEditorWidget::createUntitledTab);
    connect(openButton, &QToolButton::clicked, this, &CodeEditorWidget::openFileDialog);
    connect(saveButton, &QToolButton::clicked, this, &CodeEditorWidget::saveCurrent);
    connect(saveAsButton, &QToolButton::clicked, this,
            [this]() { (void)saveEditor(currentEditor(), true); });
    connect(findButton, &QToolButton::clicked, this, [this]() { findNext(); });
    connect(findInFilesButton, &QToolButton::clicked, this,
            [this]() { (void)findInFiles(m_findEdit->text()); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() { findNext(); });
    connect(replaceButton, &QToolButton::clicked, this, &CodeEditorWidget::replaceOne);
    connect(replaceAllButton, &QToolButton::clicked, this, &CodeEditorWidget::replaceAll);
    connect(goButton, &QToolButton::clicked, this, &CodeEditorWidget::goToRequestedLine);
    connect(completionButton, &QToolButton::clicked, this, &CodeEditorWidget::requestCompletion);
    connect(definitionButton, &QToolButton::clicked, this, &CodeEditorWidget::requestDefinition);
    connect(referencesButton, &QToolButton::clicked, this, &CodeEditorWidget::requestReferences);
    connect(hoverButton, &QToolButton::clicked, this, &CodeEditorWidget::requestHover);
    connect(renameButton, &QToolButton::clicked, this, &CodeEditorWidget::requestRename);
    connect(formatButton, &QToolButton::clicked, this, &CodeEditorWidget::requestFormatting);
    connect(m_lineSpin, &QSpinBox::editingFinished, this, &CodeEditorWidget::goToRequestedLine);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &CodeEditorWidget::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { scheduleSymbolRefresh(); });
    connect(m_projectFilesTree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) {
                const QString path = item->data(0, Qt::UserRole).toString();
                if (!path.isEmpty()) {
                    openFile(path);
                }
            });
    connect(m_symbolTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        const QString path = item->data(0, Qt::UserRole).toString();
        const int line = item->data(0, Qt::UserRole + 1).toInt();
        if (!path.isEmpty()) {
            openFile(path, line);
        }
    });
    connect(m_findResults, &QTableWidget::cellDoubleClicked, this, [this](const int row, int) {
        const auto* item = m_findResults->item(row, 0);
        if (item != nullptr) {
            openFile(item->data(Qt::UserRole).toString(),
                     m_findResults->item(row, 1)->text().toInt());
        }
    });
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            &CodeEditorWidget::handleWatchedFileChanged);
    connect(m_fileWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { scheduleProjectRefresh(); });
    connect(m_projectRefreshTimer, &QTimer::timeout, this, &CodeEditorWidget::refreshProjectFiles);
    connect(m_symbolRefreshTimer, &QTimer::timeout, this, &CodeEditorWidget::refreshSymbols);
    connect(m_clangd, &ClangdClient::stateChanged, this,
            &CodeEditorWidget::updateLanguageServerStatus);
    connect(m_clangd, &ClangdClient::statusMessage, this,
            [this](const QString& message) { emit statusMessage(message); });
    connect(m_clangd, &ClangdClient::readyChanged, this, [this](const bool ready) {
        if (!ready) {
            return;
        }
        for (int index = 0; index < m_tabs->count(); ++index) {
            synchronizeEditorWithLanguageServer(editorAt(index));
        }
    });
    connect(m_clangd, &ClangdClient::completionReady, this, &CodeEditorWidget::showCompletionItems);
    connect(m_clangd, &ClangdClient::locationsReady, this, &CodeEditorWidget::showLocations);
    connect(m_clangd, &ClangdClient::hoverReady, this, &CodeEditorWidget::showHover);
    connect(
        m_clangd, &ClangdClient::diagnosticsPublished, this,
        [this](const QString& path, const ClangdDiagnostics& diagnostics) {
            if (auto* editor = editorForPath(path)) {
                editor->setLspDiagnostics(diagnostics);
            }
            emit statusMessage(
                diagnostics.isEmpty()
                    ? tr("clangd reports no diagnostics for %1.").arg(QFileInfo(path).fileName())
                    : tr("clangd reports %1 diagnostic(s) for %2.")
                          .arg(diagnostics.size())
                          .arg(QFileInfo(path).fileName()));
        });
    connect(m_clangd, &ClangdClient::workspaceEditFinished, this,
            [this](const QString&, const bool, const QString& message) {
                emit statusMessage(message);
            });
    connect(m_clangd, &ClangdClient::fileContentChanged, this,
            &CodeEditorWidget::applyLanguageServerFile);

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
    auto* saveAsAction = new QAction(this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(saveAsAction);
    connect(saveAsAction, &QAction::triggered, this,
            [this]() { (void)saveEditor(currentEditor(), true); });

    const auto addLanguageAction = [this](const QKeySequence& shortcut, auto handler) {
        auto* action = new QAction(this);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
        connect(action, &QAction::triggered, this, handler);
    };
    addLanguageAction(QKeySequence(Qt::CTRL | Qt::Key_Space), &CodeEditorWidget::requestCompletion);
    addLanguageAction(QKeySequence(Qt::Key_F12), &CodeEditorWidget::requestDefinition);
    addLanguageAction(QKeySequence(Qt::SHIFT | Qt::Key_F12), &CodeEditorWidget::requestReferences);
    addLanguageAction(QKeySequence(Qt::CTRL | Qt::Key_I), &CodeEditorWidget::requestHover);
    addLanguageAction(QKeySequence(Qt::Key_F2), &CodeEditorWidget::requestRename);
    addLanguageAction(QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F),
                      &CodeEditorWidget::requestFormatting);
    createUntitledTab();
}

void CodeEditorWidget::openFile(const QString& filePath, const int line) {
    const QFileInfo info(filePath);
    const QString absolutePath = info.absoluteFilePath();
    if (!m_projectRoot.isEmpty() && !isSafeIndexedPath(m_projectRoot, absolutePath, false)) {
        emit statusMessage(tr("Refused to open %1 because it is outside the canonical project root "
                              "or uses a link/reparse point.")
                               .arg(QDir::toNativeSeparators(absolutePath)));
        return;
    }
    const QString normalizedPath = normalizedEditorPath(absolutePath);
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* editor = editorAt(index);
        if (editor != nullptr &&
            normalizedEditorPath(editor->property("filePath").toString()) == normalizedPath) {
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
    const QByteArray bytes = file.readAll();
    auto* editor = new CodeTextEdit(m_tabs);
    editor->setPlainText(QString::fromUtf8(bytes));
    editor->document()->setModified(false);
    editor->setProperty("filePath", absolutePath);
    editor->setProperty("displayName", info.fileName());
    editor->setProperty("diskDigest", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    editor->setProperty("externalChanged", false);
    (void)new CppHighlighter(editor->document());
    const int index = m_tabs->addTab(editor, info.fileName());
    m_tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor]() { updateTabTitle(editor); });
    connectEditorToLanguageServer(editor);
    watchEditorFile(editor);
    if (line > 0) {
        editor->goToLine(line);
    }
    emit statusMessage(tr("Opened %1").arg(QDir::toNativeSeparators(absolutePath)));
}

void CodeEditorWidget::setProjectRoot(const QString& projectRoot) {
    QString normalized;
    if (!projectRoot.trimmed().isEmpty()) {
        const QFileInfo info(projectRoot);
        normalized = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
        normalized = QDir::cleanPath(normalized);
        if (!QFileInfo(normalized).isDir()) {
            normalized.clear();
        }
    }
    if (normalizedEditorPath(normalized) == normalizedEditorPath(m_projectRoot)) {
        return;
    }
    m_projectRoot = normalized;
    if (m_projectRoot.isEmpty()) {
        m_clangd->stop();
        m_clangdStatus->setText(tr("clangd: no project"));
    } else {
        (void)m_clangd->start(m_projectRoot);
    }
    refreshProjectFiles();
}

QString CodeEditorWidget::projectRoot() const {
    return m_projectRoot;
}

qsizetype CodeEditorWidget::projectFileCount() const noexcept {
    return m_projectFiles.size();
}

qsizetype CodeEditorWidget::symbolCount() const noexcept {
    return static_cast<qsizetype>(m_symbols.size());
}

QStringList CodeEditorWidget::symbolNames() const {
    QStringList names;
    names.reserve(static_cast<qsizetype>(m_symbols.size()));
    for (const auto& symbol : m_symbols) {
        names.push_back(symbol.name);
    }
    return names;
}

int CodeEditorWidget::findInFiles(const QString& needle) {
    m_findResults->setRowCount(0);
    const QString query = needle.trimmed();
    if (query.isEmpty()) {
        m_findResults->hide();
        emit statusMessage(tr("Enter text to search across project source files."));
        return 0;
    }
    if (m_projectFiles.isEmpty() && !m_projectRoot.isEmpty()) {
        refreshProjectFiles();
    }
    constexpr qint64 MaximumIndexedFileBytes = 4LL * 1024LL * 1024LL;
    constexpr int MaximumResults = 2000;
    int resultCount = 0;
    for (const auto& filePath : m_projectFiles) {
        if (!isSourceFile(filePath) || resultCount >= MaximumResults) {
            continue;
        }
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
            file.size() > MaximumIndexedFileBytes) {
            continue;
        }
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (qsizetype lineIndex = 0; lineIndex < lines.size() && resultCount < MaximumResults;
             ++lineIndex) {
            if (!lines.at(lineIndex).contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            const int row = m_findResults->rowCount();
            m_findResults->insertRow(row);
            const QString relative = m_projectRoot.isEmpty()
                                         ? QFileInfo(filePath).fileName()
                                         : QDir(m_projectRoot).relativeFilePath(filePath);
            auto* fileItem = new QTableWidgetItem(QDir::toNativeSeparators(relative));
            fileItem->setData(Qt::UserRole, filePath);
            m_findResults->setItem(row, 0, fileItem);
            m_findResults->setItem(
                row, 1,
                new QTableWidgetItem(QString::number(static_cast<qlonglong>(lineIndex + 1))));
            m_findResults->setItem(row, 2, new QTableWidgetItem(lines.at(lineIndex).trimmed()));
            ++resultCount;
        }
    }
    m_findResults->show();
    emit statusMessage(resultCount >= MaximumResults
                           ? tr("Showing the first %1 project matches.").arg(MaximumResults)
                           : tr("Found %1 project match(es).").arg(resultCount));
    return resultCount;
}

void CodeEditorWidget::scanForExternalChanges() {
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* editor = editorAt(index);
        if (editor == nullptr || editor->property("filePath").toString().isEmpty()) {
            continue;
        }
        const QString path = editor->property("filePath").toString();
        if (fileDigest(path) != editor->property("diskDigest").toByteArray()) {
            handleWatchedFileChanged(path);
        }
    }
}

bool CodeEditorWidget::hasExternalChange(const QString& filePath) const {
    const auto* editor = editorForPath(filePath);
    return editor != nullptr && editor->property("externalChanged").toBool();
}

bool CodeEditorWidget::reloadFileFromDisk(const QString& filePath, QString& errorMessage) {
    auto* editor = editorForPath(filePath);
    if (editor == nullptr) {
        errorMessage = tr("The file is not open in the editor.");
        return false;
    }
    return reloadEditorFromDisk(editor, errorMessage);
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

bool CodeEditorWidget::saveCurrentFileAs(const QString& filePath, QString& errorMessage) {
    return saveEditorToPath(currentEditor(), filePath, errorMessage);
}

ClangdClient* CodeEditorWidget::clangdClient() const noexcept {
    return m_clangd;
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
    editor->setProperty("externalChanged", false);
    (void)new CppHighlighter(editor->document());
    const int index = m_tabs->addTab(editor, name);
    m_tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor]() { updateTabTitle(editor); });
    connectEditorToLanguageServer(editor);
    editor->setFocus();
}

void CodeEditorWidget::openFileDialog() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open Source File"), m_projectRoot,
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
        const QString suggestedPath =
            path.isEmpty()
                ? (m_projectRoot.isEmpty()
                       ? editor->property("displayName").toString()
                       : QDir(m_projectRoot).filePath(editor->property("displayName").toString()))
                : path;
        path = QFileDialog::getSaveFileName(
            this, tr("Save Source File"), suggestedPath,
            tr("C/C++ Sources (*.c *.cc *.cpp *.cxx *.h *.hpp);;All Files (*)"));
        if (path.isEmpty()) {
            return false;
        }
    }
    QString errorMessage;
    if (saveEditorToPath(editor, path, errorMessage)) {
        return true;
    }
    if (!errorMessage.isEmpty()) {
        emit statusMessage(errorMessage);
        QMessageBox::critical(this, tr("Save Failed"), errorMessage);
    }
    return false;
}

bool CodeEditorWidget::saveEditorToPath(CodeTextEdit* editor, const QString& filePath,
                                        QString& errorMessage) {
    errorMessage.clear();
    if (editor == nullptr) {
        errorMessage = tr("The editor tab is unavailable.");
        return false;
    }
    QString path = QFileInfo(filePath).absoluteFilePath();
    if (!m_projectRoot.isEmpty()) {
        QString validatedPath;
        if (!isSafeWritableProjectPath(m_projectRoot, path, validatedPath, errorMessage)) {
            return false;
        }
        path = validatedPath;
    }
    const QString previousPath = editor->property("filePath").toString();
    if (!previousPath.isEmpty()) {
        (void)m_fileWatcher->removePath(previousPath);
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        watchEditorFile(editor);
        errorMessage = tr("Cannot atomically prepare %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    const QByteArray bytes = editor->toPlainText().toUtf8();
    if (file.write(bytes) != bytes.size()) {
        watchEditorFile(editor);
        errorMessage =
            tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!m_projectRoot.isEmpty()) {
        QString revalidatedPath;
        QString validationError;
        if (!isSafeWritableProjectPath(m_projectRoot, path, revalidatedPath, validationError) ||
            normalizedEditorPath(revalidatedPath) != normalizedEditorPath(path)) {
            file.cancelWriting();
            watchEditorFile(editor);
            errorMessage = validationError.isEmpty()
                               ? tr("The save destination changed identity before commit.")
                               : validationError;
            return false;
        }
    }
    if (!file.commit()) {
        watchEditorFile(editor);
        errorMessage = tr("Cannot atomically commit %1: %2")
                           .arg(QDir::toNativeSeparators(path), file.errorString());
        file.cancelWriting();
        return false;
    }
    const QFileInfo info(path);
    editor->setProperty("filePath", info.absoluteFilePath());
    editor->setProperty("displayName", info.fileName());
    editor->setProperty("diskDigest", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    editor->setProperty("externalChanged", false);
    editor->document()->setModified(false);
    if (!previousPath.isEmpty() &&
        normalizedEditorPath(previousPath) != normalizedEditorPath(info.absoluteFilePath())) {
        m_clangd->closeDocument(previousPath);
    }
    synchronizeEditorWithLanguageServer(editor);
    watchEditorFile(editor);
    updateTabTitle(editor);
    emit fileSaved(info.absoluteFilePath());
    emit statusMessage(tr("Saved %1").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    errorMessage.clear();
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
    const QString path = editor->property("filePath").toString();
    if (!path.isEmpty()) {
        m_clangd->closeDocument(path);
        (void)m_fileWatcher->removePath(path);
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
    if (editor->property("externalChanged").toBool()) {
        title += QStringLiteral(" !");
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

void CodeEditorWidget::refreshProjectFiles() {
    const QStringList watchedDirectories = m_fileWatcher->directories();
    if (!watchedDirectories.isEmpty()) {
        (void)m_fileWatcher->removePaths(watchedDirectories);
    }
    m_projectFiles.clear();
    m_projectFilesTree->clear();
    if (m_projectRoot.isEmpty() || !QFileInfo(m_projectRoot).isDir()) {
        refreshSymbols();
        return;
    }

    constexpr qsizetype MaximumProjectFiles = 5000;
    QDirIterator iterator(m_projectRoot, QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext() && m_projectFiles.size() < MaximumProjectFiles) {
        const QString filePath = iterator.next();
        const QString relative = QDir(m_projectRoot).relativeFilePath(filePath);
        if (!isIgnoredProjectPath(relative) && isSafeIndexedPath(m_projectRoot, filePath)) {
            m_projectFiles.push_back(QFileInfo(filePath).absoluteFilePath());
        }
    }
    m_projectFiles.sort(Qt::CaseInsensitive);

    QHash<QString, QTreeWidgetItem*> directories;
    for (const auto& filePath : m_projectFiles) {
        const QString relative =
            QDir::fromNativeSeparators(QDir(m_projectRoot).relativeFilePath(filePath));
        const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QTreeWidgetItem* parent = m_projectFilesTree->invisibleRootItem();
        QString directoryKey;
        for (qsizetype partIndex = 0; partIndex < parts.size(); ++partIndex) {
            const bool isFile = partIndex + 1 == parts.size();
            const QString& part = parts.at(partIndex);
            if (isFile) {
                auto* item = new QTreeWidgetItem(parent, {part});
                item->setData(0, Qt::UserRole, filePath);
                item->setToolTip(0, QDir::toNativeSeparators(filePath));
                continue;
            }
            directoryKey = directoryKey.isEmpty() ? part : directoryKey + QLatin1Char('/') + part;
            auto* directoryItem = directories.value(directoryKey, nullptr);
            if (directoryItem == nullptr) {
                directoryItem = new QTreeWidgetItem(parent, {part});
                directoryItem->setData(0, Qt::UserRole, QString{});
                directories.insert(directoryKey, directoryItem);
            }
            parent = directoryItem;
        }
    }
    m_projectFilesTree->sortItems(0, Qt::AscendingOrder);

    QStringList directoriesToWatch{m_projectRoot};
    constexpr qsizetype MaximumWatchedDirectories = 256;
    QDirIterator directoryIterator(m_projectRoot, QDir::Dirs | QDir::NoDotAndDotDot,
                                   QDirIterator::Subdirectories);
    while (directoryIterator.hasNext() && directoriesToWatch.size() < MaximumWatchedDirectories) {
        const QString directory = directoryIterator.next();
        const QString relative = QDir(m_projectRoot).relativeFilePath(directory);
        if (!isIgnoredProjectPath(relative) && isSafeIndexedPath(m_projectRoot, directory, true)) {
            directoriesToWatch.push_back(directory);
        }
    }
    (void)m_fileWatcher->addPaths(directoriesToWatch);
    refreshSymbols();
    emit statusMessage(m_projectFiles.size() >= MaximumProjectFiles
                           ? tr("Project file tree limited to %1 entries.").arg(MaximumProjectFiles)
                           : tr("Indexed %1 project file(s).").arg(m_projectFiles.size()));
}

void CodeEditorWidget::refreshSymbols() {
    m_symbols.clear();
    m_symbolTree->clear();
    constexpr qint64 MaximumIndexedFileBytes = 4LL * 1024LL * 1024LL;
    constexpr std::size_t MaximumSymbols = 5000U;
    const QRegularExpression typePattern(
        QStringLiteral("^\\s*(class|struct|enum(?:\\s+class)?)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
    const QRegularExpression functionPattern(QStringLiteral(
        "^\\s*(?:template\\s*<[^>]*>\\s*)?(?:(?:inline|static|virtual|constexpr|consteval|friend|"
        "extern)\\s+)*(?:[A-Za-z_][A-Za-z0-9_:<>~,]*(?:\\s*[*&])?\\s+)+([A-Za-z_~][A-Za-z0-9_]*)"
        "\\s*\\([^;{}]*\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?(?:\\{|;)?\\s*$"));

    for (const auto& filePath : m_projectFiles) {
        if (!isSourceFile(filePath) || m_symbols.size() >= MaximumSymbols) {
            continue;
        }
        QString text;
        if (const auto* editor = editorForPath(filePath)) {
            text = editor->toPlainText();
        } else {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
                file.size() > MaximumIndexedFileBytes) {
                continue;
            }
            text = QString::fromUtf8(file.readAll());
        }
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (qsizetype lineIndex = 0; lineIndex < lines.size() && m_symbols.size() < MaximumSymbols;
             ++lineIndex) {
            const QString& line = lines.at(lineIndex);
            const auto typeMatch = typePattern.match(line);
            SymbolRecord symbol;
            if (typeMatch.hasMatch()) {
                symbol.kind = typeMatch.captured(1);
                symbol.name = typeMatch.captured(2);
            } else {
                const auto functionMatch = functionPattern.match(line);
                if (!functionMatch.hasMatch()) {
                    continue;
                }
                symbol.kind = tr("function");
                symbol.name = functionMatch.captured(1);
            }
            symbol.filePath = filePath;
            symbol.line = static_cast<int>(lineIndex + 1);
            m_symbols.push_back(symbol);
        }
    }

    for (const auto& symbol : m_symbols) {
        const QString relative = m_projectRoot.isEmpty()
                                     ? QFileInfo(symbol.filePath).fileName()
                                     : QDir(m_projectRoot).relativeFilePath(symbol.filePath);
        auto* item = new QTreeWidgetItem(
            m_symbolTree, {symbol.name, symbol.kind, QDir::toNativeSeparators(relative)});
        item->setData(0, Qt::UserRole, symbol.filePath);
        item->setData(0, Qt::UserRole + 1, symbol.line);
    }
    m_symbolTree->sortItems(0, Qt::AscendingOrder);
}

void CodeEditorWidget::handleWatchedFileChanged(const QString& filePath) {
    auto* editor = editorForPath(filePath);
    if (editor == nullptr) {
        return;
    }
    const QByteArray digest = fileDigest(filePath);
    if (!digest.isEmpty() && digest == editor->property("diskDigest").toByteArray()) {
        watchEditorFile(editor);
        return;
    }
    if (!QFileInfo::exists(filePath) || editor->document()->isModified()) {
        editor->setProperty("externalChanged", true);
        updateTabTitle(editor);
        emit externalFileChanged(filePath, true);
        emit statusMessage(tr("%1 changed outside the editor; local content was kept.")
                               .arg(QDir::toNativeSeparators(filePath)));
        watchEditorFile(editor);
        return;
    }
    QString errorMessage;
    if (!reloadEditorFromDisk(editor, errorMessage)) {
        editor->setProperty("externalChanged", true);
        updateTabTitle(editor);
        emit externalFileChanged(filePath, true);
        emit statusMessage(errorMessage);
        return;
    }
    emit externalFileChanged(filePath, false);
    emit statusMessage(
        tr("Reloaded externally changed file %1.").arg(QDir::toNativeSeparators(filePath)));
}

void CodeEditorWidget::scheduleProjectRefresh() {
    m_projectRefreshTimer->start();
}

void CodeEditorWidget::scheduleSymbolRefresh() {
    m_symbolRefreshTimer->start();
}

void CodeEditorWidget::watchEditorFile(CodeTextEdit* editor) {
    if (editor == nullptr) {
        return;
    }
    const QString path = editor->property("filePath").toString();
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        return;
    }
    const QString normalized = normalizedEditorPath(path);
    const auto watched = m_fileWatcher->files();
    const bool alreadyWatched =
        std::any_of(watched.cbegin(), watched.cend(), [&normalized](const QString& candidate) {
            return normalizedEditorPath(candidate) == normalized;
        });
    if (!alreadyWatched) {
        (void)m_fileWatcher->addPath(path);
    }
}

void CodeEditorWidget::connectEditorToLanguageServer(CodeTextEdit* editor) {
    if (editor == nullptr) {
        return;
    }
    auto* changeTimer = new QTimer(editor);
    changeTimer->setSingleShot(true);
    changeTimer->setInterval(125);
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor, changeTimer]() {
        scheduleSymbolRefresh();
        editor->setLspDiagnostics({});
        changeTimer->start();
    });
    connect(changeTimer, &QTimer::timeout, this,
            [this, editor]() { synchronizeEditorWithLanguageServer(editor); });
    synchronizeEditorWithLanguageServer(editor);
}

void CodeEditorWidget::synchronizeEditorWithLanguageServer(CodeTextEdit* editor) {
    if (editor == nullptr || !m_clangd->isReady()) {
        return;
    }
    const QString path = editor->property("filePath").toString();
    if (path.isEmpty() || !isSourceFile(path)) {
        return;
    }
    if (!m_clangd->openDocument(path, editor->toPlainText())) {
        emit statusMessage(
            tr("clangd did not open %1; save it inside the project root to enable LSP features.")
                .arg(QFileInfo(path).fileName()));
    }
}

bool CodeEditorWidget::currentLanguageServerPosition(QString& filePath, int& line,
                                                     int& character) const {
    const auto* editor = currentEditor();
    if (editor == nullptr || !m_clangd->isReady()) {
        return false;
    }
    filePath = editor->property("filePath").toString();
    if (filePath.isEmpty() || !isSourceFile(filePath)) {
        return false;
    }
    const QTextCursor cursor = editor->textCursor();
    line = cursor.blockNumber();
    character = cursor.positionInBlock();
    return true;
}

void CodeEditorWidget::requestCompletion() {
    QString path;
    int line = 0;
    int character = 0;
    if (!currentLanguageServerPosition(path, line, character) ||
        m_clangd->requestCompletion(path, line, character) < 0) {
        emit statusMessage(
            tr("clangd completion is unavailable; save this source inside the project first."));
    }
}

void CodeEditorWidget::requestDefinition() {
    QString path;
    int line = 0;
    int character = 0;
    if (!currentLanguageServerPosition(path, line, character) ||
        m_clangd->requestDefinition(path, line, character) < 0) {
        emit statusMessage(tr("clangd definition lookup is unavailable for this tab."));
    }
}

void CodeEditorWidget::requestReferences() {
    QString path;
    int line = 0;
    int character = 0;
    if (!currentLanguageServerPosition(path, line, character) ||
        m_clangd->requestReferences(path, line, character) < 0) {
        emit statusMessage(tr("clangd reference lookup is unavailable for this tab."));
    }
}

void CodeEditorWidget::requestHover() {
    QString path;
    int line = 0;
    int character = 0;
    if (!currentLanguageServerPosition(path, line, character) ||
        m_clangd->requestHover(path, line, character) < 0) {
        emit statusMessage(tr("clangd hover information is unavailable for this tab."));
    }
}

void CodeEditorWidget::requestRename() {
    QString path;
    int line = 0;
    int character = 0;
    if (!currentLanguageServerPosition(path, line, character)) {
        emit statusMessage(tr("clangd rename is unavailable for this tab."));
        return;
    }
    auto* editor = currentEditor();
    const QString initialName = editor != nullptr ? editor->textCursor().selectedText() : QString{};
    bool accepted = false;
    const QString newName =
        QInputDialog::getText(this, tr("Rename Symbol"), tr("New C/C++ identifier:"),
                              QLineEdit::Normal, initialName, &accepted)
            .trimmed();
    static const QRegularExpression IdentifierPattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (!accepted) {
        return;
    }
    if (!IdentifierPattern.match(newName).hasMatch()) {
        emit statusMessage(tr("Rename requires a valid C/C++ identifier."));
        return;
    }
    if (m_clangd->requestRename(path, line, character, newName) < 0) {
        emit statusMessage(tr("clangd could not start the rename request."));
    }
}

void CodeEditorWidget::requestFormatting() {
    QString path;
    int line = 0;
    int character = 0;
    if (!currentLanguageServerPosition(path, line, character) ||
        m_clangd->requestFormatting(path) < 0) {
        emit statusMessage(tr("clangd formatting is unavailable for this tab."));
    }
}

void CodeEditorWidget::showCompletionItems(const QString& filePath, const QStringList& items) {
    auto* editor = editorForPath(filePath);
    if (editor == nullptr || editor != currentEditor()) {
        return;
    }
    if (items.isEmpty()) {
        emit statusMessage(tr("clangd found no completions at the cursor."));
        return;
    }
    auto* menu = new QMenu(editor);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const qsizetype maximumVisibleItems = std::min<qsizetype>(items.size(), 100);
    const QPointer<CodeTextEdit> guardedEditor(editor);
    for (qsizetype index = 0; index < maximumVisibleItems; ++index) {
        const QString completion = items.at(index);
        QAction* action = menu->addAction(completion);
        connect(action, &QAction::triggered, menu, [guardedEditor, completion]() {
            if (guardedEditor == nullptr) {
                return;
            }
            QTextCursor cursor = guardedEditor->textCursor();
            cursor.select(QTextCursor::WordUnderCursor);
            cursor.insertText(completion);
            guardedEditor->setTextCursor(cursor);
        });
    }
    const QPoint popupPosition = editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft());
    menu->popup(popupPosition);
}

void CodeEditorWidget::showLocations(const QString& operation, const ClangdLocations& locations) {
    if (locations.isEmpty()) {
        emit statusMessage(operation == QStringLiteral("definition")
                               ? tr("clangd found no definition at the cursor.")
                               : tr("clangd found no references at the cursor."));
        return;
    }
    if (operation == QStringLiteral("definition")) {
        const ClangdLocation& location = locations.front();
        openFile(location.filePath, location.line + 1);
        if (auto* editor = editorForPath(location.filePath)) {
            QTextBlock block = editor->document()->findBlockByNumber(location.line);
            if (block.isValid()) {
                QTextCursor cursor(block);
                cursor.movePosition(
                    QTextCursor::Right, QTextCursor::MoveAnchor,
                    std::min(location.character, static_cast<int>(block.text().size())));
                editor->setTextCursor(cursor);
            }
        }
        emit statusMessage(tr("Opened clangd definition."));
        return;
    }

    m_findResults->setRowCount(0);
    for (const ClangdLocation& location : locations) {
        const int row = m_findResults->rowCount();
        m_findResults->insertRow(row);
        const QString relative = m_projectRoot.isEmpty()
                                     ? QFileInfo(location.filePath).fileName()
                                     : QDir(m_projectRoot).relativeFilePath(location.filePath);
        auto* fileItem = new QTableWidgetItem(QDir::toNativeSeparators(relative));
        fileItem->setData(Qt::UserRole, location.filePath);
        m_findResults->setItem(row, 0, fileItem);
        m_findResults->setItem(row, 1, new QTableWidgetItem(QString::number(location.line + 1)));
        m_findResults->setItem(
            row, 2,
            new QTableWidgetItem(tr("clangd reference, column %1").arg(location.character + 1)));
    }
    m_findResults->show();
    emit statusMessage(tr("clangd found %1 reference(s).").arg(locations.size()));
}

void CodeEditorWidget::showHover(const QString& filePath, const QString& contents) {
    auto* editor = editorForPath(filePath);
    if (editor == nullptr || editor != currentEditor()) {
        return;
    }
    if (contents.isEmpty()) {
        emit statusMessage(tr("clangd found no hover information at the cursor."));
        return;
    }
    QString escaped = contents.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    QToolTip::showText(editor->viewport()->mapToGlobal(editor->cursorRect().bottomRight()), escaped,
                       editor);
}

void CodeEditorWidget::applyLanguageServerFile(const QString& filePath, const QString& contents) {
    auto* editor = editorForPath(filePath);
    if (editor == nullptr) {
        return;
    }
    const int cursorPosition = editor->textCursor().position();
    (void)m_fileWatcher->removePath(filePath);
    editor->setPlainText(contents);
    QTextCursor cursor(editor->document());
    cursor.setPosition(std::clamp(cursorPosition, 0, editor->document()->characterCount() - 1));
    editor->setTextCursor(cursor);
    const QByteArray bytes = contents.toUtf8();
    editor->setProperty("diskDigest", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    editor->setProperty("externalChanged", false);
    editor->document()->setModified(false);
    updateTabTitle(editor);
    watchEditorFile(editor);
    scheduleSymbolRefresh();
}

void CodeEditorWidget::updateLanguageServerStatus(const ClangdClient::State state) {
    switch (state) {
    case ClangdClient::State::Stopped:
        m_clangdStatus->setText(m_projectRoot.isEmpty() ? tr("clangd: no project")
                                                        : tr("clangd: stopped"));
        break;
    case ClangdClient::State::Starting:
        m_clangdStatus->setText(tr("clangd: starting"));
        break;
    case ClangdClient::State::Initializing:
        m_clangdStatus->setText(tr("clangd: initializing"));
        break;
    case ClangdClient::State::Ready:
        m_clangdStatus->setText(tr("clangd: ready"));
        break;
    case ClangdClient::State::ShuttingDown:
        m_clangdStatus->setText(tr("clangd: stopping"));
        break;
    case ClangdClient::State::Failed:
        m_clangdStatus->setText(tr("clangd: unavailable"));
        break;
    }
}

bool CodeEditorWidget::reloadEditorFromDisk(CodeTextEdit* editor, QString& errorMessage) {
    if (editor == nullptr) {
        errorMessage = tr("The editor tab is unavailable.");
        return false;
    }
    const QString path = editor->property("filePath").toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage =
            tr("Cannot reload %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    const int line = editor->textCursor().blockNumber() + 1;
    editor->setPlainText(QString::fromUtf8(bytes));
    editor->document()->setModified(false);
    editor->setProperty("diskDigest", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    editor->setProperty("externalChanged", false);
    editor->goToLine(line);
    updateTabTitle(editor);
    watchEditorFile(editor);
    scheduleSymbolRefresh();
    synchronizeEditorWithLanguageServer(editor);
    errorMessage.clear();
    return true;
}

CodeTextEdit* CodeEditorWidget::editorForPath(const QString& filePath) const {
    const QString normalized = normalizedEditorPath(filePath);
    if (normalized.isEmpty()) {
        return nullptr;
    }
    for (int index = 0; index < m_tabs->count(); ++index) {
        auto* editor = editorAt(index);
        if (editor != nullptr &&
            normalizedEditorPath(editor->property("filePath").toString()) == normalized) {
            return editor;
        }
    }
    return nullptr;
}

bool CodeEditorWidget::isSourceFile(const QString& filePath) {
    static const QStringList SourceSuffixes = {
        QStringLiteral("c"),   QStringLiteral("cc"),  QStringLiteral("cpp"),
        QStringLiteral("cxx"), QStringLiteral("h"),   QStringLiteral("hh"),
        QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("ino")};
    return SourceSuffixes.contains(QFileInfo(filePath).suffix(), Qt::CaseInsensitive);
}

bool CodeEditorWidget::isIgnoredProjectPath(const QString& relativePath) {
    const QString portable = QDir::fromNativeSeparators(relativePath).toCaseFolded();
    const QStringList parts = portable.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    static const QStringList IgnoredDirectories = {
        QStringLiteral(".git"), QStringLiteral(".toolchains"), QStringLiteral(".downloads"),
        QStringLiteral("out"),  QStringLiteral("build"),       QStringLiteral("node_modules")};
    for (const auto& part : parts) {
        if (IgnoredDirectories.contains(part)) {
            return true;
        }
    }
    return false;
}

bool CodeEditorWidget::isSafeIndexedPath(const QString& projectRoot, const QString& filePath,
                                         const bool requireDirectory) {
    const QFileInfo rootInfo(projectRoot);
    const QFileInfo candidateInfo(filePath);
    if (!rootInfo.isDir() || !candidateInfo.exists() || isLinkLikePath(filePath) ||
        (requireDirectory ? !candidateInfo.isDir() : !candidateInfo.isFile())) {
        return false;
    }

    const QString root = normalizedEditorPath(rootInfo.canonicalFilePath());
    const QString candidate = normalizedEditorPath(candidateInfo.canonicalFilePath());
    if (root.isEmpty() || candidate.isEmpty()) {
        return false;
    }
    QString prefix = root;
    prefix.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!prefix.endsWith(QLatin1Char('/'))) {
        prefix += QLatin1Char('/');
    }
    QString portableCandidate = candidate;
    portableCandidate.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!portableCandidate.startsWith(prefix)) {
        return false;
    }

    const QString lexicalRelative =
        QDir(rootInfo.canonicalFilePath()).relativeFilePath(candidateInfo.absoluteFilePath());
    if (QDir::fromNativeSeparators(lexicalRelative)
            .split(QLatin1Char('/'))
            .contains(QStringLiteral(".."))) {
        return false;
    }
    QString current = rootInfo.canonicalFilePath();
    const auto parts =
        QDir::fromNativeSeparators(lexicalRelative).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        current = QDir(current).filePath(part);
        if (isLinkLikePath(current)) {
            return false;
        }
    }
    return true;
}

bool CodeEditorWidget::isLinkLikePath(const QString& filePath) {
    const QFileInfo info(filePath);
    if (info.isSymLink()) {
        return true;
    }
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    return false;
#endif
}

bool CodeEditorWidget::isSafeWritableProjectPath(const QString& projectRoot,
                                                 const QString& filePath, QString& canonicalTarget,
                                                 QString& errorMessage) {
    canonicalTarget.clear();
    errorMessage.clear();
    const QFileInfo rootInfo(projectRoot);
    const QString root = QDir::cleanPath(rootInfo.canonicalFilePath());
    if (!rootInfo.isDir() || root.isEmpty()) {
        errorMessage = tr("The project root is unavailable or not canonical.");
        return false;
    }
    if (filePath.trimmed().isEmpty()) {
        errorMessage = tr("The save destination is empty.");
        return false;
    }

    const QFileInfo requestedInfo(filePath);
    const QString requested = QDir::cleanPath(requestedInfo.absoluteFilePath());
    const QString fileName = requestedInfo.fileName();
    if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")) {
        errorMessage = tr("The save destination does not name a file.");
        return false;
    }
#ifdef Q_OS_WIN
    if (fileName.contains(QLatin1Char(':'))) {
        errorMessage = tr("Windows alternate data stream destinations are not allowed.");
        return false;
    }
#endif

    QString comparisonRoot = QDir::fromNativeSeparators(root);
    QString comparisonRequested = QDir::fromNativeSeparators(requested);
#ifdef Q_OS_WIN
    comparisonRoot = comparisonRoot.toCaseFolded();
    comparisonRequested = comparisonRequested.toCaseFolded();
#endif
    QString rootPrefix = comparisonRoot;
    if (!rootPrefix.endsWith(QLatin1Char('/'))) {
        rootPrefix += QLatin1Char('/');
    }
    if (!comparisonRequested.startsWith(rootPrefix)) {
        errorMessage = tr("Refused to save outside the canonical project root.");
        return false;
    }

    const QString lexicalRelative = QDir(root).relativeFilePath(requested);
    const QStringList lexicalParts =
        QDir::fromNativeSeparators(lexicalRelative).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (lexicalParts.isEmpty() || lexicalParts.contains(QStringLiteral(".."))) {
        errorMessage = tr("The save destination contains path traversal.");
        return false;
    }
    QString current = root;
    for (qsizetype index = 0; index + 1 < lexicalParts.size(); ++index) {
        current = QDir(current).filePath(lexicalParts.at(index));
        const QFileInfo segment(current);
        if (!segment.exists() || !segment.isDir() || isLinkLikePath(current)) {
            errorMessage = tr("A save destination directory is missing or uses a symlink, "
                              "junction, or reparse point.");
            return false;
        }
    }

    const QFileInfo parentInfo(requestedInfo.absolutePath());
    const QString canonicalParent = QDir::cleanPath(parentInfo.canonicalFilePath());
    if (!parentInfo.isDir() || canonicalParent.isEmpty() ||
        isLinkLikePath(parentInfo.absoluteFilePath())) {
        errorMessage = tr("The save destination parent is not a canonical local directory.");
        return false;
    }
    QString comparisonParent = QDir::fromNativeSeparators(canonicalParent);
#ifdef Q_OS_WIN
    comparisonParent = comparisonParent.toCaseFolded();
#endif
    if (comparisonParent != comparisonRoot && !comparisonParent.startsWith(rootPrefix)) {
        errorMessage = tr("The save destination resolves outside the canonical project root.");
        return false;
    }

    canonicalTarget = QDir::cleanPath(QDir(canonicalParent).filePath(fileName));
    const QFileInfo targetInfo(canonicalTarget);
    if (targetInfo.exists()) {
        if (!targetInfo.isFile() || isLinkLikePath(canonicalTarget)) {
            canonicalTarget.clear();
            errorMessage = tr("The save destination is not a regular canonical file.");
            return false;
        }
        const QString existingCanonical = QDir::cleanPath(targetInfo.canonicalFilePath());
        if (existingCanonical.isEmpty() ||
            normalizedEditorPath(existingCanonical) != normalizedEditorPath(canonicalTarget)) {
            canonicalTarget.clear();
            errorMessage = tr("The save destination changed canonical identity.");
            return false;
        }
    }
    return true;
}

QByteArray CodeEditorWidget::fileDigest(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result();
}

} // namespace fgl::studio
