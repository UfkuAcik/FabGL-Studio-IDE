#pragma once

#include <fabgl/animation/animation_authoring.h>
#include <fabgl/visual/visual_graph.h>

#include <performance_budget.h>

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QSlider;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QUndoStack;

namespace fgl::studio {

class SceneDocument;

class VisualScriptEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit VisualScriptEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] qsizetype nodeCount() const noexcept;
    [[nodiscard]] qsizetype edgeCount() const noexcept;
    [[nodiscard]] qsizetype validationIssueCount() const noexcept;
    [[nodiscard]] bool hasValidationErrors() const noexcept;
    [[nodiscard]] QString graphFilePath() const;
    [[nodiscard]] bool graphModified() const noexcept;
    [[nodiscard]] qsizetype commentCount() const noexcept;
    [[nodiscard]] qsizetype selectedCanvasNodeCount() const noexcept;
    [[nodiscard]] double canvasScale() const noexcept;
    [[nodiscard]] bool canUndoGraphEdit() const noexcept;
    [[nodiscard]] bool canRedoGraphEdit() const noexcept;
    [[nodiscard]] qsizetype debugTraceNodeCount() const noexcept;
    [[nodiscard]] fabgl::VisualNodeId activeDebugNodeId() const noexcept;
    [[nodiscard]] bool executeDebugPreview(QString& errorMessage);
    void newGraph();
    [[nodiscard]] bool openGraphFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool saveGraphFile(const QString& filePath, QString& errorMessage);

  public slots:
    void validateGraph();

  signals:
    void statusMessage(const QString& message);

  private:
    [[nodiscard]] fabgl::VisualNode makeNode(fabgl::VisualNodeKind kind);
    [[nodiscard]] bool buildGraph(fabgl::VisualGraph& graph, QString& errorMessage) const;
    void addNode();
    void removeSelectedNode();
    void addConnection();
    void removeSelectedConnection();
    void refreshNodeList();
    void refreshConnectionTable();
    void refreshNodeEditors();
    void refreshPinCombos();
    void applySelectedNodeEdits();
    void refreshCanvas();
    void addCommentBox();
    void removeSelectedCanvasItems();
    void copySelectedCanvasItems();
    void pasteCanvasItems();
    void handleCanvasConnection(fabgl::VisualNodeId sourceNode, fabgl::VisualPinId sourcePin,
                                fabgl::VisualNodeId targetNode, fabgl::VisualPinId targetPin);
    void updateNodeLayout(fabgl::VisualNodeId nodeId, float x, float y);
    void updateCommentLayout(std::uint16_t commentId, float x, float y);
    void beginLayoutTransaction();
    void endLayoutTransaction();
    void recordUndoPoint();
    void undoGraphEdit();
    void redoGraphEdit();
    void clearGraphHistory();
    void openGraphDialog();
    void saveGraphDialog(bool saveAs);
    void setGraphModified(bool modified);
    void updateGraphFileStatus();
    [[nodiscard]] fabgl::VisualNode* selectedNode();
    [[nodiscard]] const fabgl::VisualNode* selectedNode() const;
    [[nodiscard]] fabgl::VisualNodeId selectedNodeId(const QComboBox* combo) const;
    [[nodiscard]] fabgl::VisualPinId selectedPinId(const QComboBox* combo) const;

    struct GraphSnapshot final {
        std::vector<fabgl::VisualNode> nodes;
        std::vector<fabgl::VisualEdge> edges;
        std::vector<fabgl::VisualCommentBox> comments;
        fabgl::VisualNodeId nextNodeId = 1;
        std::uint16_t nextCommentId = 1;
        fabgl::VisualNodeId entryNodeId = 0;
        fabgl::AssetGuid graphGuid;
        std::string graphName;
        bool modified = false;
    };

    [[nodiscard]] GraphSnapshot graphSnapshot() const;
    void restoreGraphSnapshot(GraphSnapshot snapshot);
    void updateGraphHistoryActions();
    void buildLocalDebugTrace(const fabgl::VisualGraph& graph);
    void advanceDebugTrace();
    void clearDebugTrace();
    void updateDebugHighlight();

    std::vector<fabgl::VisualNode> m_nodes;
    std::vector<fabgl::VisualEdge> m_edges;
    std::vector<fabgl::VisualCommentBox> m_comments;
    fabgl::VisualNodeId m_nextNodeId = 1;
    std::uint16_t m_nextCommentId = 1;
    fabgl::VisualNodeId m_entryNodeId = 0;
    fabgl::AssetGuid m_graphGuid;
    std::string m_graphName;
    QString m_graphFilePath;
    bool m_graphModified = false;
    bool m_updating = false;
    bool m_hasErrors = false;
    qsizetype m_validationIssueCount = 0;
    std::vector<fabgl::VisualNode> m_clipboardNodes;
    std::vector<fabgl::VisualEdge> m_clipboardEdges;
    std::vector<fabgl::VisualCommentBox> m_clipboardComments;
    std::vector<GraphSnapshot> m_undoHistory;
    std::vector<GraphSnapshot> m_redoHistory;
    std::optional<GraphSnapshot> m_layoutTransactionStart;
    bool m_layoutTransactionChanged = false;
    std::vector<fabgl::VisualNodeId> m_debugTrace;
    std::size_t m_debugTraceIndex = 0;
    QString m_nodeEditorError;

    QTreeWidget* m_nodeTree = nullptr;
    QLineEdit* m_nodeSearch = nullptr;
    QComboBox* m_kindCombo = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QDoubleSpinBox* m_numberEdit = nullptr;
    QCheckBox* m_booleanEdit = nullptr;
    QLineEdit* m_variableEdit = nullptr;
    QComboBox* m_callbackEdit = nullptr;
    QLineEdit* m_payloadEdit = nullptr;
    QLineEdit* m_assetReferenceEdit = nullptr;
    QLineEdit* m_entityReferenceEdit = nullptr;
    QLineEdit* m_componentReferenceEdit = nullptr;
    QComboBox* m_sourceNodeCombo = nullptr;
    QComboBox* m_sourcePinCombo = nullptr;
    QComboBox* m_targetNodeCombo = nullptr;
    QComboBox* m_targetPinCombo = nullptr;
    QTableWidget* m_connectionTable = nullptr;
    QTableWidget* m_validationTable = nullptr;
    QLabel* m_compileStatus = nullptr;
    QLabel* m_graphFileStatus = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_redoButton = nullptr;
    QPushButton* m_debugRunButton = nullptr;
    QLabel* m_debugStatus = nullptr;
    QTimer* m_debugTimer = nullptr;
    QGraphicsScene* m_graphScene = nullptr;
    QGraphicsView* m_graphView = nullptr;
};

class AnimatorEditorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit AnimatorEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] int stateCount() const noexcept;
    [[nodiscard]] int parameterCount() const noexcept;
    [[nodiscard]] int transitionCount() const noexcept;
    [[nodiscard]] QString validationText() const;
    [[nodiscard]] QString assetFilePath() const;
    [[nodiscard]] bool assetModified() const noexcept;
    [[nodiscard]] QString previewText() const;
    [[nodiscard]] bool canUndoAssetEdit() const noexcept;
    [[nodiscard]] bool canRedoAssetEdit() const noexcept;
    [[nodiscard]] bool openAssetFile(const QString& filePath, QString& errorMessage);
    [[nodiscard]] bool saveAssetFile(const QString& filePath, QString& errorMessage);
    void newController();
    void newAnimationClip();

  public slots:
    void validateController();

  signals:
    void statusMessage(const QString& message);

  private:
    void addState();
    void removeSelectedState();
    void addParameter();
    void removeSelectedParameter();
    void addTransition();
    void removeSelectedTransition();
    void createInitialController();
    void refreshControllerTables();
    void refreshClipTables();
    [[nodiscard]] bool collectControllerFromTables(QString& errorMessage);
    [[nodiscard]] bool collectClipFromTables(QString& errorMessage);
    void updatePreview();
    void openAssetDialog();
    void saveAssetDialog(bool saveAs);
    void setAssetModified(bool modified);
    void updateAssetStatus();
    void addClipKey();
    void removeSelectedClipKey();
    void addClipEvent();
    void removeSelectedClipEvent();
    void loadClipLibraryAround(const QString& filePath);
    void undoAssetEdit();
    void redoAssetEdit();

    enum class AssetMode { Controller, AnimationClip };

    struct AnimatorSnapshot final {
        AssetMode mode = AssetMode::Controller;
        QString controllerName;
        QString controllerGuid;
        QString initialState;
        QVector<QStringList> states;
        QVector<QStringList> parameters;
        QVector<QStringList> transitions;
        QVector<qlonglong> transitionGroups;
        QString clipName;
        QString clipGuid;
        double clipDuration = 1.0;
        bool clipLooping = false;
        QVector<QStringList> keys;
        QVector<QStringList> events;
        bool modified = false;
    };

    [[nodiscard]] AnimatorSnapshot animatorSnapshot() const;
    void restoreAnimatorSnapshot(const AnimatorSnapshot& snapshot);
    void recordAnimatorEdit(const QString& description);
    void resetAnimatorHistory();

    AssetMode m_assetMode = AssetMode::Controller;
    fabgl::AnimatorControllerAsset m_controllerAsset;
    fabgl::AnimationClipAsset m_clipAsset;
    std::map<fabgl::AssetGuid, fabgl::AnimationClipAsset> m_clipLibrary;
    QString m_assetFilePath;
    bool m_assetModified = false;

    QTableWidget* m_states = nullptr;
    QTableWidget* m_parameters = nullptr;
    QTableWidget* m_transitions = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_assetStatus = nullptr;
    QLabel* m_preview = nullptr;
    QStackedWidget* m_assetPages = nullptr;
    QLineEdit* m_controllerName = nullptr;
    QLineEdit* m_controllerGuid = nullptr;
    QLineEdit* m_initialState = nullptr;
    QLineEdit* m_clipName = nullptr;
    QLineEdit* m_clipGuid = nullptr;
    QDoubleSpinBox* m_clipDuration = nullptr;
    QCheckBox* m_clipLooping = nullptr;
    QTableWidget* m_clipKeys = nullptr;
    QTableWidget* m_clipEvents = nullptr;
    QSlider* m_timeline = nullptr;
    QPushButton* m_previewPlayButton = nullptr;
    QTimer* m_previewTimer = nullptr;
    bool m_updating = false;
    bool m_restoringHistory = false;
    bool m_historyReady = false;
    AnimatorSnapshot m_lastAnimatorSnapshot;
    QUndoStack* m_assetUndoStack = nullptr;
    QPushButton* m_assetUndoButton = nullptr;
    QPushButton* m_assetRedoButton = nullptr;
};

class MemoryAnalyzerWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit MemoryAnalyzerWidget(SceneDocument* document, QWidget* parent = nullptr);

    void setProjectRoot(const QString& projectRoot);
    void setPerformanceBudgets(const fabgl::project::PerformanceBudgetSettings& settings);
    void setEsp32StorageEstimates(std::uint64_t flashBytes, std::uint64_t internalRamBytes,
                                  std::uint64_t psramBytes, std::uint64_t sdBytes);
    void setMeasuredPcResidentBytes(std::optional<std::uint64_t> bytes);
    [[nodiscard]] std::size_t entityCount() const noexcept;
    [[nodiscard]] std::size_t componentCount() const noexcept;
    [[nodiscard]] std::size_t assetCount() const noexcept;
    [[nodiscard]] std::size_t assetBytes() const noexcept;
    [[nodiscard]] std::size_t estimatedRuntimeBytes() const noexcept;

  public slots:
    void refresh();

  signals:
    void statusMessage(const QString& message);
    void performanceProfilesChanged(int pcProfile, int esp32Profile);

  private:
    SceneDocument* m_document = nullptr;
    QString m_projectRoot;
    QTableWidget* m_summary = nullptr;
    QComboBox* m_pcProfile = nullptr;
    QComboBox* m_esp32Profile = nullptr;
    QProgressBar* m_pcBudgetBar = nullptr;
    QProgressBar* m_esp32BudgetBar = nullptr;
    QLabel* m_status = nullptr;
    std::size_t m_entityCount = 0;
    std::size_t m_componentCount = 0;
    std::size_t m_assetCount = 0;
    std::size_t m_assetBytes = 0;
    std::size_t m_estimatedRuntimeBytes = 0;
    fabgl::project::PerformanceBudgetSettings m_performance;
    std::optional<std::uint64_t> m_measuredPcResidentBytes;
    std::uint64_t m_flashEstimate = 0U;
    std::uint64_t m_internalRamEstimate = 0U;
    std::uint64_t m_psramEstimate = 0U;
    std::uint64_t m_sdEstimate = 0U;
};

} // namespace fgl::studio
