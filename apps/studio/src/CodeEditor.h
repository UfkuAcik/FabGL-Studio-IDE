#pragma once

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QWidget>

#include <vector>

class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QSpinBox;
class QTabWidget;
class QTextDocument;

namespace fgl::studio {

class CodeTextEdit;

class LineNumberArea final : public QWidget {
  public:
    explicit LineNumberArea(CodeTextEdit* editor);
    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    CodeTextEdit* m_editor = nullptr;
};

class CodeTextEdit final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit CodeTextEdit(QWidget* parent = nullptr);
    [[nodiscard]] int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void goToLine(int line);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

    LineNumberArea* m_lineNumberArea = nullptr;
};

class CppHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT

  public:
    explicit CppHighlighter(QTextDocument* document);

  protected:
    void highlightBlock(const QString& text) override;

  private:
    struct Rule final {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    std::vector<Rule> m_rules;
    QRegularExpression m_commentStart;
    QRegularExpression m_commentEnd;
    QTextCharFormat m_commentFormat;
};

class DiagnosticOutputEdit final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit DiagnosticOutputEdit(QWidget* parent = nullptr);

  signals:
    void diagnosticActivated(const QString& filePath, int line);

  protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
};

class CodeEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit CodeEditorWidget(QWidget* parent = nullptr);

    void openFile(const QString& filePath, int line = 0);
    [[nodiscard]] bool maybeSaveAll();
    [[nodiscard]] bool hasUnsavedFiles() const;

  signals:
    void statusMessage(const QString& message);

  private:
    [[nodiscard]] CodeTextEdit* currentEditor() const;
    [[nodiscard]] CodeTextEdit* editorAt(int index) const;
    void createUntitledTab();
    void openFileDialog();
    [[nodiscard]] bool saveEditor(CodeTextEdit* editor, bool saveAs = false);
    void saveCurrent();
    void closeTab(int index);
    void updateTabTitle(CodeTextEdit* editor);
    void findNext(bool backwards = false);
    void replaceOne();
    void replaceAll();
    void goToRequestedLine();

    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QSpinBox* m_lineSpin = nullptr;
    int m_untitledCounter = 0;
};

} // namespace fgl::studio
