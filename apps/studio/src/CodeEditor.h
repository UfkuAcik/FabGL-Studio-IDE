#pragma once

#include "ClangdClient.h"

#include <QByteArray>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

class QFileSystemWatcher;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTextDocument;
class QTimer;
class QTreeWidget;

namespace fgl::studio {

enum class BuildOutputSeverity;

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
    [[nodiscard]] std::optional<std::pair<int, int>> matchingBracketPositions() const;
    void setLspDiagnostics(const ClangdDiagnostics& diagnostics);
    [[nodiscard]] qsizetype lspDiagnosticCount() const noexcept;

  protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

    LineNumberArea* m_lineNumberArea = nullptr;
    ClangdDiagnostics m_lspDiagnostics;
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
    void appendOutput(const QString& text, BuildOutputSeverity severity);

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
    void setProjectRoot(const QString& projectRoot);
    [[nodiscard]] QString projectRoot() const;
    [[nodiscard]] qsizetype projectFileCount() const noexcept;
    [[nodiscard]] qsizetype symbolCount() const noexcept;
    [[nodiscard]] QStringList symbolNames() const;
    [[nodiscard]] int findInFiles(const QString& needle);
    void scanForExternalChanges();
    [[nodiscard]] bool hasExternalChange(const QString& filePath) const;
    [[nodiscard]] bool reloadFileFromDisk(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool maybeSaveAll();
    [[nodiscard]] bool hasUnsavedFiles() const;
    [[nodiscard]] bool saveCurrentFileAs(const QString& filePath, QString& errorMessage);
    [[nodiscard]] ClangdClient* clangdClient() const noexcept;

  signals:
    void statusMessage(const QString& message);
    void fileSaved(const QString& filePath);
    void externalFileChanged(const QString& filePath, bool localChangesKept);

  private:
    [[nodiscard]] CodeTextEdit* currentEditor() const;
    [[nodiscard]] CodeTextEdit* editorAt(int index) const;
    void createUntitledTab();
    void openFileDialog();
    [[nodiscard]] bool saveEditor(CodeTextEdit* editor, bool saveAs = false);
    [[nodiscard]] bool saveEditorToPath(CodeTextEdit* editor, const QString& filePath,
                                        QString& errorMessage);
    void saveCurrent();
    void closeTab(int index);
    void updateTabTitle(CodeTextEdit* editor);
    void findNext(bool backwards = false);
    void replaceOne();
    void replaceAll();
    void goToRequestedLine();
    void refreshProjectFiles();
    void refreshSymbols();
    void handleWatchedFileChanged(const QString& filePath);
    void scheduleProjectRefresh();
    void scheduleSymbolRefresh();
    void watchEditorFile(CodeTextEdit* editor);
    void connectEditorToLanguageServer(CodeTextEdit* editor);
    void synchronizeEditorWithLanguageServer(CodeTextEdit* editor);
    [[nodiscard]] bool currentLanguageServerPosition(QString& filePath, int& line,
                                                     int& character) const;
    void requestCompletion();
    void requestDefinition();
    void requestReferences();
    void requestHover();
    void requestRename();
    void requestFormatting();
    void showCompletionItems(const QString& filePath, const QStringList& items);
    void showLocations(const QString& operation, const ClangdLocations& locations);
    void showHover(const QString& filePath, const QString& contents);
    void applyLanguageServerFile(const QString& filePath, const QString& contents);
    void updateLanguageServerStatus(ClangdClient::State state);
    [[nodiscard]] bool reloadEditorFromDisk(CodeTextEdit* editor, QString& errorMessage);
    [[nodiscard]] CodeTextEdit* editorForPath(const QString& filePath) const;
    [[nodiscard]] static bool isSourceFile(const QString& filePath);
    [[nodiscard]] static bool isIgnoredProjectPath(const QString& relativePath);
    [[nodiscard]] static bool isSafeIndexedPath(const QString& projectRoot, const QString& filePath,
                                                bool requireDirectory = false);
    [[nodiscard]] static bool isLinkLikePath(const QString& filePath);
    [[nodiscard]] static bool isSafeWritableProjectPath(const QString& projectRoot,
                                                        const QString& filePath,
                                                        QString& canonicalTarget,
                                                        QString& errorMessage);
    [[nodiscard]] static QByteArray fileDigest(const QString& filePath);

    struct SymbolRecord final {
        QString name;
        QString kind;
        QString filePath;
        int line = 0;
    };

    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_projectFilesTree = nullptr;
    QTreeWidget* m_symbolTree = nullptr;
    QTableWidget* m_findResults = nullptr;
    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QSpinBox* m_lineSpin = nullptr;
    QLabel* m_clangdStatus = nullptr;
    QFileSystemWatcher* m_fileWatcher = nullptr;
    QTimer* m_projectRefreshTimer = nullptr;
    QTimer* m_symbolRefreshTimer = nullptr;
    ClangdClient* m_clangd = nullptr;
    QString m_projectRoot;
    QStringList m_projectFiles;
    std::vector<SymbolRecord> m_symbols;
    int m_untitledCounter = 0;
};

} // namespace fgl::studio
