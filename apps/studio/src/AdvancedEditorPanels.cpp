#include "AdvancedEditorPanels.h"

#include "SceneDocument.h"

#include <fabgl/animation/animation.h>
#include <fabgl/animation/animation_authoring.h>
#include <fabgl/project/project_visual_host.h>
#include <fabgl/scene/entity.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

namespace fgl::studio {
namespace {

template <typename Guid>
bool parseOptionalGuid(const QString& text, std::optional<Guid>& value, QString& error,
                       const QString& label) {
    const auto normalized = text.trimmed();
    if (normalized.isEmpty()) {
        value.reset();
        return true;
    }
    auto parsed = Guid::parse(normalized.toStdString());
    if (!parsed || parsed.value().isNil()) {
        error = QObject::tr("%1 must be a non-nil GUID in canonical form.").arg(label);
        return false;
    }
    value = parsed.value();
    return true;
}

QString visualNodeAuthoringError(const fabgl::VisualNode& node) {
    using Type = fabgl::VisualBuiltinNodeType;
    if (node.callbackPayload.size() > 256U)
        return QObject::tr("Callback payload exceeds the desktop host limit of 256 bytes.");
    const auto payload = QString::fromStdString(node.callbackPayload).trimmed();
    switch (node.builtinType) {
    case Type::FunctionCall: {
        const auto& callbacks = fabgl::project::ProjectVisualHost::safeFunctionCallbacks();
        if (std::find(callbacks.cbegin(), callbacks.cend(), node.callbackName) == callbacks.cend())
            return QObject::tr("Function callback is not one of the bounded desktop built-ins.");
        break;
    }
    case Type::InputAction: {
        if (payload.isEmpty())
            return QObject::tr("Input payload must name an action or axis.");
        const auto separator = payload.indexOf(QLatin1Char(':'));
        if (separator >= 0) {
            const auto mode = payload.left(separator);
            const auto name = payload.mid(separator + 1);
            if (name.isEmpty() ||
                (mode != QStringLiteral("held") && mode != QStringLiteral("pressed") &&
                 mode != QStringLiteral("released") && mode != QStringLiteral("axis")))
                return QObject::tr("Input payload must be name, held:name, pressed:name, "
                                   "released:name, or axis:name.");
        }
        break;
    }
    case Type::EntityAction:
        if (!QStringList{QStringLiteral("set_active"), QStringLiteral("toggle_active"),
                         QStringLiteral("translate_x"), QStringLiteral("translate_y"),
                         QStringLiteral("translate_z"), QStringLiteral("set_position_x"),
                         QStringLiteral("set_position_y"), QStringLiteral("set_position_z")}
                 .contains(payload.isEmpty() ? QStringLiteral("set_active") : payload))
            return QObject::tr("Entity payload is not supported by the desktop host.");
        break;
    case Type::ComponentAction:
        if (payload.isEmpty() || payload == QStringLiteral("set_enabled") ||
            payload == QStringLiteral("toggle_enabled"))
            break;
        if ((!payload.startsWith(QStringLiteral("set:")) &&
             !payload.startsWith(QStringLiteral("add:"))) ||
            payload.mid(4).trimmed().isEmpty())
            return QObject::tr("Component payload must be set_enabled, toggle_enabled, "
                               "set:property, or add:property.");
        break;
    case Type::AudioPlay:
        if (!QStringList{QString{}, QStringLiteral("sfx"), QStringLiteral("sfx.loop"),
                         QStringLiteral("music"), QStringLiteral("music.loop"),
                         QStringLiteral("ui"), QStringLiteral("ui.loop")}
                 .contains(payload))
            return QObject::tr("Audio payload must select sfx, music, or ui, optionally .loop.");
        break;
    case Type::AnimationPlay:
        if (payload.isEmpty())
            return QObject::tr("Animation payload must name an Animator state.");
        break;
    case Type::UiAction:
        if (!QStringList{QString{}, QStringLiteral("set_value"),
                         QStringLiteral("set_checked"), QStringLiteral("select_index"),
                         QStringLiteral("set_scale")}
                 .contains(payload))
            return QObject::tr("UI payload is not supported by the desktop host.");
        break;
    default:
        break;
    }
    return {};
}

QString nodeKindName(const fabgl::VisualNodeKind kind) {
    using enum fabgl::VisualNodeKind;
    switch (kind) {
    case Entry:
        return QObject::tr("Entry");
    case ConstantNumber:
        return QObject::tr("Number");
    case ConstantBoolean:
        return QObject::tr("Boolean");
    case GetVariable:
        return QObject::tr("Get Variable");
    case Add:
        return QObject::tr("Add");
    case Multiply:
        return QObject::tr("Multiply");
    case Less:
        return QObject::tr("Less Than");
    case Return:
        return QObject::tr("Return");
    case AssetReference:
        return QObject::tr("Asset Reference");
    case EntityReference:
        return QObject::tr("Entity Reference");
    }
    return QObject::tr("Unknown");
}

QString nodeTypeName(const fabgl::VisualNode& node) {
    if (node.builtinType != fabgl::VisualBuiltinNodeType::Legacy) {
        if (const auto* definition = fabgl::VisualNodeRegistry::builtins().find(node.builtinType)) {
            return QString::fromStdString(definition->displayName);
        }
    }
    return nodeKindName(node.kind);
}

QString valueTypeName(const fabgl::VisualValueType type) {
    using enum fabgl::VisualValueType;
    switch (type) {
    case Flow:
        return QObject::tr("flow");
    case Number:
        return QObject::tr("number");
    case Boolean:
        return QObject::tr("boolean");
    }
    return QObject::tr("unknown");
}

QString animationError(const fabgl::Error& error) {
    return QString::fromStdString(error.message());
}

QTableWidgetItem* editableItem(const QString& text) {
    return new QTableWidgetItem(text);
}

QString tableText(const QTableWidget* table, const int row, const int column) {
    const auto* item = table->item(row, column);
    return item != nullptr ? item->text().trimmed() : QString{};
}

bool textBoolean(const QString& text, const bool fallback = false) {
    if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 ||
        text == QStringLiteral("1") ||
        text.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 ||
        text == QStringLiteral("0") ||
        text.compare(QStringLiteral("no"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return fallback;
}

QString formattedBytes(const std::size_t bytes) {
    constexpr double KiB = 1024.0;
    constexpr double MiB = KiB * KiB;
    if (bytes >= static_cast<std::size_t>(MiB)) {
        return QObject::tr("%1 MiB").arg(static_cast<double>(bytes) / MiB, 0, 'f', 2);
    }
    if (bytes >= static_cast<std::size_t>(KiB)) {
        return QObject::tr("%1 KiB").arg(static_cast<double>(bytes) / KiB, 0, 'f', 1);
    }
    return QObject::tr("%1 B").arg(static_cast<qulonglong>(bytes));
}

constexpr int CanvasKindRole = 0;
constexpr int CanvasIdRole = 1;
constexpr int CanvasPinRole = 2;
constexpr int CanvasDirectionRole = 3;
constexpr int CanvasDebugActiveRole = 4;
constexpr int CanvasNodeKind = 1;
constexpr int CanvasCommentKind = 2;
constexpr int CanvasPortKind = 3;

class MovableCanvasRect : public QGraphicsRectItem {
  public:
    using MoveCallback = std::function<void(QPointF)>;
    using TransactionCallback = std::function<void()>;

    MovableCanvasRect(const QRectF& rectangle, MoveCallback moved, TransactionCallback beginMove,
                      TransactionCallback endMove)
        : QGraphicsRectItem(rectangle), moved_(std::move(moved)), beginMove_(std::move(beginMove)),
          endMove_(std::move(endMove)) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    }

  protected:
    QVariant itemChange(const GraphicsItemChange change, const QVariant& value) override {
        const auto result = QGraphicsRectItem::itemChange(change, value);
        if (change == ItemPositionHasChanged && moved_)
            moved_(value.toPointF());
        return result;
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && beginMove_)
            beginMove_();
        QGraphicsRectItem::mousePressEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsRectItem::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && endMove_)
            endMove_();
    }

  private:
    MoveCallback moved_;
    TransactionCallback beginMove_;
    TransactionCallback endMove_;
};

class VisualGraphView final : public QGraphicsView {
  public:
    explicit VisualGraphView(QWidget* parent = nullptr) : QGraphicsView(parent) {
        setDragMode(RubberBandDrag);
        setRenderHint(QPainter::Antialiasing);
        setTransformationAnchor(AnchorUnderMouse);
        setResizeAnchor(AnchorViewCenter);
    }

    std::function<void()> copyRequested;
    std::function<void()> pasteRequested;
    std::function<void()> deleteRequested;
    std::function<void()> undoRequested;
    std::function<void()> redoRequested;
    std::function<void(fabgl::VisualNodeId, fabgl::VisualPinId, fabgl::VisualNodeId,
                       fabgl::VisualPinId)>
        connectionRequested;

    [[nodiscard]] double scaleFactor() const noexcept {
        return scaleFactor_;
    }

  protected:
    void wheelEvent(QWheelEvent* event) override {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
        const double next = std::clamp(scaleFactor_ * factor, 0.25, 4.0);
        const double applied = next / scaleFactor_;
        scale(applied, applied);
        scaleFactor_ = next;
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->matches(QKeySequence::Undo) && undoRequested) {
            undoRequested();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Redo) && redoRequested) {
            redoRequested();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Copy) && copyRequested) {
            copyRequested();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Paste) && pasteRequested) {
            pasteRequested();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Delete && deleteRequested) {
            deleteRequested();
            event->accept();
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            panOrigin_ = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            if (auto* port = portItemAt(event->position().toPoint()); port != nullptr) {
                connectionStart_ = port;
                const QPointF start = port->sceneBoundingRect().center();
                connectionPreview_ =
                    scene()->addLine(QLineF(start, start), QPen(QColor("#7cc7ff"), 2.0));
                connectionPreview_->setZValue(1000.0);
                event->accept();
                return;
            }
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            const QPointF delta = event->position() - panOrigin_;
            panOrigin_ = event->position();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                            static_cast<int>(delta.x()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() -
                                          static_cast<int>(delta.y()));
            event->accept();
            return;
        }
        if (connectionPreview_ != nullptr && connectionStart_ != nullptr) {
            connectionPreview_->setLine(
                QLineF(connectionStart_->sceneBoundingRect().center(), mapToScene(event->pos())));
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton && panning_) {
            panning_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && connectionStart_ != nullptr) {
            auto* target = portItemAt(event->position().toPoint());
            if (connectionPreview_ != nullptr) {
                scene()->removeItem(connectionPreview_);
                delete connectionPreview_;
            }
            connectionPreview_ = nullptr;
            if (target != nullptr && target != connectionStart_ && connectionRequested) {
                auto* source = connectionStart_;
                if (source->data(CanvasDirectionRole).toInt() ==
                    static_cast<int>(fabgl::VisualPinDirection::Input)) {
                    std::swap(source, target);
                }
                if (source->data(CanvasDirectionRole).toInt() ==
                        static_cast<int>(fabgl::VisualPinDirection::Output) &&
                    target->data(CanvasDirectionRole).toInt() ==
                        static_cast<int>(fabgl::VisualPinDirection::Input)) {
                    connectionRequested(
                        static_cast<fabgl::VisualNodeId>(source->data(CanvasIdRole).toUInt()),
                        static_cast<fabgl::VisualPinId>(source->data(CanvasPinRole).toUInt()),
                        static_cast<fabgl::VisualNodeId>(target->data(CanvasIdRole).toUInt()),
                        static_cast<fabgl::VisualPinId>(target->data(CanvasPinRole).toUInt()));
                }
            }
            connectionStart_ = nullptr;
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

  private:
    QGraphicsItem* portItemAt(const QPoint& position) const {
        auto* item = itemAt(position);
        while (item != nullptr && item->data(CanvasKindRole).toInt() != CanvasPortKind)
            item = item->parentItem();
        return item;
    }

    bool panning_ = false;
    QPointF panOrigin_;
    double scaleFactor_ = 1.0;
    QGraphicsItem* connectionStart_ = nullptr;
    QGraphicsLineItem* connectionPreview_ = nullptr;
};

class CallbackUndoCommand final : public QUndoCommand {
  public:
    CallbackUndoCommand(QString text, std::function<void()> undoCallback,
                        std::function<void()> redoCallback)
        : QUndoCommand(std::move(text)), undoCallback_(std::move(undoCallback)),
          redoCallback_(std::move(redoCallback)) {}

    void undo() override {
        if (undoCallback_)
            undoCallback_();
    }

    void redo() override {
        if (redoCallback_)
            redoCallback_();
    }

  private:
    std::function<void()> undoCallback_;
    std::function<void()> redoCallback_;
};

} // namespace

VisualScriptEditorWidget::VisualScriptEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("visualScriptEditor"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);

    auto* fileRow = new QHBoxLayout();
    auto* newGraphButton = new QPushButton(tr("New"), this);
    newGraphButton->setObjectName(QStringLiteral("visualNewGraphButton"));
    auto* openGraphButton = new QPushButton(tr("Open"), this);
    openGraphButton->setObjectName(QStringLiteral("visualOpenGraphButton"));
    auto* saveGraphButton = new QPushButton(tr("Save"), this);
    saveGraphButton->setObjectName(QStringLiteral("visualSaveGraphButton"));
    auto* saveGraphAsButton = new QPushButton(tr("Save As"), this);
    saveGraphAsButton->setObjectName(QStringLiteral("visualSaveGraphAsButton"));
    m_graphFileStatus = new QLabel(this);
    m_graphFileStatus->setObjectName(QStringLiteral("visualGraphFileStatus"));
    fileRow->addWidget(newGraphButton);
    fileRow->addWidget(openGraphButton);
    fileRow->addWidget(saveGraphButton);
    fileRow->addWidget(saveGraphAsButton);
    fileRow->addWidget(m_graphFileStatus, 1);
    rootLayout->addLayout(fileRow);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* nodesPanel = new QWidget(splitter);
    auto* nodesLayout = new QVBoxLayout(nodesPanel);
    nodesLayout->setContentsMargins(0, 0, 0, 0);
    nodesLayout->addWidget(new QLabel(tr("Nodes"), nodesPanel));
    m_nodeSearch = new QLineEdit(nodesPanel);
    m_nodeSearch->setObjectName(QStringLiteral("visualNodeSearchEdit"));
    m_nodeSearch->setPlaceholderText(tr("Search nodes or node types..."));
    m_nodeSearch->setClearButtonEnabled(true);
    nodesLayout->addWidget(m_nodeSearch);
    m_nodeTree = new QTreeWidget(nodesPanel);
    m_nodeTree->setObjectName(QStringLiteral("visualNodeTree"));
    m_nodeTree->setHeaderLabels({tr("ID"), tr("Node"), tr("Outputs")});
    m_nodeTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nodeTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    nodesLayout->addWidget(m_nodeTree, 1);

    auto* addRow = new QHBoxLayout();
    m_kindCombo = new QComboBox(nodesPanel);
    m_kindCombo->setObjectName(QStringLiteral("visualNodeKindCombo"));
    for (const auto& definition : fabgl::VisualNodeRegistry::builtins().definitions()) {
        m_kindCombo->addItem(QStringLiteral("%1  [%2]")
                                 .arg(QString::fromStdString(definition.displayName),
                                      QString::fromStdString(definition.stableName)),
                             static_cast<int>(definition.type));
    }
    auto* addNodeButton = new QPushButton(tr("Add"), nodesPanel);
    addNodeButton->setObjectName(QStringLiteral("visualAddNodeButton"));
    auto* removeNodeButton = new QPushButton(tr("Remove"), nodesPanel);
    removeNodeButton->setObjectName(QStringLiteral("visualRemoveNodeButton"));
    addRow->addWidget(m_kindCombo, 1);
    addRow->addWidget(addNodeButton);
    addRow->addWidget(removeNodeButton);
    nodesLayout->addLayout(addRow);

    auto* canvasPanel = new QWidget(splitter);
    auto* canvasLayout = new QVBoxLayout(canvasPanel);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    auto* canvasTools = new QHBoxLayout();
    m_undoButton = new QPushButton(tr("Undo"), canvasPanel);
    m_undoButton->setObjectName(QStringLiteral("visualUndoButton"));
    m_redoButton = new QPushButton(tr("Redo"), canvasPanel);
    m_redoButton->setObjectName(QStringLiteral("visualRedoButton"));
    m_debugRunButton = new QPushButton(tr("Debug Run"), canvasPanel);
    m_debugRunButton->setObjectName(QStringLiteral("visualDebugRunButton"));
    auto* addCommentButton = new QPushButton(tr("Comment"), canvasPanel);
    addCommentButton->setObjectName(QStringLiteral("visualAddCommentButton"));
    auto* copyButton = new QPushButton(tr("Copy"), canvasPanel);
    copyButton->setObjectName(QStringLiteral("visualCanvasCopyButton"));
    auto* pasteButton = new QPushButton(tr("Paste"), canvasPanel);
    pasteButton->setObjectName(QStringLiteral("visualCanvasPasteButton"));
    auto* deleteCanvasButton = new QPushButton(tr("Delete"), canvasPanel);
    deleteCanvasButton->setObjectName(QStringLiteral("visualCanvasDeleteButton"));
    canvasTools->addWidget(m_undoButton);
    canvasTools->addWidget(m_redoButton);
    canvasTools->addWidget(m_debugRunButton);
    canvasTools->addWidget(addCommentButton);
    canvasTools->addWidget(copyButton);
    canvasTools->addWidget(pasteButton);
    canvasTools->addWidget(deleteCanvasButton);
    canvasTools->addStretch();
    canvasLayout->addLayout(canvasTools);
    m_graphScene = new QGraphicsScene(this);
    m_graphScene->setSceneRect(-2000.0, -2000.0, 4000.0, 4000.0);
    auto* graphView = new VisualGraphView(canvasPanel);
    graphView->setObjectName(QStringLiteral("visualGraphCanvas"));
    graphView->setScene(m_graphScene);
    m_graphView = graphView;
    canvasLayout->addWidget(graphView, 1);

    auto* editorPanel = new QWidget(splitter);
    auto* editorLayout = new QVBoxLayout(editorPanel);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    auto* properties = new QGroupBox(tr("Selected Node"), editorPanel);
    auto* propertyLayout = new QFormLayout(properties);
    m_nameEdit = new QLineEdit(properties);
    m_nameEdit->setObjectName(QStringLiteral("visualNodeNameEdit"));
    m_numberEdit = new QDoubleSpinBox(properties);
    m_numberEdit->setObjectName(QStringLiteral("visualNodeNumberEdit"));
    m_numberEdit->setRange(-1.0e9, 1.0e9);
    m_numberEdit->setDecimals(4);
    m_booleanEdit = new QCheckBox(tr("True"), properties);
    m_booleanEdit->setObjectName(QStringLiteral("visualNodeBooleanEdit"));
    m_variableEdit = new QLineEdit(properties);
    m_variableEdit->setObjectName(QStringLiteral("visualNodeVariableEdit"));
    m_callbackEdit = new QComboBox(properties);
    m_callbackEdit->setObjectName(QStringLiteral("visualNodeCallbackEdit"));
    m_callbackEdit->setEditable(true);
    for (const auto& callback : fabgl::project::ProjectVisualHost::safeFunctionCallbacks())
        m_callbackEdit->addItem(QString::fromStdString(callback));
    m_payloadEdit = new QLineEdit(properties);
    m_payloadEdit->setObjectName(QStringLiteral("visualNodePayloadEdit"));
    m_payloadEdit->setMaxLength(256);
    m_payloadEdit->setPlaceholderText(tr("Action/state/input payload"));
    m_assetReferenceEdit = new QLineEdit(properties);
    m_assetReferenceEdit->setObjectName(QStringLiteral("visualNodeAssetReferenceEdit"));
    m_assetReferenceEdit->setPlaceholderText(tr("Asset GUID"));
    m_entityReferenceEdit = new QLineEdit(properties);
    m_entityReferenceEdit->setObjectName(QStringLiteral("visualNodeEntityReferenceEdit"));
    m_entityReferenceEdit->setPlaceholderText(tr("Entity GUID"));
    m_componentReferenceEdit = new QLineEdit(properties);
    m_componentReferenceEdit->setObjectName(QStringLiteral("visualNodeComponentReferenceEdit"));
    m_componentReferenceEdit->setPlaceholderText(tr("Component type GUID"));
    propertyLayout->addRow(tr("Name"), m_nameEdit);
    propertyLayout->addRow(tr("Number"), m_numberEdit);
    propertyLayout->addRow(tr("Boolean"), m_booleanEdit);
    propertyLayout->addRow(tr("Variable"), m_variableEdit);
    propertyLayout->addRow(tr("Callback"), m_callbackEdit);
    propertyLayout->addRow(tr("Payload"), m_payloadEdit);
    propertyLayout->addRow(tr("Asset reference"), m_assetReferenceEdit);
    propertyLayout->addRow(tr("Entity reference"), m_entityReferenceEdit);
    propertyLayout->addRow(tr("Component reference"), m_componentReferenceEdit);
    editorLayout->addWidget(properties);

    auto* connections = new QGroupBox(tr("Connections"), editorPanel);
    auto* connectionLayout = new QVBoxLayout(connections);
    auto* connectionForm = new QFormLayout();
    m_sourceNodeCombo = new QComboBox(connections);
    m_sourceNodeCombo->setObjectName(QStringLiteral("visualSourceNodeCombo"));
    m_sourcePinCombo = new QComboBox(connections);
    m_sourcePinCombo->setObjectName(QStringLiteral("visualSourcePinCombo"));
    m_targetNodeCombo = new QComboBox(connections);
    m_targetNodeCombo->setObjectName(QStringLiteral("visualTargetNodeCombo"));
    m_targetPinCombo = new QComboBox(connections);
    m_targetPinCombo->setObjectName(QStringLiteral("visualTargetPinCombo"));
    connectionForm->addRow(tr("Source node"), m_sourceNodeCombo);
    connectionForm->addRow(tr("Source pin"), m_sourcePinCombo);
    connectionForm->addRow(tr("Target node"), m_targetNodeCombo);
    connectionForm->addRow(tr("Target pin"), m_targetPinCombo);
    connectionLayout->addLayout(connectionForm);
    auto* connectionButtons = new QHBoxLayout();
    auto* addConnectionButton = new QPushButton(tr("Add Connection"), connections);
    addConnectionButton->setObjectName(QStringLiteral("visualAddConnectionButton"));
    auto* removeConnectionButton = new QPushButton(tr("Remove Connection"), connections);
    removeConnectionButton->setObjectName(QStringLiteral("visualRemoveConnectionButton"));
    connectionButtons->addWidget(addConnectionButton);
    connectionButtons->addWidget(removeConnectionButton);
    connectionLayout->addLayout(connectionButtons);
    m_connectionTable = new QTableWidget(0, 4, connections);
    m_connectionTable->setObjectName(QStringLiteral("visualConnectionTable"));
    m_connectionTable->setHorizontalHeaderLabels(
        {tr("Source"), tr("Pin"), tr("Target"), tr("Pin")});
    m_connectionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_connectionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_connectionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_connectionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connectionLayout->addWidget(m_connectionTable, 1);
    editorLayout->addWidget(connections, 1);

    splitter->addWidget(nodesPanel);
    splitter->addWidget(canvasPanel);
    splitter->addWidget(editorPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 2);
    rootLayout->addWidget(splitter, 2);

    auto* validationHeader = new QHBoxLayout();
    validationHeader->addWidget(new QLabel(tr("Validation / Compilation"), this));
    validationHeader->addStretch();
    auto* validateButton = new QPushButton(tr("Validate & Compile"), this);
    validateButton->setObjectName(QStringLiteral("visualValidateButton"));
    validationHeader->addWidget(validateButton);
    rootLayout->addLayout(validationHeader);
    m_validationTable = new QTableWidget(0, 3, this);
    m_validationTable->setObjectName(QStringLiteral("visualValidationTable"));
    m_validationTable->setHorizontalHeaderLabels({tr("Severity"), tr("Node"), tr("Message")});
    m_validationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_validationTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    rootLayout->addWidget(m_validationTable, 1);
    m_compileStatus = new QLabel(this);
    m_compileStatus->setObjectName(QStringLiteral("visualCompileStatus"));
    rootLayout->addWidget(m_compileStatus);
    m_debugStatus = new QLabel(tr("Debug trace idle."), this);
    m_debugStatus->setObjectName(QStringLiteral("visualDebugStatus"));
    rootLayout->addWidget(m_debugStatus);
    m_debugTimer = new QTimer(this);
    m_debugTimer->setInterval(220);

    connect(newGraphButton, &QPushButton::clicked, this, [this]() {
        if (m_graphModified &&
            QMessageBox::question(this, tr("Discard Visual Script Changes"),
                                  tr("Create a new graph and discard unsaved graph changes?"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        newGraph();
    });
    connect(openGraphButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::openGraphDialog);
    connect(saveGraphButton, &QPushButton::clicked, this, [this]() { saveGraphDialog(false); });
    connect(saveGraphAsButton, &QPushButton::clicked, this, [this]() { saveGraphDialog(true); });
    connect(addNodeButton, &QPushButton::clicked, this, &VisualScriptEditorWidget::addNode);
    connect(removeNodeButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::removeSelectedNode);
    connect(addCommentButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::addCommentBox);
    connect(copyButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::copySelectedCanvasItems);
    connect(pasteButton, &QPushButton::clicked, this, &VisualScriptEditorWidget::pasteCanvasItems);
    connect(deleteCanvasButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::removeSelectedCanvasItems);
    graphView->copyRequested = [this]() { copySelectedCanvasItems(); };
    graphView->pasteRequested = [this]() { pasteCanvasItems(); };
    graphView->deleteRequested = [this]() { removeSelectedCanvasItems(); };
    graphView->undoRequested = [this]() { undoGraphEdit(); };
    graphView->redoRequested = [this]() { redoGraphEdit(); };
    graphView->connectionRequested =
        [this](const fabgl::VisualNodeId sourceNode, const fabgl::VisualPinId sourcePin,
               const fabgl::VisualNodeId targetNode, const fabgl::VisualPinId targetPin) {
            handleCanvasConnection(sourceNode, sourcePin, targetNode, targetPin);
        };
    connect(addConnectionButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::addConnection);
    connect(removeConnectionButton, &QPushButton::clicked, this,
            &VisualScriptEditorWidget::removeSelectedConnection);
    connect(validateButton, &QPushButton::clicked, this, &VisualScriptEditorWidget::validateGraph);
    connect(m_undoButton, &QPushButton::clicked, this, &VisualScriptEditorWidget::undoGraphEdit);
    connect(m_redoButton, &QPushButton::clicked, this, &VisualScriptEditorWidget::redoGraphEdit);
    connect(m_debugRunButton, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        if (!executeDebugPreview(errorMessage))
            emit statusMessage(tr("Visual debug run failed: %1").arg(errorMessage));
    });
    connect(m_debugTimer, &QTimer::timeout, this, &VisualScriptEditorWidget::advanceDebugTrace);
    connect(m_nodeSearch, &QLineEdit::textChanged, this, [this](const QString& query) {
        refreshNodeList();
        if (query.trimmed().isEmpty())
            return;
        for (int index = 0; index < m_kindCombo->count(); ++index) {
            if (m_kindCombo->itemText(index).contains(query, Qt::CaseInsensitive)) {
                m_kindCombo->setCurrentIndex(index);
                break;
            }
        }
    });
    connect(m_nodeSearch, &QLineEdit::returnPressed, this, [this]() {
        if (!m_nodeSearch->text().trimmed().isEmpty())
            addNode();
    });
    connect(m_nodeTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { refreshNodeEditors(); });
    connect(m_nodeTree, &QTreeWidget::itemSelectionChanged, m_graphScene, [this]() {
        if (m_updating || m_graphScene == nullptr)
            return;
        std::set<fabgl::VisualNodeId> selectedIds;
        for (const auto* item : m_nodeTree->selectedItems())
            selectedIds.insert(
                static_cast<fabgl::VisualNodeId>(item->data(0, Qt::UserRole).toUInt()));
        const QSignalBlocker blocker(m_graphScene);
        for (auto* item : m_graphScene->items()) {
            if (item->data(CanvasKindRole).toInt() == CanvasNodeKind) {
                const auto id = static_cast<fabgl::VisualNodeId>(item->data(CanvasIdRole).toUInt());
                item->setSelected(selectedIds.contains(id));
            }
        }
    });
    connect(m_graphScene, &QGraphicsScene::selectionChanged, m_nodeTree, [this]() {
        if (m_updating)
            return;
        std::set<fabgl::VisualNodeId> selectedIds;
        for (const auto* selected : m_graphScene->selectedItems()) {
            if (selected->data(CanvasKindRole).toInt() == CanvasNodeKind)
                selectedIds.insert(
                    static_cast<fabgl::VisualNodeId>(selected->data(CanvasIdRole).toUInt()));
        }
        const QSignalBlocker blocker(m_nodeTree);
        QModelIndex currentIndex;
        for (int index = 0; index < m_nodeTree->topLevelItemCount(); ++index) {
            auto* item = m_nodeTree->topLevelItem(index);
            const bool selected = selectedIds.contains(
                static_cast<fabgl::VisualNodeId>(item->data(0, Qt::UserRole).toUInt()));
            item->setSelected(selected);
            if (selected && !currentIndex.isValid())
                currentIndex = m_nodeTree->indexFromItem(item);
        }
        if (currentIndex.isValid())
            m_nodeTree->selectionModel()->setCurrentIndex(currentIndex,
                                                          QItemSelectionModel::NoUpdate);
        refreshNodeEditors();
    });
    connect(m_sourceNodeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshPinCombos(); });
    connect(m_targetNodeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshPinCombos(); });
    connect(m_nameEdit, &QLineEdit::editingFinished, this,
            &VisualScriptEditorWidget::applySelectedNodeEdits);
    connect(m_numberEdit, &QDoubleSpinBox::editingFinished, this,
            &VisualScriptEditorWidget::applySelectedNodeEdits);
    connect(m_booleanEdit, &QCheckBox::toggled, this, [this](bool) { applySelectedNodeEdits(); });
    connect(m_variableEdit, &QLineEdit::editingFinished, this,
            &VisualScriptEditorWidget::applySelectedNodeEdits);
    connect(m_callbackEdit->lineEdit(), &QLineEdit::editingFinished, this,
            &VisualScriptEditorWidget::applySelectedNodeEdits);
    for (auto* edit : {m_payloadEdit, m_assetReferenceEdit, m_entityReferenceEdit,
                       m_componentReferenceEdit}) {
        connect(edit, &QLineEdit::editingFinished, this,
                &VisualScriptEditorWidget::applySelectedNodeEdits);
    }

    newGraph();
}

qsizetype VisualScriptEditorWidget::nodeCount() const noexcept {
    return static_cast<qsizetype>(m_nodes.size());
}

qsizetype VisualScriptEditorWidget::edgeCount() const noexcept {
    return static_cast<qsizetype>(m_edges.size());
}

qsizetype VisualScriptEditorWidget::validationIssueCount() const noexcept {
    return m_validationIssueCount;
}

bool VisualScriptEditorWidget::hasValidationErrors() const noexcept {
    return m_hasErrors;
}

QString VisualScriptEditorWidget::graphFilePath() const {
    return m_graphFilePath;
}

bool VisualScriptEditorWidget::graphModified() const noexcept {
    return m_graphModified;
}

qsizetype VisualScriptEditorWidget::commentCount() const noexcept {
    return static_cast<qsizetype>(m_comments.size());
}

qsizetype VisualScriptEditorWidget::selectedCanvasNodeCount() const noexcept {
    if (m_graphScene == nullptr)
        return 0;
    const auto selected = m_graphScene->selectedItems();
    return static_cast<qsizetype>(
        std::count_if(selected.cbegin(), selected.cend(), [](const QGraphicsItem* item) {
            return item != nullptr && item->data(CanvasKindRole).toInt() == CanvasNodeKind;
        }));
}

double VisualScriptEditorWidget::canvasScale() const noexcept {
    const auto* view = dynamic_cast<const VisualGraphView*>(m_graphView);
    return view != nullptr ? view->scaleFactor() : 1.0;
}

bool VisualScriptEditorWidget::canUndoGraphEdit() const noexcept {
    return !m_undoHistory.empty();
}

bool VisualScriptEditorWidget::canRedoGraphEdit() const noexcept {
    return !m_redoHistory.empty();
}

qsizetype VisualScriptEditorWidget::debugTraceNodeCount() const noexcept {
    return static_cast<qsizetype>(m_debugTrace.size());
}

fabgl::VisualNodeId VisualScriptEditorWidget::activeDebugNodeId() const noexcept {
    return m_debugTrace.empty() || m_debugTraceIndex >= m_debugTrace.size()
               ? fabgl::VisualNodeId{0}
               : m_debugTrace[m_debugTraceIndex];
}

bool VisualScriptEditorWidget::executeDebugPreview(QString& errorMessage) {
    fabgl::VisualGraph graph;
    if (!buildGraph(graph, errorMessage))
        return false;
    auto report = fabgl::VisualGraphValidator::validate(
        graph, {}, fabgl::VisualNodeRegistry::builtins(),
        &fabgl::project::ProjectVisualHost::validationCallbacks());
    for (const auto& [id, node] : graph.nodes()) {
        const auto authoringError = visualNodeAuthoringError(node);
        if (!authoringError.isEmpty()) {
            report.issues.push_back({fabgl::VisualIssueSeverity::Error,
                                     fabgl::VisualIssueCode::InvalidNode, id,
                                     authoringError.toStdString()});
        }
    }
    if (!m_nodeEditorError.isEmpty()) {
        const auto* selected = selectedNode();
        report.issues.push_back({fabgl::VisualIssueSeverity::Error,
                                 fabgl::VisualIssueCode::InvalidNode,
                                 selected != nullptr ? selected->id : fabgl::VisualNodeId{0},
                                 m_nodeEditorError.toStdString()});
    }
    if (report.hasErrors()) {
        errorMessage = tr("Graph validation has errors.");
        return false;
    }
    auto bytecode = fabgl::VisualGraphCompiler::compile(
        graph, {}, {}, fabgl::VisualNodeRegistry::builtins(),
        &fabgl::project::ProjectVisualHost::validationCallbacks());
    if (!bytecode) {
        errorMessage = animationError(bytecode.error());
        return false;
    }
    fabgl::VisualBytecodeVm vm;
    auto execution = vm.execute(
        bytecode.value(), {}, fabgl::VisualMaximumCompiledInstructions,
        &fabgl::project::ProjectVisualHost::validationCallbacks());
    if (!execution) {
        errorMessage = animationError(execution.error());
        return false;
    }
    clearDebugTrace();
    buildLocalDebugTrace(graph);
    if (m_debugTrace.empty()) {
        errorMessage = tr("The local VM completed without an executable node trace.");
        return false;
    }
    m_debugTraceIndex = 0;
    refreshCanvas();
    m_debugStatus->setText(tr("Local VM: node %1 (%2/%3), %4 instruction(s), result %5")
                               .arg(activeDebugNodeId())
                               .arg(m_debugTraceIndex + 1U)
                               .arg(m_debugTrace.size())
                               .arg(execution.value().executedInstructions)
                               .arg(execution.value().returnValue, 0, 'g', 8));
    m_debugTimer->start();
    errorMessage.clear();
    emit statusMessage(m_debugStatus->text());
    return true;
}

void VisualScriptEditorWidget::buildLocalDebugTrace(const fabgl::VisualGraph& graph) {
    m_debugTrace.clear();
    std::map<fabgl::VisualNodeId, std::vector<fabgl::VisualNodeId>> adjacency;
    for (const auto& edge : graph.edges()) {
        adjacency[edge.sourceNode].push_back(edge.targetNode);
        adjacency[edge.targetNode].push_back(edge.sourceNode);
    }
    for (auto& [node, neighbours] : adjacency) {
        Q_UNUSED(node);
        std::sort(neighbours.begin(), neighbours.end());
        neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
    }
    std::set<fabgl::VisualNodeId> visited;
    std::vector<fabgl::VisualNodeId> pending{graph.entryNode()};
    while (!pending.empty()) {
        const auto node = pending.front();
        pending.erase(pending.begin());
        if (node == 0 || !visited.insert(node).second || graph.findNode(node) == nullptr)
            continue;
        m_debugTrace.push_back(node);
        const auto neighbours = adjacency.find(node);
        if (neighbours != adjacency.end())
            pending.insert(pending.end(), neighbours->second.begin(), neighbours->second.end());
    }
}

void VisualScriptEditorWidget::advanceDebugTrace() {
    if (m_debugTrace.empty()) {
        m_debugTimer->stop();
        return;
    }
    if (m_debugTraceIndex + 1U >= m_debugTrace.size()) {
        m_debugTimer->stop();
        m_debugStatus->setText(tr("Local VM trace complete: %1 node(s).").arg(m_debugTrace.size()));
        emit statusMessage(m_debugStatus->text());
        updateDebugHighlight();
        return;
    }
    ++m_debugTraceIndex;
    m_debugStatus->setText(tr("Local VM: active node %1 (%2/%3)")
                               .arg(activeDebugNodeId())
                               .arg(m_debugTraceIndex + 1U)
                               .arg(m_debugTrace.size()));
    updateDebugHighlight();
}

void VisualScriptEditorWidget::clearDebugTrace() {
    if (m_debugTimer != nullptr)
        m_debugTimer->stop();
    m_debugTrace.clear();
    m_debugTraceIndex = 0;
    if (m_debugStatus != nullptr)
        m_debugStatus->setText(tr("Debug trace idle."));
}

void VisualScriptEditorWidget::updateDebugHighlight() {
    if (m_graphScene == nullptr)
        return;
    for (auto* item : m_graphScene->items()) {
        if (item->data(CanvasKindRole).toInt() != CanvasNodeKind)
            continue;
        const auto nodeId = static_cast<fabgl::VisualNodeId>(item->data(CanvasIdRole).toUInt());
        const bool active = nodeId == activeDebugNodeId();
        item->setData(CanvasDebugActiveRole, active);
        auto* rectangle = static_cast<QGraphicsRectItem*>(item);
        rectangle->setPen(
            QPen(active ? QColor("#ffb347")
                        : (nodeId == m_entryNodeId ? QColor("#65d46e") : QColor("#637086")),
                 active ? 4.0 : (nodeId == m_entryNodeId ? 2.0 : 1.25)));
    }
}

void VisualScriptEditorWidget::newGraph() {
    clearDebugTrace();
    m_nodes.clear();
    m_edges.clear();
    m_comments.clear();
    m_nextNodeId = 1;
    m_nextCommentId = 1;
    m_graphGuid = fabgl::AssetGuid::fromStableName("fabgl.studio.visual.untitled");
    m_graphName = "Untitled Visual Script";
    m_graphFilePath.clear();
    m_nodes.push_back(makeNode(fabgl::VisualNodeKind::Entry));
    m_nodes.back().layout.x = -260.0F;
    m_nodes.back().layout.y = 0.0F;
    m_nodes.push_back(makeNode(fabgl::VisualNodeKind::ConstantNumber));
    m_nodes.back().numberValue = 1.0;
    m_nodes.back().layout.x = -260.0F;
    m_nodes.back().layout.y = 150.0F;
    m_nodes.push_back(makeNode(fabgl::VisualNodeKind::Return));
    m_nodes.back().layout.x = 80.0F;
    m_nodes.back().layout.y = 60.0F;
    m_entryNodeId = m_nodes.front().id;
    m_edges.push_back({m_nodes[0].id, 1, m_nodes[2].id, 1});
    m_edges.push_back({m_nodes[1].id, 1, m_nodes[2].id, 2});
    refreshNodeList();
    refreshCanvas();
    refreshConnectionTable();
    validateGraph();
    setGraphModified(false);
    clearGraphHistory();
    emit statusMessage(tr("Created a new visual script graph."));
}

bool VisualScriptEditorWidget::openGraphFile(const QString& filePath, QString& errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = tr("Cannot open visual graph %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    auto decoded = fabgl::deserializeVisualGraph(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!decoded) {
        errorMessage = tr("Cannot deserialize visual graph: %1")
                           .arg(QString::fromStdString(decoded.error().message()));
        return false;
    }
    const auto& graph = decoded.value();
    clearDebugTrace();
    std::vector<fabgl::VisualNode> nodes;
    nodes.reserve(graph.nodes().size());
    fabgl::VisualNodeId nextNodeId = 1;
    for (const auto& [id, node] : graph.nodes()) {
        nodes.push_back(node);
        if (id >= nextNodeId && id < std::numeric_limits<fabgl::VisualNodeId>::max()) {
            nextNodeId = id + 1;
        }
    }
    m_nodes = std::move(nodes);
    m_edges = graph.edges();
    m_comments = graph.comments();
    m_entryNodeId = graph.entryNode();
    m_graphGuid = graph.guid();
    m_graphName = graph.name();
    m_nextNodeId = nextNodeId;
    m_nextCommentId = 1;
    for (const auto& comment : m_comments) {
        if (comment.id >= m_nextCommentId && comment.id < std::numeric_limits<std::uint16_t>::max())
            m_nextCommentId = static_cast<std::uint16_t>(comment.id + 1U);
    }
    m_graphFilePath = QFileInfo(filePath).absoluteFilePath();
    refreshNodeList();
    refreshCanvas();
    refreshConnectionTable();
    validateGraph();
    setGraphModified(false);
    clearGraphHistory();
    errorMessage.clear();
    emit statusMessage(tr("Opened visual graph %1").arg(QDir::toNativeSeparators(m_graphFilePath)));
    return true;
}

bool VisualScriptEditorWidget::saveGraphFile(const QString& filePath, QString& errorMessage) {
    const QString destination = filePath.trimmed().isEmpty() ? m_graphFilePath : filePath;
    if (destination.isEmpty()) {
        errorMessage = tr("Choose a .fglvisual destination before saving.");
        return false;
    }
    fabgl::VisualGraph graph;
    if (!buildGraph(graph, errorMessage)) {
        return false;
    }
    auto serialized = fabgl::serializeVisualGraph(graph);
    if (!serialized) {
        errorMessage = tr("Cannot serialize visual graph: %1")
                           .arg(QString::fromStdString(serialized.error().message()));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        errorMessage = tr("Cannot create the visual graph destination directory.");
        return false;
    }
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = tr("Cannot write visual graph %1: %2")
                           .arg(QDir::toNativeSeparators(destination), file.errorString());
        return false;
    }
    const auto& source = serialized.value();
    const QByteArray bytes(source.data(), static_cast<qsizetype>(source.size()));
    if (file.write(bytes) != bytes.size()) {
        errorMessage =
            tr("Could not completely write the visual graph: %1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        errorMessage =
            tr("Could not atomically replace the visual graph: %1").arg(file.errorString());
        return false;
    }
    m_graphFilePath = QFileInfo(destination).absoluteFilePath();
    setGraphModified(false);
    errorMessage.clear();
    emit statusMessage(tr("Saved visual graph %1").arg(QDir::toNativeSeparators(m_graphFilePath)));
    return true;
}

void VisualScriptEditorWidget::openGraphDialog() {
    if (m_graphModified &&
        QMessageBox::question(this, tr("Discard Visual Script Changes"),
                              tr("Open another graph and discard unsaved graph changes?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Visual Script"), QFileInfo(m_graphFilePath).absolutePath(),
        tr("FabGL Visual Scripts (*.fglvisual);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString errorMessage;
    if (!openGraphFile(path, errorMessage)) {
        QMessageBox::critical(this, tr("Visual Script Open Failed"), errorMessage);
    }
}

void VisualScriptEditorWidget::saveGraphDialog(const bool saveAs) {
    QString path = saveAs ? QString{} : m_graphFilePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Visual Script"),
            m_graphFilePath.isEmpty() ? QStringLiteral("Untitled.fglvisual") : m_graphFilePath,
            tr("FabGL Visual Scripts (*.fglvisual);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".fglvisual"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".fglvisual");
        }
    }
    QString errorMessage;
    if (!saveGraphFile(path, errorMessage)) {
        QMessageBox::critical(this, tr("Visual Script Save Failed"), errorMessage);
    }
}

void VisualScriptEditorWidget::setGraphModified(const bool modified) {
    m_graphModified = modified;
    updateGraphFileStatus();
}

void VisualScriptEditorWidget::updateGraphFileStatus() {
    if (m_graphFileStatus == nullptr) {
        return;
    }
    const QString display = m_graphFilePath.isEmpty() ? tr("Untitled.fglvisual")
                                                      : QDir::toNativeSeparators(m_graphFilePath);
    m_graphFileStatus->setText(display + (m_graphModified ? QStringLiteral(" *") : QString{}));
    m_graphFileStatus->setToolTip(display);
}

VisualScriptEditorWidget::GraphSnapshot VisualScriptEditorWidget::graphSnapshot() const {
    return {m_nodes,       m_edges,     m_comments,  m_nextNodeId,   m_nextCommentId,
            m_entryNodeId, m_graphGuid, m_graphName, m_graphModified};
}

void VisualScriptEditorWidget::restoreGraphSnapshot(GraphSnapshot snapshot) {
    m_nodes = std::move(snapshot.nodes);
    m_edges = std::move(snapshot.edges);
    m_comments = std::move(snapshot.comments);
    m_nextNodeId = snapshot.nextNodeId;
    m_nextCommentId = snapshot.nextCommentId;
    m_entryNodeId = snapshot.entryNodeId;
    m_graphGuid = snapshot.graphGuid;
    m_graphName = std::move(snapshot.graphName);
    refreshNodeList();
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(snapshot.modified);
}

void VisualScriptEditorWidget::updateGraphHistoryActions() {
    if (m_undoButton != nullptr)
        m_undoButton->setEnabled(canUndoGraphEdit());
    if (m_redoButton != nullptr)
        m_redoButton->setEnabled(canRedoGraphEdit());
}

void VisualScriptEditorWidget::recordUndoPoint() {
    if (m_layoutTransactionStart.has_value())
        return;
    clearDebugTrace();
    constexpr std::size_t MaximumHistory = 100U;
    if (m_undoHistory.size() >= MaximumHistory)
        m_undoHistory.erase(m_undoHistory.begin());
    m_undoHistory.push_back(graphSnapshot());
    m_redoHistory.clear();
    updateGraphHistoryActions();
}

void VisualScriptEditorWidget::beginLayoutTransaction() {
    if (!m_layoutTransactionStart.has_value()) {
        clearDebugTrace();
        m_layoutTransactionStart = graphSnapshot();
        m_layoutTransactionChanged = false;
    }
}

void VisualScriptEditorWidget::endLayoutTransaction() {
    if (!m_layoutTransactionStart.has_value())
        return;
    if (m_layoutTransactionChanged) {
        constexpr std::size_t MaximumHistory = 100U;
        if (m_undoHistory.size() >= MaximumHistory)
            m_undoHistory.erase(m_undoHistory.begin());
        m_undoHistory.push_back(std::move(*m_layoutTransactionStart));
        m_redoHistory.clear();
        QTimer::singleShot(0, this, [this]() { refreshCanvas(); });
    }
    m_layoutTransactionStart.reset();
    m_layoutTransactionChanged = false;
    updateGraphHistoryActions();
}

void VisualScriptEditorWidget::undoGraphEdit() {
    endLayoutTransaction();
    if (m_undoHistory.empty())
        return;
    m_redoHistory.push_back(graphSnapshot());
    auto snapshot = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();
    restoreGraphSnapshot(std::move(snapshot));
    updateGraphHistoryActions();
    emit statusMessage(tr("Visual graph edit undone."));
}

void VisualScriptEditorWidget::redoGraphEdit() {
    endLayoutTransaction();
    if (m_redoHistory.empty())
        return;
    m_undoHistory.push_back(graphSnapshot());
    auto snapshot = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();
    restoreGraphSnapshot(std::move(snapshot));
    updateGraphHistoryActions();
    emit statusMessage(tr("Visual graph edit redone."));
}

void VisualScriptEditorWidget::clearGraphHistory() {
    m_undoHistory.clear();
    m_redoHistory.clear();
    m_layoutTransactionStart.reset();
    m_layoutTransactionChanged = false;
    updateGraphHistoryActions();
}

fabgl::VisualNode VisualScriptEditorWidget::makeNode(const fabgl::VisualNodeKind kind) {
    fabgl::VisualNode node;
    node.id = m_nextNodeId++;
    node.kind = kind;
    node.name = QStringLiteral("%1 %2").arg(nodeKindName(kind)).arg(node.id).toStdString();
    using Direction = fabgl::VisualPinDirection;
    using Type = fabgl::VisualValueType;
    const auto pin = [](const fabgl::VisualPinId id, const char* name, const Type type,
                        const Direction direction) {
        return fabgl::VisualPin{id, name, type, direction};
    };
    using enum fabgl::VisualNodeKind;
    switch (kind) {
    case Entry:
        node.pins = {pin(1, "flow", Type::Flow, Direction::Output)};
        break;
    case ConstantNumber:
    case GetVariable:
    case AssetReference:
    case EntityReference:
        node.pins = {pin(1, "value", Type::Number, Direction::Output)};
        break;
    case ConstantBoolean:
        node.pins = {pin(1, "value", Type::Boolean, Direction::Output)};
        break;
    case Add:
    case Multiply:
        node.pins = {pin(1, "a", Type::Number, Direction::Input),
                     pin(2, "b", Type::Number, Direction::Input),
                     pin(3, "value", Type::Number, Direction::Output)};
        break;
    case Less:
        node.pins = {pin(1, "a", Type::Number, Direction::Input),
                     pin(2, "b", Type::Number, Direction::Input),
                     pin(3, "value", Type::Boolean, Direction::Output)};
        break;
    case Return:
        node.pins = {pin(1, "flow", Type::Flow, Direction::Input),
                     pin(2, "value", Type::Number, Direction::Input)};
        break;
    }
    if (kind == GetVariable) {
        node.variableName = "value";
    }
    return node;
}

bool VisualScriptEditorWidget::buildGraph(fabgl::VisualGraph& graph, QString& errorMessage) const {
    graph.setGuid(m_graphGuid);
    graph.setName(m_graphName);
    for (const auto& node : m_nodes) {
        auto result = graph.addNode(node);
        if (!result) {
            errorMessage = QString::fromStdString(result.error().message());
            return false;
        }
    }
    for (const auto& edge : m_edges) {
        auto result = graph.addEdge(edge);
        if (!result) {
            errorMessage = QString::fromStdString(result.error().message());
            return false;
        }
    }
    for (const auto& comment : m_comments) {
        auto result = graph.addCommentBox(comment);
        if (!result) {
            errorMessage = QString::fromStdString(result.error().message());
            return false;
        }
    }
    graph.setEntryNode(m_entryNodeId);
    errorMessage.clear();
    return true;
}

void VisualScriptEditorWidget::validateGraph() {
    fabgl::VisualGraph graph;
    QString buildError;
    if (!buildGraph(graph, buildError)) {
        m_hasErrors = true;
        m_validationIssueCount = 1;
        m_validationTable->setRowCount(1);
        m_validationTable->setItem(0, 0, new QTableWidgetItem(tr("Error")));
        m_validationTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("-")));
        m_validationTable->setItem(0, 2, new QTableWidgetItem(buildError));
        m_compileStatus->setText(tr("Build failed: %1").arg(buildError));
        return;
    }

    auto report = fabgl::VisualGraphValidator::validate(
        graph, {}, fabgl::VisualNodeRegistry::builtins(),
        &fabgl::project::ProjectVisualHost::validationCallbacks());
    for (const auto& [id, node] : graph.nodes()) {
        const auto authoringError = visualNodeAuthoringError(node);
        if (!authoringError.isEmpty()) {
            report.issues.push_back({fabgl::VisualIssueSeverity::Error,
                                     fabgl::VisualIssueCode::InvalidNode, id,
                                     authoringError.toStdString()});
        }
    }
    if (!m_nodeEditorError.isEmpty()) {
        const auto* selected = selectedNode();
        report.issues.push_back({fabgl::VisualIssueSeverity::Error,
                                 fabgl::VisualIssueCode::InvalidNode,
                                 selected != nullptr ? selected->id : fabgl::VisualNodeId{0},
                                 m_nodeEditorError.toStdString()});
    }
    m_validationIssueCount = static_cast<qsizetype>(report.issues.size());
    m_hasErrors = report.hasErrors();
    m_validationTable->setRowCount(static_cast<int>(report.issues.size()));
    for (int row = 0; row < static_cast<int>(report.issues.size()); ++row) {
        const auto& issue = report.issues[static_cast<std::size_t>(row)];
        const bool error = issue.severity == fabgl::VisualIssueSeverity::Error;
        auto* severity = new QTableWidgetItem(error ? tr("Error") : tr("Warning"));
        severity->setForeground(error ? QColor(QStringLiteral("#ff6b6b"))
                                      : QColor(QStringLiteral("#f7c948")));
        m_validationTable->setItem(row, 0, severity);
        m_validationTable->setItem(row, 1, new QTableWidgetItem(QString::number(issue.node)));
        m_validationTable->setItem(row, 2,
                                   new QTableWidgetItem(QString::fromStdString(issue.message)));
    }

    if (m_hasErrors) {
        m_compileStatus->setText(tr("Validation failed: %1 issue(s)").arg(m_validationIssueCount));
        emit statusMessage(m_compileStatus->text());
        return;
    }
    auto compiled = fabgl::VisualGraphCompiler::compile(
        graph, {}, {}, fabgl::VisualNodeRegistry::builtins(),
        &fabgl::project::ProjectVisualHost::validationCallbacks());
    if (!compiled) {
        m_hasErrors = true;
        m_compileStatus->setText(
            tr("Compilation failed: %1").arg(QString::fromStdString(compiled.error().message())));
    } else {
        m_compileStatus->setText(tr("Compiled: %1 byte(s), %2 constant(s), %3 warning(s)")
                                     .arg(compiled.value().code.size())
                                     .arg(compiled.value().constants.size())
                                     .arg(m_validationIssueCount));
    }
    emit statusMessage(m_compileStatus->text());
}

void VisualScriptEditorWidget::refreshCanvas() {
    if (m_graphScene == nullptr)
        return;
    std::set<fabgl::VisualNodeId> selectedNodes;
    std::set<std::uint16_t> selectedComments;
    for (const auto* selected : m_graphScene->selectedItems()) {
        if (selected->data(CanvasKindRole).toInt() == CanvasNodeKind)
            selectedNodes.insert(
                static_cast<fabgl::VisualNodeId>(selected->data(CanvasIdRole).toUInt()));
        else if (selected->data(CanvasKindRole).toInt() == CanvasCommentKind)
            selectedComments.insert(
                static_cast<std::uint16_t>(selected->data(CanvasIdRole).toUInt()));
    }
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_graphScene->clear();
    std::unordered_map<std::uint32_t, QGraphicsItem*> ports;
    const auto portKey = [](const fabgl::VisualNodeId node, const fabgl::VisualPinId pin) {
        return (static_cast<std::uint32_t>(node) << 16U) | static_cast<std::uint32_t>(pin);
    };

    for (const auto& comment : m_comments) {
        const QRectF rectangle(0.0, 0.0, std::max(120.0F, comment.layout.width),
                               std::max(60.0F, comment.layout.height));
        auto* item = new MovableCanvasRect(
            rectangle,
            [this, id = comment.id](const QPointF position) {
                updateCommentLayout(id, static_cast<float>(position.x()),
                                    static_cast<float>(position.y()));
            },
            [this]() { beginLayoutTransaction(); }, [this]() { endLayoutTransaction(); });
        item->setData(CanvasKindRole, CanvasCommentKind);
        item->setData(CanvasIdRole, comment.id);
        item->setBrush(QColor(225, 190, 75, 38));
        item->setPen(QPen(QColor("#d8b84e"), 1.5, Qt::DashLine));
        item->setPos(comment.layout.x, comment.layout.y);
        item->setSelected(selectedComments.contains(comment.id));
        item->setZValue(-10.0);
        auto* title = new QGraphicsSimpleTextItem(QString::fromStdString(comment.title), item);
        title->setBrush(QColor("#f4da78"));
        title->setPos(8.0, 5.0);
        m_graphScene->addItem(item);
    }

    for (const auto& node : m_nodes) {
        const qreal width = std::max(150.0F, node.layout.width);
        const qreal height = std::max(70.0F, node.layout.height);
        auto* item = new MovableCanvasRect(
            QRectF(0.0, 0.0, width, height),
            [this, id = node.id](const QPointF position) {
                updateNodeLayout(id, static_cast<float>(position.x()),
                                 static_cast<float>(position.y()));
            },
            [this]() { beginLayoutTransaction(); }, [this]() { endLayoutTransaction(); });
        item->setData(CanvasKindRole, CanvasNodeKind);
        item->setData(CanvasIdRole, node.id);
        const bool debugActive = node.id == activeDebugNodeId();
        item->setData(CanvasDebugActiveRole, debugActive);
        item->setBrush(QColor("#242936"));
        item->setPen(QPen(debugActive
                              ? QColor("#ffb347")
                              : (node.id == m_entryNodeId ? QColor("#65d46e") : QColor("#637086")),
                          debugActive ? 4.0 : (node.id == m_entryNodeId ? 2.0 : 1.25)));
        item->setPos(node.layout.x, node.layout.y);
        item->setSelected(selectedNodes.contains(node.id));
        item->setZValue(5.0);
        auto* title = new QGraphicsSimpleTextItem(QString::fromStdString(node.name), item);
        title->setBrush(QColor("#f0f3f7"));
        title->setPos(10.0, 6.0);
        auto* type = new QGraphicsSimpleTextItem(nodeTypeName(node), item);
        type->setBrush(QColor("#9ba8ba"));
        type->setPos(10.0, 25.0);

        int inputIndex = 0;
        int outputIndex = 0;
        for (const auto& pin : node.pins) {
            const bool output = pin.direction == fabgl::VisualPinDirection::Output;
            const int index = output ? outputIndex++ : inputIndex++;
            const qreal y = 51.0 + static_cast<qreal>(index) * 18.0;
            const qreal x = output ? width - 6.0 : -6.0;
            auto* port = new QGraphicsEllipseItem(-5.0, -5.0, 10.0, 10.0, item);
            port->setData(CanvasKindRole, CanvasPortKind);
            port->setData(CanvasIdRole, node.id);
            port->setData(CanvasPinRole, pin.id);
            port->setData(CanvasDirectionRole, static_cast<int>(pin.direction));
            const QColor pinColor =
                pin.type == fabgl::VisualValueType::Flow
                    ? QColor("#f2f2f2")
                    : (pin.type == fabgl::VisualValueType::Boolean ? QColor("#d96b6b")
                                                                   : QColor("#5bb8ff"));
            port->setBrush(pinColor);
            port->setPen(QPen(pinColor.lighter(130), 1.0));
            port->setPos(x, y);
            port->setZValue(20.0);
            port->setToolTip(QStringLiteral("%1 (%2)").arg(QString::fromStdString(pin.name),
                                                           valueTypeName(pin.type)));
            ports.emplace(portKey(node.id, pin.id), port);
            auto* label = new QGraphicsSimpleTextItem(QString::fromStdString(pin.name), item);
            label->setBrush(QColor("#c8d0dc"));
            const qreal labelWidth = label->boundingRect().width();
            label->setPos(output ? width - labelWidth - 12.0 : 12.0, y - 8.0);
        }
        m_graphScene->addItem(item);
    }

    for (const auto& edge : m_edges) {
        const auto source = ports.find(portKey(edge.sourceNode, edge.sourcePin));
        const auto target = ports.find(portKey(edge.targetNode, edge.targetPin));
        if (source == ports.end() || target == ports.end())
            continue;
        const QPointF start = source->second->sceneBoundingRect().center();
        const QPointF end = target->second->sceneBoundingRect().center();
        QPainterPath path(start);
        const qreal bend = std::max(50.0, std::fabs(end.x() - start.x()) * 0.5);
        path.cubicTo(start + QPointF(bend, 0.0), end - QPointF(bend, 0.0), end);
        auto* connection = m_graphScene->addPath(path, QPen(QColor("#7cc7ff"), 2.0));
        connection->setZValue(0.0);
    }
    m_updating = wasUpdating;
}

void VisualScriptEditorWidget::updateNodeLayout(const fabgl::VisualNodeId nodeId, const float x,
                                                const float y) {
    if (m_updating || !std::isfinite(x) || !std::isfinite(y))
        return;
    const auto node = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const auto& candidate) {
        return candidate.id == nodeId;
    });
    if (node == m_nodes.end())
        return;
    const float clampedX = std::clamp(x, -10000.0F, 10000.0F);
    const float clampedY = std::clamp(y, -10000.0F, 10000.0F);
    if (node->layout.x == clampedX && node->layout.y == clampedY)
        return;
    beginLayoutTransaction();
    node->layout.x = clampedX;
    node->layout.y = clampedY;
    m_layoutTransactionChanged = true;
    setGraphModified(true);
}

void VisualScriptEditorWidget::updateCommentLayout(const std::uint16_t commentId, const float x,
                                                   const float y) {
    if (m_updating || !std::isfinite(x) || !std::isfinite(y))
        return;
    const auto comment =
        std::find_if(m_comments.begin(), m_comments.end(),
                     [commentId](const auto& candidate) { return candidate.id == commentId; });
    if (comment == m_comments.end())
        return;
    const float clampedX = std::clamp(x, -10000.0F, 10000.0F);
    const float clampedY = std::clamp(y, -10000.0F, 10000.0F);
    if (comment->layout.x == clampedX && comment->layout.y == clampedY)
        return;
    beginLayoutTransaction();
    comment->layout.x = clampedX;
    comment->layout.y = clampedY;
    m_layoutTransactionChanged = true;
    setGraphModified(true);
}

void VisualScriptEditorWidget::addCommentBox() {
    if (m_comments.size() >= fabgl::VisualGraphFormatLimits{}.maximumComments ||
        m_nextCommentId == 0)
        return;
    recordUndoPoint();
    fabgl::VisualCommentBox comment;
    comment.id = m_nextCommentId++;
    comment.title = tr("Comment %1").arg(comment.id).toStdString();
    comment.layout = {-40.0F + static_cast<float>(comment.id % 8U) * 24.0F,
                      -40.0F + static_cast<float>(comment.id % 8U) * 24.0F, 360.0F, 220.0F};
    m_comments.push_back(std::move(comment));
    refreshCanvas();
    setGraphModified(true);
}

void VisualScriptEditorWidget::copySelectedCanvasItems() {
    m_clipboardNodes.clear();
    m_clipboardEdges.clear();
    m_clipboardComments.clear();
    if (m_graphScene == nullptr)
        return;
    std::set<fabgl::VisualNodeId> selectedNodes;
    std::set<std::uint16_t> selectedComments;
    for (const auto* item : m_graphScene->selectedItems()) {
        if (item->data(CanvasKindRole).toInt() == CanvasNodeKind)
            selectedNodes.insert(
                static_cast<fabgl::VisualNodeId>(item->data(CanvasIdRole).toUInt()));
        else if (item->data(CanvasKindRole).toInt() == CanvasCommentKind)
            selectedComments.insert(static_cast<std::uint16_t>(item->data(CanvasIdRole).toUInt()));
    }
    for (const auto& node : m_nodes) {
        if (selectedNodes.contains(node.id))
            m_clipboardNodes.push_back(node);
    }
    for (const auto& edge : m_edges) {
        if (selectedNodes.contains(edge.sourceNode) && selectedNodes.contains(edge.targetNode))
            m_clipboardEdges.push_back(edge);
    }
    for (const auto& comment : m_comments) {
        if (selectedComments.contains(comment.id))
            m_clipboardComments.push_back(comment);
    }
    emit statusMessage(tr("Copied %1 node(s) and %2 comment(s).")
                           .arg(m_clipboardNodes.size())
                           .arg(m_clipboardComments.size()));
}

void VisualScriptEditorWidget::pasteCanvasItems() {
    if (m_clipboardNodes.empty() && m_clipboardComments.empty())
        return;
    if (m_nodes.size() + m_clipboardNodes.size() > fabgl::VisualMaximumGraphNodes)
        return;
    recordUndoPoint();
    std::unordered_map<fabgl::VisualNodeId, fabgl::VisualNodeId> remap;
    for (auto node : m_clipboardNodes) {
        if (m_nextNodeId == 0)
            return;
        const auto old = node.id;
        node.id = m_nextNodeId++;
        node.name += " Copy";
        node.layout.x += 32.0F;
        node.layout.y += 32.0F;
        remap.emplace(old, node.id);
        m_nodes.push_back(std::move(node));
    }
    for (auto edge : m_clipboardEdges) {
        const auto source = remap.find(edge.sourceNode);
        const auto target = remap.find(edge.targetNode);
        if (source != remap.end() && target != remap.end()) {
            edge.sourceNode = source->second;
            edge.targetNode = target->second;
            m_edges.push_back(edge);
        }
    }
    for (auto comment : m_clipboardComments) {
        if (m_nextCommentId == 0)
            break;
        comment.id = m_nextCommentId++;
        comment.title += " Copy";
        comment.layout.x += 32.0F;
        comment.layout.y += 32.0F;
        m_comments.push_back(std::move(comment));
    }
    for (auto& node : m_clipboardNodes) {
        node.layout.x += 32.0F;
        node.layout.y += 32.0F;
    }
    for (auto& comment : m_clipboardComments) {
        comment.layout.x += 32.0F;
        comment.layout.y += 32.0F;
    }
    refreshNodeList();
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

void VisualScriptEditorWidget::removeSelectedCanvasItems() {
    if (m_graphScene == nullptr)
        return;
    std::set<fabgl::VisualNodeId> nodes;
    std::set<std::uint16_t> comments;
    for (const auto* item : m_graphScene->selectedItems()) {
        if (item->data(CanvasKindRole).toInt() == CanvasNodeKind)
            nodes.insert(static_cast<fabgl::VisualNodeId>(item->data(CanvasIdRole).toUInt()));
        else if (item->data(CanvasKindRole).toInt() == CanvasCommentKind)
            comments.insert(static_cast<std::uint16_t>(item->data(CanvasIdRole).toUInt()));
    }
    if (nodes.empty() && comments.empty())
        return;
    recordUndoPoint();
    std::erase_if(m_nodes, [&nodes](const auto& node) { return nodes.contains(node.id); });
    std::erase_if(m_edges, [&nodes](const auto& edge) {
        return nodes.contains(edge.sourceNode) || nodes.contains(edge.targetNode);
    });
    std::erase_if(m_comments,
                  [&comments](const auto& comment) { return comments.contains(comment.id); });
    if (nodes.contains(m_entryNodeId))
        m_entryNodeId = 0;
    refreshNodeList();
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

void VisualScriptEditorWidget::handleCanvasConnection(const fabgl::VisualNodeId sourceNode,
                                                      const fabgl::VisualPinId sourcePin,
                                                      const fabgl::VisualNodeId targetNode,
                                                      const fabgl::VisualPinId targetPin) {
    const fabgl::VisualEdge edge{sourceNode, sourcePin, targetNode, targetPin};
    const auto duplicate =
        std::find_if(m_edges.cbegin(), m_edges.cend(), [&edge](const auto& item) {
            return item.sourceNode == edge.sourceNode && item.sourcePin == edge.sourcePin &&
                   item.targetNode == edge.targetNode && item.targetPin == edge.targetPin;
        });
    if (duplicate != m_edges.cend())
        return;
    fabgl::VisualGraph graph;
    QString error;
    m_edges.push_back(edge);
    if (!buildGraph(graph, error)) {
        m_edges.pop_back();
        emit statusMessage(tr("Cannot connect pins: %1").arg(error));
        return;
    }
    m_edges.pop_back();
    recordUndoPoint();
    m_edges.push_back(edge);
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

void VisualScriptEditorWidget::addNode() {
    recordUndoPoint();
    if (m_nextNodeId == 0) {
        m_undoHistory.pop_back();
        updateGraphHistoryActions();
        return;
    }
    const auto type = static_cast<fabgl::VisualBuiltinNodeType>(m_kindCombo->currentData().toInt());
    const auto* definition = fabgl::VisualNodeRegistry::builtins().find(type);
    if (definition == nullptr) {
        m_undoHistory.pop_back();
        updateGraphHistoryActions();
        return;
    }
    const auto id = m_nextNodeId++;
    auto created = fabgl::VisualNodeRegistry::builtins().create(
        type, id,
        QStringLiteral("%1 %2")
            .arg(QString::fromStdString(definition->displayName))
            .arg(id)
            .toStdString());
    if (!created) {
        --m_nextNodeId;
        m_undoHistory.pop_back();
        updateGraphHistoryActions();
        emit statusMessage(tr("Cannot create node: %1").arg(animationError(created.error())));
        return;
    }
    auto node = std::move(created.value());
    node.callbackName = definition->hostCallback;
    if (type == fabgl::VisualBuiltinNodeType::FunctionCall)
        node.callbackName = fabgl::project::ProjectVisualHost::safeFunctionCallbacks().front();
    if (type == fabgl::VisualBuiltinNodeType::VariableGet ||
        type == fabgl::VisualBuiltinNodeType::VariableSet)
        node.variableName = "value";
    m_nodes.push_back(std::move(node));
    m_nodes.back().layout.x = static_cast<float>((m_nodes.size() % 5U) * 210U);
    m_nodes.back().layout.y = static_cast<float>((m_nodes.size() / 5U) * 130U);
    if (definition->execution == fabgl::VisualExecutionKind::Entry && m_entryNodeId == 0) {
        m_entryNodeId = id;
    }
    refreshNodeList();
    for (int index = 0; index < m_nodeTree->topLevelItemCount(); ++index) {
        auto* item = m_nodeTree->topLevelItem(index);
        if (item->data(0, Qt::UserRole).toUInt() == id) {
            m_nodeTree->setCurrentItem(item);
            break;
        }
    }
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

void VisualScriptEditorWidget::removeSelectedNode() {
    auto* node = selectedNode();
    if (node == nullptr) {
        return;
    }
    const auto id = node->id;
    recordUndoPoint();
    std::erase_if(m_nodes, [id](const fabgl::VisualNode& candidate) { return candidate.id == id; });
    std::erase_if(m_edges, [id](const fabgl::VisualEdge& edge) {
        return edge.sourceNode == id || edge.targetNode == id;
    });
    if (m_entryNodeId == id) {
        m_entryNodeId = 0;
        const auto entry =
            std::find_if(m_nodes.cbegin(), m_nodes.cend(), [](const auto& candidate) {
                return candidate.kind == fabgl::VisualNodeKind::Entry;
            });
        if (entry != m_nodes.cend()) {
            m_entryNodeId = entry->id;
        }
    }
    refreshNodeList();
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

void VisualScriptEditorWidget::addConnection() {
    const fabgl::VisualEdge edge{selectedNodeId(m_sourceNodeCombo), selectedPinId(m_sourcePinCombo),
                                 selectedNodeId(m_targetNodeCombo),
                                 selectedPinId(m_targetPinCombo)};
    if (edge.sourceNode == 0 || edge.sourcePin == 0 || edge.targetNode == 0 ||
        edge.targetPin == 0) {
        emit statusMessage(tr("Select a source and target pin before adding a connection."));
        return;
    }
    handleCanvasConnection(edge.sourceNode, edge.sourcePin, edge.targetNode, edge.targetPin);
}

void VisualScriptEditorWidget::removeSelectedConnection() {
    const int row = m_connectionTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_edges.size())) {
        return;
    }
    recordUndoPoint();
    m_edges.erase(m_edges.begin() + row);
    refreshConnectionTable();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

void VisualScriptEditorWidget::refreshNodeList() {
    const auto previous = selectedNode() != nullptr ? selectedNode()->id : fabgl::VisualNodeId{0};
    const QSignalBlocker blocker(m_nodeTree);
    m_nodeTree->clear();
    const QString query = m_nodeSearch != nullptr ? m_nodeSearch->text().trimmed() : QString{};
    for (const auto& node : m_nodes) {
        const QString nodeName = QString::fromStdString(node.name);
        const QString kindName = nodeTypeName(node);
        if (!query.isEmpty() && !nodeName.contains(query, Qt::CaseInsensitive) &&
            !kindName.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        QStringList outputs;
        for (const auto& pin : node.pins) {
            if (pin.direction == fabgl::VisualPinDirection::Output) {
                outputs.push_back(QStringLiteral("%1:%2").arg(QString::fromStdString(pin.name),
                                                              valueTypeName(pin.type)));
            }
        }
        auto* item = new QTreeWidgetItem({QString::number(node.id), nodeName, outputs.join(", ")});
        item->setData(0, Qt::UserRole, node.id);
        m_nodeTree->addTopLevelItem(item);
        if (node.id == previous) {
            m_nodeTree->setCurrentItem(item);
        }
    }
    if (m_nodeTree->currentItem() == nullptr && m_nodeTree->topLevelItemCount() > 0) {
        m_nodeTree->setCurrentItem(m_nodeTree->topLevelItem(0));
    }

    const QSignalBlocker sourceBlocker(m_sourceNodeCombo);
    const QSignalBlocker targetBlocker(m_targetNodeCombo);
    const auto sourceId = selectedNodeId(m_sourceNodeCombo);
    const auto targetId = selectedNodeId(m_targetNodeCombo);
    m_sourceNodeCombo->clear();
    m_targetNodeCombo->clear();
    for (const auto& node : m_nodes) {
        const auto label = tr("%1: %2").arg(node.id).arg(QString::fromStdString(node.name));
        m_sourceNodeCombo->addItem(label, node.id);
        m_targetNodeCombo->addItem(label, node.id);
    }
    int index = m_sourceNodeCombo->findData(sourceId);
    m_sourceNodeCombo->setCurrentIndex(index >= 0 ? index : 0);
    index = m_targetNodeCombo->findData(targetId);
    m_targetNodeCombo->setCurrentIndex(index >= 0 ? index
                                                  : qMin(1, m_targetNodeCombo->count() - 1));
    refreshNodeEditors();
    refreshPinCombos();
}

void VisualScriptEditorWidget::refreshConnectionTable() {
    m_connectionTable->setRowCount(static_cast<int>(m_edges.size()));
    for (int row = 0; row < static_cast<int>(m_edges.size()); ++row) {
        const auto& edge = m_edges[static_cast<std::size_t>(row)];
        m_connectionTable->setItem(row, 0, new QTableWidgetItem(QString::number(edge.sourceNode)));
        m_connectionTable->setItem(row, 1, new QTableWidgetItem(QString::number(edge.sourcePin)));
        m_connectionTable->setItem(row, 2, new QTableWidgetItem(QString::number(edge.targetNode)));
        m_connectionTable->setItem(row, 3, new QTableWidgetItem(QString::number(edge.targetPin)));
    }
}

void VisualScriptEditorWidget::refreshNodeEditors() {
    m_updating = true;
    m_nodeEditorError.clear();
    const auto* node = selectedNode();
    const bool available = node != nullptr;
    const auto* definition = available
                                 ? (node->builtinType == fabgl::VisualBuiltinNodeType::Legacy
                                        ? fabgl::VisualNodeRegistry::builtins().findLegacy(node->kind)
                                        : fabgl::VisualNodeRegistry::builtins().find(node->builtinType))
                                 : nullptr;
    m_nameEdit->setEnabled(available);
    m_numberEdit->setEnabled(available && node->kind == fabgl::VisualNodeKind::ConstantNumber);
    m_booleanEdit->setEnabled(available && node->kind == fabgl::VisualNodeKind::ConstantBoolean);
    m_variableEdit->setEnabled(
        available &&
        (node->builtinType == fabgl::VisualBuiltinNodeType::VariableGet ||
         node->builtinType == fabgl::VisualBuiltinNodeType::VariableSet ||
         node->kind == fabgl::VisualNodeKind::GetVariable));
    m_callbackEdit->setEnabled(available && definition != nullptr &&
                               definition->callbackNameRequired);
    const bool hostNode = definition != nullptr &&
                          (definition->execution == fabgl::VisualExecutionKind::HostFlow ||
                           definition->execution == fabgl::VisualExecutionKind::HostValue);
    m_payloadEdit->setEnabled(available && hostNode &&
                              node->builtinType != fabgl::VisualBuiltinNodeType::FlowDelay &&
                              node->builtinType != fabgl::VisualBuiltinNodeType::VectorLength3);
    m_assetReferenceEdit->setEnabled(
        available && definition != nullptr &&
        (definition->assetReferenceRequired || node->assetReference.has_value()));
    m_entityReferenceEdit->setEnabled(
        available && definition != nullptr &&
        (definition->entityReferenceRequired || definition->componentReferenceRequired ||
         node->entityReference.has_value()));
    m_componentReferenceEdit->setEnabled(
        available && definition != nullptr &&
        (definition->componentReferenceRequired || node->componentReference.has_value()));
    m_nameEdit->setText(available ? QString::fromStdString(node->name) : QString{});
    m_numberEdit->setValue(available ? node->numberValue : 0.0);
    m_booleanEdit->setChecked(available && node->booleanValue);
    m_variableEdit->setText(available ? QString::fromStdString(node->variableName) : QString{});
    m_callbackEdit->setEditText(available ? QString::fromStdString(node->callbackName) : QString{});
    m_payloadEdit->setText(available ? QString::fromStdString(node->callbackPayload) : QString{});
    m_assetReferenceEdit->setText(
        available && node->assetReference ? QString::fromStdString(node->assetReference->toString())
                                          : QString{});
    m_entityReferenceEdit->setText(
        available && node->entityReference
            ? QString::fromStdString(node->entityReference->toString())
            : QString{});
    m_componentReferenceEdit->setText(
        available && node->componentReference
            ? QString::fromStdString(node->componentReference->toString())
            : QString{});
    m_updating = false;
}

void VisualScriptEditorWidget::refreshPinCombos() {
    const auto sourceNodeId = selectedNodeId(m_sourceNodeCombo);
    const auto targetNodeId = selectedNodeId(m_targetNodeCombo);
    const QSignalBlocker sourceBlocker(m_sourcePinCombo);
    const QSignalBlocker targetBlocker(m_targetPinCombo);
    m_sourcePinCombo->clear();
    m_targetPinCombo->clear();
    for (const auto& node : m_nodes) {
        if (node.id == sourceNodeId) {
            for (const auto& pin : node.pins) {
                if (pin.direction == fabgl::VisualPinDirection::Output) {
                    m_sourcePinCombo->addItem(tr("%1 (%2)").arg(QString::fromStdString(pin.name),
                                                                valueTypeName(pin.type)),
                                              pin.id);
                }
            }
        }
        if (node.id == targetNodeId) {
            for (const auto& pin : node.pins) {
                if (pin.direction == fabgl::VisualPinDirection::Input) {
                    m_targetPinCombo->addItem(tr("%1 (%2)").arg(QString::fromStdString(pin.name),
                                                                valueTypeName(pin.type)),
                                              pin.id);
                }
            }
        }
    }
}

void VisualScriptEditorWidget::applySelectedNodeEdits() {
    if (m_updating) {
        return;
    }
    auto* node = selectedNode();
    if (node == nullptr) {
        return;
    }
    const QString name = m_nameEdit->text().trimmed();
    const std::string nextName = name.isEmpty() ? node->name : name.toStdString();
    const double nextNumber = m_numberEdit->value();
    const bool nextBoolean = m_booleanEdit->isChecked();
    const std::string nextVariable = m_variableEdit->text().trimmed().toStdString();
    const std::string nextCallback = m_callbackEdit->currentText().trimmed().toStdString();
    const std::string nextPayload = m_payloadEdit->text().trimmed().toStdString();
    std::optional<fabgl::AssetGuid> nextAsset;
    std::optional<fabgl::EntityGuid> nextEntity;
    std::optional<fabgl::ComponentTypeGuid> nextComponent;
    QString editorError;
    if (!parseOptionalGuid(m_assetReferenceEdit->text(), nextAsset, editorError, tr("Asset reference")) ||
        !parseOptionalGuid(m_entityReferenceEdit->text(), nextEntity, editorError, tr("Entity reference")) ||
        !parseOptionalGuid(m_componentReferenceEdit->text(), nextComponent, editorError,
                           tr("Component reference"))) {
        m_nodeEditorError = editorError;
        validateGraph();
        emit statusMessage(editorError);
        return;
    }
    auto authored = *node;
    authored.callbackName = nextCallback;
    authored.callbackPayload = nextPayload;
    authored.assetReference = nextAsset;
    authored.entityReference = nextEntity;
    authored.componentReference = nextComponent;
    editorError = visualNodeAuthoringError(authored);
    if (!editorError.isEmpty()) {
        m_nodeEditorError = editorError;
        validateGraph();
        emit statusMessage(editorError);
        return;
    }
    m_nodeEditorError.clear();
    if (node->name == nextName && node->numberValue == nextNumber &&
        node->booleanValue == nextBoolean && node->variableName == nextVariable &&
        node->callbackName == nextCallback && node->callbackPayload == nextPayload &&
        node->assetReference == nextAsset && node->entityReference == nextEntity &&
        node->componentReference == nextComponent) {
        return;
    }
    recordUndoPoint();
    if (!name.isEmpty()) {
        node->name = nextName;
    }
    node->numberValue = nextNumber;
    node->booleanValue = nextBoolean;
    node->variableName = nextVariable;
    node->callbackName = nextCallback;
    node->callbackPayload = nextPayload;
    node->assetReference = nextAsset;
    node->entityReference = nextEntity;
    node->componentReference = nextComponent;
    refreshNodeList();
    refreshCanvas();
    validateGraph();
    setGraphModified(true);
}

fabgl::VisualNode* VisualScriptEditorWidget::selectedNode() {
    const auto* item = m_nodeTree != nullptr ? m_nodeTree->currentItem() : nullptr;
    if (item == nullptr) {
        return nullptr;
    }
    const auto id = static_cast<fabgl::VisualNodeId>(item->data(0, Qt::UserRole).toUInt());
    const auto iterator = std::find_if(m_nodes.begin(), m_nodes.end(),
                                       [id](const auto& node) { return node.id == id; });
    return iterator == m_nodes.end() ? nullptr : &*iterator;
}

const fabgl::VisualNode* VisualScriptEditorWidget::selectedNode() const {
    const auto* item = m_nodeTree != nullptr ? m_nodeTree->currentItem() : nullptr;
    if (item == nullptr) {
        return nullptr;
    }
    const auto id = static_cast<fabgl::VisualNodeId>(item->data(0, Qt::UserRole).toUInt());
    const auto iterator = std::find_if(m_nodes.cbegin(), m_nodes.cend(),
                                       [id](const auto& node) { return node.id == id; });
    return iterator == m_nodes.cend() ? nullptr : &*iterator;
}

fabgl::VisualNodeId VisualScriptEditorWidget::selectedNodeId(const QComboBox* combo) const {
    return combo != nullptr && combo->currentIndex() >= 0
               ? static_cast<fabgl::VisualNodeId>(combo->currentData().toUInt())
               : fabgl::VisualNodeId{0};
}

fabgl::VisualPinId VisualScriptEditorWidget::selectedPinId(const QComboBox* combo) const {
    return combo != nullptr && combo->currentIndex() >= 0
               ? static_cast<fabgl::VisualPinId>(combo->currentData().toUInt())
               : fabgl::VisualPinId{0};
}

AnimatorEditorWidget::AnimatorEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("animatorEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    m_assetUndoStack = new QUndoStack(this);

    auto* fileRow = new QHBoxLayout();
    auto* newControllerButton = new QPushButton(tr("New Controller"), this);
    newControllerButton->setObjectName(QStringLiteral("animatorNewControllerButton"));
    auto* newClipButton = new QPushButton(tr("New Clip"), this);
    newClipButton->setObjectName(QStringLiteral("animatorNewClipButton"));
    auto* openButton = new QPushButton(tr("Open"), this);
    openButton->setObjectName(QStringLiteral("animatorOpenButton"));
    auto* saveButton = new QPushButton(tr("Save"), this);
    saveButton->setObjectName(QStringLiteral("animatorSaveButton"));
    auto* saveAsButton = new QPushButton(tr("Save As"), this);
    saveAsButton->setObjectName(QStringLiteral("animatorSaveAsButton"));
    m_assetUndoButton = new QPushButton(tr("Undo"), this);
    m_assetUndoButton->setObjectName(QStringLiteral("animatorUndoButton"));
    m_assetRedoButton = new QPushButton(tr("Redo"), this);
    m_assetRedoButton->setObjectName(QStringLiteral("animatorRedoButton"));
    m_assetStatus = new QLabel(this);
    m_assetStatus->setObjectName(QStringLiteral("animatorAssetStatus"));
    fileRow->addWidget(newControllerButton);
    fileRow->addWidget(newClipButton);
    fileRow->addWidget(openButton);
    fileRow->addWidget(saveButton);
    fileRow->addWidget(saveAsButton);
    fileRow->addWidget(m_assetUndoButton);
    fileRow->addWidget(m_assetRedoButton);
    fileRow->addWidget(m_assetStatus, 1);
    layout->addLayout(fileRow);

    m_assetPages = new QStackedWidget(this);
    m_assetPages->setObjectName(QStringLiteral("animatorAssetPages"));

    auto* controllerPage = new QWidget(m_assetPages);
    auto* controllerLayout = new QVBoxLayout(controllerPage);
    controllerLayout->setContentsMargins(0, 0, 0, 0);
    auto* controllerIdentity = new QGroupBox(tr("Controller Asset"), controllerPage);
    auto* controllerForm = new QFormLayout(controllerIdentity);
    m_controllerName = new QLineEdit(controllerIdentity);
    m_controllerName->setObjectName(QStringLiteral("animatorControllerNameEdit"));
    m_controllerGuid = new QLineEdit(controllerIdentity);
    m_controllerGuid->setObjectName(QStringLiteral("animatorControllerGuidEdit"));
    m_initialState = new QLineEdit(controllerIdentity);
    m_initialState->setObjectName(QStringLiteral("animatorInitialStateEdit"));
    controllerForm->addRow(tr("Name"), m_controllerName);
    controllerForm->addRow(tr("GUID"), m_controllerGuid);
    controllerForm->addRow(tr("Initial State"), m_initialState);
    controllerLayout->addWidget(controllerIdentity);

    const auto addTableSection = [this, controllerLayout, controllerPage](
                                     const QString& title, QTableWidget*& table,
                                     const QStringList& headers, const QString& objectName,
                                     const QString& addObjectName, const QString& removeObjectName,
                                     auto addHandler, auto removeHandler) {
        auto* group = new QGroupBox(title, controllerPage);
        auto* groupLayout = new QVBoxLayout(group);
        table = new QTableWidget(0, static_cast<int>(headers.size()), group);
        table->setObjectName(objectName);
        table->setHorizontalHeaderLabels(headers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        groupLayout->addWidget(table);
        auto* buttons = new QHBoxLayout();
        auto* addButton = new QPushButton(tr("Add"), group);
        addButton->setObjectName(addObjectName);
        auto* removeButton = new QPushButton(tr("Remove"), group);
        removeButton->setObjectName(removeObjectName);
        buttons->addStretch();
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        groupLayout->addLayout(buttons);
        connect(addButton, &QPushButton::clicked, this, addHandler);
        connect(removeButton, &QPushButton::clicked, this, removeHandler);
        controllerLayout->addWidget(group);
    };

    addTableSection(tr("States"), m_states,
                    {tr("Name"), tr("Duration"), tr("Loop"), tr("Clip GUID")},
                    QStringLiteral("animatorStatesTable"), QStringLiteral("animatorAddStateButton"),
                    QStringLiteral("animatorRemoveStateButton"), &AnimatorEditorWidget::addState,
                    &AnimatorEditorWidget::removeSelectedState);
    addTableSection(
        tr("Parameters"), m_parameters, {tr("Name"), tr("Type"), tr("Value")},
        QStringLiteral("animatorParametersTable"), QStringLiteral("animatorAddParameterButton"),
        QStringLiteral("animatorRemoveParameterButton"), &AnimatorEditorWidget::addParameter,
        &AnimatorEditorWidget::removeSelectedParameter);
    addTableSection(
        tr("Transitions"), m_transitions,
        {tr("From"), tr("To"), tr("Parameter"), tr("Mode"), tr("Expected"), tr("Minimum Time"),
         tr("Exit Time"), tr("Blend")},
        QStringLiteral("animatorTransitionsTable"), QStringLiteral("animatorAddTransitionButton"),
        QStringLiteral("animatorRemoveTransitionButton"), &AnimatorEditorWidget::addTransition,
        &AnimatorEditorWidget::removeSelectedTransition);
    m_assetPages->addWidget(controllerPage);

    auto* clipPage = new QWidget(m_assetPages);
    auto* clipLayout = new QVBoxLayout(clipPage);
    clipLayout->setContentsMargins(0, 0, 0, 0);
    auto* clipIdentity = new QGroupBox(tr("Animation Clip Asset"), clipPage);
    auto* clipForm = new QFormLayout(clipIdentity);
    m_clipName = new QLineEdit(clipIdentity);
    m_clipName->setObjectName(QStringLiteral("animatorClipNameEdit"));
    m_clipGuid = new QLineEdit(clipIdentity);
    m_clipGuid->setObjectName(QStringLiteral("animatorClipGuidEdit"));
    m_clipDuration = new QDoubleSpinBox(clipIdentity);
    m_clipDuration->setObjectName(QStringLiteral("animatorClipDurationSpin"));
    m_clipDuration->setRange(0.001, 86400.0);
    m_clipDuration->setDecimals(4);
    m_clipLooping = new QCheckBox(tr("Loop"), clipIdentity);
    m_clipLooping->setObjectName(QStringLiteral("animatorClipLoopCheck"));
    clipForm->addRow(tr("Name"), m_clipName);
    clipForm->addRow(tr("GUID"), m_clipGuid);
    clipForm->addRow(tr("Duration (seconds)"), m_clipDuration);
    clipForm->addRow(tr("Playback"), m_clipLooping);
    clipLayout->addWidget(clipIdentity);

    auto* keysGroup = new QGroupBox(tr("Curve Keys"), clipPage);
    auto* keysLayout = new QVBoxLayout(keysGroup);
    m_clipKeys = new QTableWidget(0, 6, keysGroup);
    m_clipKeys->setObjectName(QStringLiteral("animatorClipKeysTable"));
    m_clipKeys->setHorizontalHeaderLabels({tr("Property"), tr("Time"), tr("Value"),
                                           tr("In Tangent"), tr("Out Tangent"),
                                           tr("Interpolation")});
    m_clipKeys->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_clipKeys->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    keysLayout->addWidget(m_clipKeys);
    auto* keyButtons = new QHBoxLayout();
    auto* addKeyButton = new QPushButton(tr("Add Key"), keysGroup);
    addKeyButton->setObjectName(QStringLiteral("animatorAddKeyButton"));
    auto* removeKeyButton = new QPushButton(tr("Remove Key"), keysGroup);
    removeKeyButton->setObjectName(QStringLiteral("animatorRemoveKeyButton"));
    keyButtons->addStretch();
    keyButtons->addWidget(addKeyButton);
    keyButtons->addWidget(removeKeyButton);
    keysLayout->addLayout(keyButtons);
    clipLayout->addWidget(keysGroup, 2);

    auto* eventsGroup = new QGroupBox(tr("Events"), clipPage);
    auto* eventsLayout = new QVBoxLayout(eventsGroup);
    m_clipEvents = new QTableWidget(0, 2, eventsGroup);
    m_clipEvents->setObjectName(QStringLiteral("animatorClipEventsTable"));
    m_clipEvents->setHorizontalHeaderLabels({tr("Time"), tr("Name")});
    m_clipEvents->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_clipEvents->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    eventsLayout->addWidget(m_clipEvents);
    auto* eventButtons = new QHBoxLayout();
    auto* addEventButton = new QPushButton(tr("Add Event"), eventsGroup);
    addEventButton->setObjectName(QStringLiteral("animatorAddEventButton"));
    auto* removeEventButton = new QPushButton(tr("Remove Event"), eventsGroup);
    removeEventButton->setObjectName(QStringLiteral("animatorRemoveEventButton"));
    eventButtons->addStretch();
    eventButtons->addWidget(addEventButton);
    eventButtons->addWidget(removeEventButton);
    eventsLayout->addLayout(eventButtons);
    clipLayout->addWidget(eventsGroup, 1);
    m_assetPages->addWidget(clipPage);
    layout->addWidget(m_assetPages, 1);

    auto* previewRow = new QHBoxLayout();
    m_previewPlayButton = new QPushButton(tr("Play"), this);
    m_previewPlayButton->setObjectName(QStringLiteral("animatorPreviewPlayButton"));
    m_timeline = new QSlider(Qt::Horizontal, this);
    m_timeline->setObjectName(QStringLiteral("animatorTimelineSlider"));
    m_timeline->setRange(0, 1000);
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("animatorPreviewStatus"));
    previewRow->addWidget(m_previewPlayButton);
    previewRow->addWidget(m_timeline, 1);
    previewRow->addWidget(m_preview);
    layout->addLayout(previewRow);

    auto* validationRow = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("animatorValidationStatus"));
    auto* validateButton = new QPushButton(tr("Validate & Preview"), this);
    validateButton->setObjectName(QStringLiteral("animatorValidateButton"));
    validationRow->addWidget(m_status, 1);
    validationRow->addWidget(validateButton);
    layout->addLayout(validationRow);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(16);
    const auto assetEdited = [this]() {
        if (m_updating)
            return;
        setAssetModified(true);
        validateController();
        recordAnimatorEdit(tr("Edit Animator Asset"));
    };
    connect(m_states, &QTableWidget::cellChanged, this, [assetEdited](int, int) { assetEdited(); });
    connect(m_parameters, &QTableWidget::cellChanged, this,
            [assetEdited](int, int) { assetEdited(); });
    connect(m_transitions, &QTableWidget::cellChanged, this,
            [assetEdited](int, int) { assetEdited(); });
    connect(m_clipKeys, &QTableWidget::cellChanged, this,
            [assetEdited](int, int) { assetEdited(); });
    connect(m_clipEvents, &QTableWidget::cellChanged, this,
            [assetEdited](int, int) { assetEdited(); });
    connect(m_controllerName, &QLineEdit::textChanged, this,
            [assetEdited](const QString&) { assetEdited(); });
    connect(m_controllerGuid, &QLineEdit::textChanged, this,
            [assetEdited](const QString&) { assetEdited(); });
    connect(m_initialState, &QLineEdit::textChanged, this,
            [assetEdited](const QString&) { assetEdited(); });
    connect(m_clipName, &QLineEdit::textChanged, this,
            [assetEdited](const QString&) { assetEdited(); });
    connect(m_clipGuid, &QLineEdit::textChanged, this,
            [assetEdited](const QString&) { assetEdited(); });
    connect(m_clipDuration, &QDoubleSpinBox::valueChanged, this,
            [assetEdited](double) { assetEdited(); });
    connect(m_clipLooping, &QCheckBox::toggled, this, [assetEdited](bool) { assetEdited(); });
    connect(m_assetUndoButton, &QPushButton::clicked, this, &AnimatorEditorWidget::undoAssetEdit);
    connect(m_assetRedoButton, &QPushButton::clicked, this, &AnimatorEditorWidget::redoAssetEdit);
    connect(m_assetUndoStack, &QUndoStack::canUndoChanged, m_assetUndoButton,
            &QPushButton::setEnabled);
    connect(m_assetUndoStack, &QUndoStack::canRedoChanged, m_assetRedoButton,
            &QPushButton::setEnabled);
    m_assetUndoButton->setEnabled(false);
    m_assetRedoButton->setEnabled(false);
    connect(m_states, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { updatePreview(); });
    connect(m_timeline, &QSlider::valueChanged, this, [this](int) { updatePreview(); });
    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        float duration = m_assetMode == AssetMode::AnimationClip
                             ? static_cast<float>(m_clipDuration->value())
                             : 1.0F;
        if (m_assetMode == AssetMode::Controller && m_states->currentRow() >= 0) {
            bool ok = false;
            const float stateDuration = tableText(m_states, m_states->currentRow(), 1).toFloat(&ok);
            if (ok && stateDuration > 0.0F)
                duration = stateDuration;
        }
        const int increment =
            std::max(1, static_cast<int>(std::lround(0.016F / std::max(0.001F, duration) *
                                                     static_cast<float>(m_timeline->maximum()))));
        const int next = m_timeline->value() + increment;
        if (next > m_timeline->maximum()) {
            const bool loop = m_assetMode == AssetMode::AnimationClip && m_clipLooping->isChecked();
            if (!loop) {
                m_previewTimer->stop();
                m_previewPlayButton->setText(tr("Play"));
                return;
            }
            m_timeline->setValue(0);
        } else {
            m_timeline->setValue(next);
        }
    });
    connect(m_previewPlayButton, &QPushButton::clicked, this, [this]() {
        if (m_previewTimer->isActive()) {
            m_previewTimer->stop();
            m_previewPlayButton->setText(tr("Play"));
        } else {
            if (m_timeline->value() >= m_timeline->maximum())
                m_timeline->setValue(0);
            m_previewTimer->start();
            m_previewPlayButton->setText(tr("Pause"));
        }
    });
    connect(newControllerButton, &QPushButton::clicked, this, &AnimatorEditorWidget::newController);
    connect(newClipButton, &QPushButton::clicked, this, &AnimatorEditorWidget::newAnimationClip);
    connect(openButton, &QPushButton::clicked, this, &AnimatorEditorWidget::openAssetDialog);
    connect(saveButton, &QPushButton::clicked, this, [this]() { saveAssetDialog(false); });
    connect(saveAsButton, &QPushButton::clicked, this, [this]() { saveAssetDialog(true); });
    connect(addKeyButton, &QPushButton::clicked, this, &AnimatorEditorWidget::addClipKey);
    connect(removeKeyButton, &QPushButton::clicked, this,
            &AnimatorEditorWidget::removeSelectedClipKey);
    connect(addEventButton, &QPushButton::clicked, this, &AnimatorEditorWidget::addClipEvent);
    connect(removeEventButton, &QPushButton::clicked, this,
            &AnimatorEditorWidget::removeSelectedClipEvent);
    connect(validateButton, &QPushButton::clicked, this, &AnimatorEditorWidget::validateController);
    newController();
}

int AnimatorEditorWidget::stateCount() const noexcept {
    return m_states->rowCount();
}

int AnimatorEditorWidget::parameterCount() const noexcept {
    return m_parameters->rowCount();
}

int AnimatorEditorWidget::transitionCount() const noexcept {
    std::set<qlonglong> grouped;
    int ungrouped = 0;
    for (int row = 0; row < m_transitions->rowCount(); ++row) {
        const auto* item = m_transitions->item(row, 0);
        const QVariant group = item != nullptr ? item->data(Qt::UserRole) : QVariant{};
        if (group.isValid())
            grouped.insert(group.toLongLong());
        else
            ++ungrouped;
    }
    return static_cast<int>(grouped.size()) + ungrouped;
}

QString AnimatorEditorWidget::validationText() const {
    return m_status->text();
}

QString AnimatorEditorWidget::assetFilePath() const {
    return m_assetFilePath;
}

bool AnimatorEditorWidget::assetModified() const noexcept {
    return m_assetModified;
}

QString AnimatorEditorWidget::previewText() const {
    return m_preview != nullptr ? m_preview->text() : QString{};
}

bool AnimatorEditorWidget::canUndoAssetEdit() const noexcept {
    return m_assetUndoStack != nullptr && m_assetUndoStack->canUndo();
}

bool AnimatorEditorWidget::canRedoAssetEdit() const noexcept {
    return m_assetUndoStack != nullptr && m_assetUndoStack->canRedo();
}

AnimatorEditorWidget::AnimatorSnapshot AnimatorEditorWidget::animatorSnapshot() const {
    const auto tableRows = [](const QTableWidget* table) {
        QVector<QStringList> rows;
        if (table == nullptr)
            return rows;
        rows.reserve(table->rowCount());
        for (int row = 0; row < table->rowCount(); ++row) {
            QStringList values;
            values.reserve(table->columnCount());
            for (int column = 0; column < table->columnCount(); ++column)
                values.push_back(tableText(table, row, column));
            rows.push_back(std::move(values));
        }
        return rows;
    };
    AnimatorSnapshot snapshot;
    snapshot.mode = m_assetMode;
    snapshot.controllerName = m_controllerName->text();
    snapshot.controllerGuid = m_controllerGuid->text();
    snapshot.initialState = m_initialState->text();
    snapshot.states = tableRows(m_states);
    snapshot.parameters = tableRows(m_parameters);
    snapshot.transitions = tableRows(m_transitions);
    snapshot.transitionGroups.reserve(m_transitions->rowCount());
    constexpr qlonglong NoGroup = std::numeric_limits<qlonglong>::min();
    for (int row = 0; row < m_transitions->rowCount(); ++row) {
        const auto* item = m_transitions->item(row, 0);
        const QVariant group = item != nullptr ? item->data(Qt::UserRole) : QVariant{};
        snapshot.transitionGroups.push_back(group.isValid() ? group.toLongLong() : NoGroup);
    }
    snapshot.clipName = m_clipName->text();
    snapshot.clipGuid = m_clipGuid->text();
    snapshot.clipDuration = m_clipDuration->value();
    snapshot.clipLooping = m_clipLooping->isChecked();
    snapshot.keys = tableRows(m_clipKeys);
    snapshot.events = tableRows(m_clipEvents);
    snapshot.modified = m_assetModified;
    return snapshot;
}

void AnimatorEditorWidget::restoreAnimatorSnapshot(const AnimatorSnapshot& snapshot) {
    const auto restoreTable = [](QTableWidget* table, const QVector<QStringList>& rows) {
        table->setRowCount(0);
        for (const auto& values : rows) {
            const int row = table->rowCount();
            table->insertRow(row);
            for (int column = 0; column < table->columnCount() && column < values.size(); ++column)
                table->setItem(row, column, editableItem(values.at(column)));
        }
    };
    m_restoringHistory = true;
    m_updating = true;
    m_assetMode = snapshot.mode;
    m_assetPages->setCurrentIndex(m_assetMode == AssetMode::Controller ? 0 : 1);
    m_controllerName->setText(snapshot.controllerName);
    m_controllerGuid->setText(snapshot.controllerGuid);
    m_initialState->setText(snapshot.initialState);
    restoreTable(m_states, snapshot.states);
    restoreTable(m_parameters, snapshot.parameters);
    restoreTable(m_transitions, snapshot.transitions);
    constexpr qlonglong NoGroup = std::numeric_limits<qlonglong>::min();
    for (int row = 0; row < m_transitions->rowCount() && row < snapshot.transitionGroups.size();
         ++row) {
        if (snapshot.transitionGroups.at(row) != NoGroup)
            m_transitions->item(row, 0)->setData(Qt::UserRole, snapshot.transitionGroups.at(row));
    }
    if (m_states->rowCount() > 0)
        m_states->setCurrentCell(0, 0);
    m_clipName->setText(snapshot.clipName);
    m_clipGuid->setText(snapshot.clipGuid);
    m_clipDuration->setValue(snapshot.clipDuration);
    m_clipLooping->setChecked(snapshot.clipLooping);
    restoreTable(m_clipKeys, snapshot.keys);
    restoreTable(m_clipEvents, snapshot.events);
    m_updating = false;
    setAssetModified(snapshot.modified);
    validateController();
    m_lastAnimatorSnapshot = animatorSnapshot();
    m_restoringHistory = false;
}

void AnimatorEditorWidget::recordAnimatorEdit(const QString& description) {
    if (!m_historyReady || m_restoringHistory || m_updating || m_assetUndoStack == nullptr)
        return;
    const AnimatorSnapshot next = animatorSnapshot();
    const auto same = [](const AnimatorSnapshot& lhs, const AnimatorSnapshot& rhs) {
        return lhs.mode == rhs.mode && lhs.controllerName == rhs.controllerName &&
               lhs.controllerGuid == rhs.controllerGuid && lhs.initialState == rhs.initialState &&
               lhs.states == rhs.states && lhs.parameters == rhs.parameters &&
               lhs.transitions == rhs.transitions && lhs.transitionGroups == rhs.transitionGroups &&
               lhs.clipName == rhs.clipName && lhs.clipGuid == rhs.clipGuid &&
               lhs.clipDuration == rhs.clipDuration && lhs.clipLooping == rhs.clipLooping &&
               lhs.keys == rhs.keys && lhs.events == rhs.events && lhs.modified == rhs.modified;
    };
    if (same(m_lastAnimatorSnapshot, next))
        return;
    const AnimatorSnapshot previous = m_lastAnimatorSnapshot;
    m_assetUndoStack->push(new CallbackUndoCommand(
        description, [this, previous]() { restoreAnimatorSnapshot(previous); },
        [this, next]() { restoreAnimatorSnapshot(next); }));
}

void AnimatorEditorWidget::resetAnimatorHistory() {
    m_historyReady = false;
    m_assetUndoStack->clear();
    m_lastAnimatorSnapshot = animatorSnapshot();
    m_assetUndoStack->setClean();
    m_historyReady = true;
}

void AnimatorEditorWidget::undoAssetEdit() {
    if (m_assetUndoStack != nullptr)
        m_assetUndoStack->undo();
}

void AnimatorEditorWidget::redoAssetEdit() {
    if (m_assetUndoStack != nullptr)
        m_assetUndoStack->redo();
}

void AnimatorEditorWidget::validateController() {
    if (m_updating) {
        return;
    }
    QString authoringError;
    bool valid = m_assetMode == AssetMode::Controller ? collectControllerFromTables(authoringError)
                                                      : collectClipFromTables(authoringError);
    if (valid) {
        if (m_assetMode == AssetMode::Controller) {
            auto serialized = fabgl::serializeAnimatorControllerAsset(m_controllerAsset);
            if (!serialized) {
                valid = false;
                authoringError = animationError(serialized.error());
            }
        } else {
            auto serialized = fabgl::serializeAnimationClipAsset(m_clipAsset);
            if (!serialized) {
                valid = false;
                authoringError = animationError(serialized.error());
            }
        }
    }
    if (valid) {
        if (m_assetMode == AssetMode::Controller) {
            m_status->setText(
                tr("Valid controller - %1 state(s), %2 parameter(s), %3 transition(s)")
                    .arg(stateCount())
                    .arg(parameterCount())
                    .arg(transitionCount()));
        } else {
            std::size_t keyCount = 0;
            for (const auto& [property, curve] : m_clipAsset.tracks) {
                Q_UNUSED(property);
                keyCount += curve.keys().size();
            }
            m_status->setText(tr("Valid animation clip - %1 track(s), %2 key(s), %3 event(s)")
                                  .arg(m_clipAsset.tracks.size())
                                  .arg(keyCount)
                                  .arg(m_clipAsset.events.size()));
        }
        m_status->setStyleSheet(QStringLiteral("color: #65d46e;"));
    } else {
        m_status->setText(
            tr("Invalid %1 - %2")
                .arg(m_assetMode == AssetMode::Controller ? tr("controller") : tr("animation clip"),
                     authoringError));
        m_status->setStyleSheet(QStringLiteral("color: #ff6b6b;"));
    }
    updatePreview();
    emit statusMessage(m_status->text());
    return;
#if 0 // Legacy prototype kept unreachable while the v1 authoring model above is active.
    QString error;
    fabgl::AnimatorController controller;
    for (int row = 0; row < m_states->rowCount(); ++row) {
        const QString name = tableText(m_states, row, 0);
        bool durationOk = false;
        const float duration = tableText(m_states, row, 1).toFloat(&durationOk);
        if (name.isEmpty() || !durationOk || duration <= 0.0F || !std::isfinite(duration)) {
            error = tr("State row %1 has an invalid name or duration.").arg(row + 1);
            break;
        }
        auto clip = std::make_shared<fabgl::AnimationClip>(
            name.toStdString(), duration, textBoolean(tableText(m_states, row, 2)));
        auto result = controller.addState(name.toStdString(), std::move(clip));
        if (!result) {
            error = tr("State row %1: %2").arg(row + 1).arg(animationError(result.error()));
            break;
        }
    }

    if (error.isEmpty()) {
        for (int row = 0; row < m_parameters->rowCount(); ++row) {
            const QString name = tableText(m_parameters, row, 0);
            const QString type = tableText(m_parameters, row, 1).toLower();
            const QString value = tableText(m_parameters, row, 2);
            if (name.isEmpty()) {
                error = tr("Parameter row %1 has an empty name.").arg(row + 1);
                break;
            }
            if (type == QStringLiteral("boolean") || type == QStringLiteral("bool")) {
                controller.setBoolean(name.toStdString(), textBoolean(value));
            } else if (type == QStringLiteral("integer") || type == QStringLiteral("int")) {
                bool ok = false;
                const qlonglong integer = value.toLongLong(&ok);
                if (!ok) {
                    error = tr("Parameter row %1 has an invalid integer.").arg(row + 1);
                    break;
                }
                controller.setInteger(name.toStdString(), integer);
            } else if (type == QStringLiteral("float") || type == QStringLiteral("number")) {
                bool ok = false;
                const float number = value.toFloat(&ok);
                if (!ok || !std::isfinite(number)) {
                    error = tr("Parameter row %1 has an invalid float.").arg(row + 1);
                    break;
                }
                controller.setFloat(name.toStdString(), number);
            } else if (type == QStringLiteral("trigger")) {
                if (textBoolean(value)) {
                    controller.setTrigger(name.toStdString());
                }
            } else {
                error = tr("Parameter row %1 uses an unknown type.").arg(row + 1);
                break;
            }
        }
    }

    if (error.isEmpty()) {
        for (int row = 0; row < m_transitions->rowCount(); ++row) {
            fabgl::AnimationTransition transition;
            transition.fromState = tableText(m_transitions, row, 0).toStdString();
            transition.toState = tableText(m_transitions, row, 1).toStdString();
            const QString parameter = tableText(m_transitions, row, 2);
            const QString mode = tableText(m_transitions, row, 3).toLower();
            const QString expected = tableText(m_transitions, row, 4);
            bool minimumOk = false;
            transition.minimumNormalizedTime = tableText(m_transitions, row, 5).toFloat(&minimumOk);
            bool exitOk = false;
            const float exitTime = tableText(m_transitions, row, 6).toFloat(&exitOk);
            bool blendOk = false;
            transition.blendDurationSeconds = tableText(m_transitions, row, 7).toFloat(&blendOk);
            if (!minimumOk || !exitOk || !blendOk) {
                error = tr("Transition row %1 has invalid timing values.").arg(row + 1);
                break;
            }
            transition.hasExitTime = exitTime >= 0.0F;
            transition.exitTime = std::max(0.0F, exitTime);
            if (!parameter.isEmpty()) {
                fabgl::AnimationCondition condition;
                condition.parameter = parameter.toStdString();
                if (mode == QStringLiteral("boolean") || mode == QStringLiteral("booleanequals")) {
                    condition.mode = fabgl::AnimationConditionMode::BooleanEquals;
                    condition.booleanValue = textBoolean(expected, true);
                } else if (mode == QStringLiteral("integerequals")) {
                    condition.mode = fabgl::AnimationConditionMode::IntegerEquals;
                    condition.integerValue = expected.toLongLong();
                } else if (mode == QStringLiteral("integernotequals")) {
                    condition.mode = fabgl::AnimationConditionMode::IntegerNotEquals;
                    condition.integerValue = expected.toLongLong();
                } else if (mode == QStringLiteral("integergreater")) {
                    condition.mode = fabgl::AnimationConditionMode::IntegerGreater;
                    condition.integerValue = expected.toLongLong();
                } else if (mode == QStringLiteral("integerless")) {
                    condition.mode = fabgl::AnimationConditionMode::IntegerLess;
                    condition.integerValue = expected.toLongLong();
                } else if (mode == QStringLiteral("floatgreater")) {
                    condition.mode = fabgl::AnimationConditionMode::FloatGreater;
                    condition.floatValue = expected.toFloat();
                } else if (mode == QStringLiteral("floatless")) {
                    condition.mode = fabgl::AnimationConditionMode::FloatLess;
                    condition.floatValue = expected.toFloat();
                } else if (mode == QStringLiteral("trigger") || mode == QStringLiteral("triggerset")) {
                    condition.mode = fabgl::AnimationConditionMode::TriggerSet;
                } else {
                    error = tr("Transition row %1 uses an unknown condition mode.").arg(row + 1);
                    break;
                }
                transition.conditions.push_back(std::move(condition));
            }
            auto result = controller.addTransition(std::move(transition));
            if (!result) {
                error = tr("Transition row %1: %2")
                            .arg(row + 1)
                            .arg(animationError(result.error()));
                break;
            }
        }
    }

    QString previewState;
    if (error.isEmpty()) {
        int stateRow = m_states->currentRow();
        if (stateRow < 0 && m_states->rowCount() > 0) {
            stateRow = 0;
        }
        if (stateRow < 0) {
            error = tr("The controller needs at least one state.");
        } else {
            previewState = tableText(m_states, stateRow, 0);
            auto playResult = controller.play(previewState.toStdString());
            if (!playResult) {
                error = animationError(playResult.error());
            } else {
                auto frame = controller.update(1.0F / 60.0F);
                if (!frame) {
                    error = animationError(frame.error());
                } else {
                    previewState = QString::fromStdString(frame.value().state);
                }
            }
        }
    }

    if (error.isEmpty()) {
        m_status->setText(tr("Valid controller — %1 state(s), %2 parameter(s), %3 transition(s); "
                             "preview: %4")
                              .arg(stateCount())
                              .arg(parameterCount())
                              .arg(transitionCount())
                              .arg(previewState));
        m_status->setStyleSheet(QStringLiteral("color: #65d46e;"));
    } else {
        m_status->setText(tr("Invalid controller — %1").arg(error));
        m_status->setStyleSheet(QStringLiteral("color: #ff6b6b;"));
    }
    emit statusMessage(m_status->text());
#endif
}

bool AnimatorEditorWidget::collectControllerFromTables(QString& errorMessage) {
    fabgl::AnimatorControllerAsset asset;
    const auto parsedGuid =
        fabgl::AssetGuid::parse(m_controllerGuid->text().trimmed().toStdString());
    if (!parsedGuid || parsedGuid.value().isNil()) {
        errorMessage = tr("Controller GUID is invalid.");
        return false;
    }
    asset.guid = parsedGuid.value();
    asset.name = m_controllerName->text().trimmed().toStdString();
    asset.initialState = m_initialState->text().trimmed().toStdString();
    if (asset.name.empty()) {
        errorMessage = tr("Controller name cannot be empty.");
        return false;
    }

    for (int row = 0; row < m_states->rowCount(); ++row) {
        const QString name = tableText(m_states, row, 0);
        bool durationOk = false;
        const float duration = tableText(m_states, row, 1).toFloat(&durationOk);
        const bool looping = textBoolean(tableText(m_states, row, 2));
        const auto clipGuid = fabgl::AssetGuid::parse(tableText(m_states, row, 3).toStdString());
        if (name.isEmpty() || !durationOk || duration <= 0.0F || !std::isfinite(duration) ||
            !clipGuid || clipGuid.value().isNil()) {
            errorMessage =
                tr("State row %1 has an invalid name, duration, or clip GUID.").arg(row + 1);
            return false;
        }
        if (!asset.states
                 .emplace(name.toStdString(), fabgl::AnimatorStateDefinition{clipGuid.value()})
                 .second) {
            errorMessage = tr("State row %1 duplicates a state name.").arg(row + 1);
            return false;
        }
        if (auto clip = m_clipLibrary.find(clipGuid.value()); clip != m_clipLibrary.end()) {
            clip->second.name = name.toStdString();
            clip->second.durationSeconds = duration;
            clip->second.looping = looping;
        }
    }

    for (int row = 0; row < m_parameters->rowCount(); ++row) {
        const QString name = tableText(m_parameters, row, 0);
        const QString type = tableText(m_parameters, row, 1).toLower();
        const QString value = tableText(m_parameters, row, 2);
        fabgl::AnimatorParameterDefinition definition;
        bool valueOk = true;
        if (type == QStringLiteral("boolean") || type == QStringLiteral("bool")) {
            definition.type = fabgl::AnimatorParameterType::Boolean;
            definition.booleanDefault = textBoolean(value);
        } else if (type == QStringLiteral("integer") || type == QStringLiteral("int")) {
            definition.type = fabgl::AnimatorParameterType::Integer;
            definition.integerDefault = value.toLongLong(&valueOk);
        } else if (type == QStringLiteral("float") || type == QStringLiteral("number")) {
            definition.type = fabgl::AnimatorParameterType::Float;
            definition.floatDefault = value.toFloat(&valueOk);
            valueOk = valueOk && std::isfinite(definition.floatDefault);
        } else if (type == QStringLiteral("trigger")) {
            definition.type = fabgl::AnimatorParameterType::Trigger;
        } else {
            valueOk = false;
        }
        if (name.isEmpty() || !valueOk ||
            !asset.parameters.emplace(name.toStdString(), definition).second) {
            errorMessage = tr("Parameter row %1 is invalid or duplicated.").arg(row + 1);
            return false;
        }
    }

    std::map<qlonglong, std::size_t> transitionGroups;
    qlonglong nextSyntheticGroup = -1;
    for (int row = 0; row < m_transitions->rowCount(); ++row) {
        fabgl::AnimatorTransitionDefinition transition;
        transition.fromState = tableText(m_transitions, row, 0).toStdString();
        transition.toState = tableText(m_transitions, row, 1).toStdString();
        const QString parameter = tableText(m_transitions, row, 2);
        const QString mode = tableText(m_transitions, row, 3).toLower();
        const QString expected = tableText(m_transitions, row, 4);
        bool minimumOk = false;
        transition.minimumNormalizedTime = tableText(m_transitions, row, 5).toFloat(&minimumOk);
        bool exitOk = false;
        const float exitTime = tableText(m_transitions, row, 6).toFloat(&exitOk);
        bool blendOk = false;
        transition.blendDurationSeconds = tableText(m_transitions, row, 7).toFloat(&blendOk);
        if (!minimumOk || !exitOk || !blendOk || transition.minimumNormalizedTime < 0.0F ||
            transition.blendDurationSeconds < 0.0F || !std::isfinite(exitTime)) {
            errorMessage = tr("Transition row %1 has invalid timing values.").arg(row + 1);
            return false;
        }
        transition.hasExitTime = exitTime >= 0.0F;
        transition.exitTime = std::max(0.0F, exitTime);
        if (!parameter.isEmpty()) {
            fabgl::AnimationCondition condition;
            condition.parameter = parameter.toStdString();
            bool expectedOk = true;
            if (mode == QStringLiteral("boolean") || mode == QStringLiteral("booleanequals")) {
                condition.mode = fabgl::AnimationConditionMode::BooleanEquals;
                condition.booleanValue = textBoolean(expected, true);
            } else if (mode == QStringLiteral("integerequals")) {
                condition.mode = fabgl::AnimationConditionMode::IntegerEquals;
                condition.integerValue = expected.toLongLong(&expectedOk);
            } else if (mode == QStringLiteral("integernotequals")) {
                condition.mode = fabgl::AnimationConditionMode::IntegerNotEquals;
                condition.integerValue = expected.toLongLong(&expectedOk);
            } else if (mode == QStringLiteral("integergreater")) {
                condition.mode = fabgl::AnimationConditionMode::IntegerGreater;
                condition.integerValue = expected.toLongLong(&expectedOk);
            } else if (mode == QStringLiteral("integerless")) {
                condition.mode = fabgl::AnimationConditionMode::IntegerLess;
                condition.integerValue = expected.toLongLong(&expectedOk);
            } else if (mode == QStringLiteral("floatgreater")) {
                condition.mode = fabgl::AnimationConditionMode::FloatGreater;
                condition.floatValue = expected.toFloat(&expectedOk);
            } else if (mode == QStringLiteral("floatless")) {
                condition.mode = fabgl::AnimationConditionMode::FloatLess;
                condition.floatValue = expected.toFloat(&expectedOk);
            } else if (mode == QStringLiteral("trigger") || mode == QStringLiteral("triggerset")) {
                condition.mode = fabgl::AnimationConditionMode::TriggerSet;
            } else {
                expectedOk = false;
            }
            if (!expectedOk || !std::isfinite(condition.floatValue)) {
                errorMessage = tr("Transition row %1 has an invalid condition.").arg(row + 1);
                return false;
            }
            transition.conditions.push_back(std::move(condition));
        }
        const QVariant groupData = m_transitions->item(row, 0) != nullptr
                                       ? m_transitions->item(row, 0)->data(Qt::UserRole)
                                       : QVariant{};
        const qlonglong group = groupData.isValid() ? groupData.toLongLong() : nextSyntheticGroup--;
        if (const auto existing = transitionGroups.find(group);
            existing != transitionGroups.end()) {
            auto& grouped = asset.transitions[existing->second];
            if (grouped.fromState != transition.fromState ||
                grouped.toState != transition.toState ||
                grouped.minimumNormalizedTime != transition.minimumNormalizedTime ||
                grouped.hasExitTime != transition.hasExitTime ||
                grouped.exitTime != transition.exitTime ||
                grouped.blendDurationSeconds != transition.blendDurationSeconds) {
                errorMessage =
                    tr("Transition row %1 disagrees with its grouped conditions.").arg(row + 1);
                return false;
            }
            grouped.conditions.insert(grouped.conditions.end(),
                                      std::make_move_iterator(transition.conditions.begin()),
                                      std::make_move_iterator(transition.conditions.end()));
        } else {
            transitionGroups.emplace(group, asset.transitions.size());
            asset.transitions.push_back(std::move(transition));
        }
    }

    auto validated = fabgl::validateAnimatorControllerAsset(asset);
    if (!validated) {
        errorMessage = animationError(validated.error());
        return false;
    }
    m_controllerAsset = std::move(asset);
    errorMessage.clear();
    return true;
}

bool AnimatorEditorWidget::collectClipFromTables(QString& errorMessage) {
    fabgl::AnimationClipAsset asset;
    const auto parsedGuid = fabgl::AssetGuid::parse(m_clipGuid->text().trimmed().toStdString());
    if (!parsedGuid || parsedGuid.value().isNil()) {
        errorMessage = tr("Animation clip GUID is invalid.");
        return false;
    }
    asset.guid = parsedGuid.value();
    asset.name = m_clipName->text().trimmed().toStdString();
    asset.durationSeconds = static_cast<float>(m_clipDuration->value());
    asset.looping = m_clipLooping->isChecked();
    if (asset.name.empty()) {
        errorMessage = tr("Animation clip name cannot be empty.");
        return false;
    }
    for (int row = 0; row < m_clipKeys->rowCount(); ++row) {
        const QString property = tableText(m_clipKeys, row, 0);
        bool timeOk = false;
        bool valueOk = false;
        bool inOk = false;
        bool outOk = false;
        fabgl::AnimationKey key;
        key.time = tableText(m_clipKeys, row, 1).toFloat(&timeOk);
        key.value = tableText(m_clipKeys, row, 2).toFloat(&valueOk);
        key.inTangent = tableText(m_clipKeys, row, 3).toFloat(&inOk);
        key.outTangent = tableText(m_clipKeys, row, 4).toFloat(&outOk);
        const QString interpolation = tableText(m_clipKeys, row, 5).toLower();
        if (interpolation == QStringLiteral("step"))
            key.interpolation = fabgl::CurveInterpolation::Step;
        else if (interpolation == QStringLiteral("linear"))
            key.interpolation = fabgl::CurveInterpolation::Linear;
        else if (interpolation == QStringLiteral("cubic") ||
                 interpolation == QStringLiteral("cubichermite"))
            key.interpolation = fabgl::CurveInterpolation::CubicHermite;
        else
            timeOk = false;
        if (property.isEmpty() || !timeOk || !valueOk || !inOk || !outOk) {
            errorMessage = tr("Curve key row %1 is invalid.").arg(row + 1);
            return false;
        }
        auto& curve = asset.tracks[property.toStdString()];
        auto added = curve.addKey(key);
        if (!added) {
            errorMessage =
                tr("Curve key row %1: %2").arg(row + 1).arg(animationError(added.error()));
            return false;
        }
    }
    for (int row = 0; row < m_clipEvents->rowCount(); ++row) {
        bool timeOk = false;
        fabgl::AnimationEvent event;
        event.time = tableText(m_clipEvents, row, 0).toFloat(&timeOk);
        event.name = tableText(m_clipEvents, row, 1).toStdString();
        if (!timeOk || event.name.empty()) {
            errorMessage = tr("Animation event row %1 is invalid.").arg(row + 1);
            return false;
        }
        asset.events.push_back(std::move(event));
    }
    auto validated = fabgl::validateAnimationClipAsset(asset);
    if (!validated) {
        errorMessage = animationError(validated.error());
        return false;
    }
    m_clipAsset = std::move(asset);
    m_clipLibrary[m_clipAsset.guid] = m_clipAsset;
    errorMessage.clear();
    return true;
}

void AnimatorEditorWidget::updatePreview() {
    if (m_preview == nullptr || m_timeline == nullptr)
        return;
    const float normalized = static_cast<float>(m_timeline->value()) /
                             static_cast<float>(std::max(1, m_timeline->maximum()));
    if (m_assetMode == AssetMode::AnimationClip) {
        auto runtime = fabgl::buildAnimationClip(m_clipAsset);
        if (!runtime) {
            m_preview->setText(tr("Preview unavailable"));
            return;
        }
        const float time = normalized * m_clipAsset.durationSeconds;
        const auto values = runtime.value()->sample(time);
        QStringList samples;
        for (const auto& [property, value] : values) {
            samples.push_back(QStringLiteral("%1=%2")
                                  .arg(QString::fromStdString(property))
                                  .arg(value, 0, 'f', 3));
        }
        m_preview->setText(
            tr("%1 s / %2 s%3")
                .arg(time, 0, 'f', 3)
                .arg(m_clipAsset.durationSeconds, 0, 'f', 3)
                .arg(samples.isEmpty() ? QString{} : QStringLiteral(" - ") + samples.join(", ")));
        return;
    }
    const fabgl::AnimationClipResolver resolver = [this](const fabgl::AssetGuid guid) {
        const auto found = m_clipLibrary.find(guid);
        if (found == m_clipLibrary.end()) {
            return fabgl::Result<std::shared_ptr<const fabgl::AnimationClip>>::failure(
                fabgl::Error(fabgl::ErrorCode::NotFound, "animation clip is not loaded"));
        }
        return fabgl::buildAnimationClip(found->second);
    };
    auto runtime = fabgl::buildAnimatorController(m_controllerAsset, resolver);
    if (!runtime) {
        m_preview->setText(tr("Preview unavailable: %1").arg(animationError(runtime.error())));
        return;
    }
    int row = m_states->currentRow();
    if (row < 0 && m_states->rowCount() > 0)
        row = 0;
    if (row >= 0) {
        auto played = runtime.value()->play(tableText(m_states, row, 0).toStdString());
        if (!played) {
            m_preview->setText(tr("Preview unavailable: %1").arg(animationError(played.error())));
            return;
        }
    }
    float duration = 1.0F;
    if (row >= 0) {
        bool ok = false;
        duration = tableText(m_states, row, 1).toFloat(&ok);
        if (!ok || duration <= 0.0F)
            duration = 1.0F;
    }
    auto frame = runtime.value()->update(normalized * duration);
    if (!frame) {
        m_preview->setText(tr("Preview unavailable: %1").arg(animationError(frame.error())));
        return;
    }
    m_preview->setText(tr("State %1 - %2 s - %3 value(s)")
                           .arg(QString::fromStdString(frame.value().state))
                           .arg(frame.value().localTime, 0, 'f', 3)
                           .arg(frame.value().values.size()));
}

void AnimatorEditorWidget::refreshControllerTables() {
    m_updating = true;
    m_controllerName->setText(QString::fromStdString(m_controllerAsset.name));
    m_controllerGuid->setText(QString::fromStdString(m_controllerAsset.guid.toString()));
    m_initialState->setText(QString::fromStdString(m_controllerAsset.initialState));
    m_states->setRowCount(0);
    for (const auto& [name, state] : m_controllerAsset.states) {
        const int row = m_states->rowCount();
        m_states->insertRow(row);
        float duration = 1.0F;
        bool looping = true;
        if (const auto clip = m_clipLibrary.find(state.clip); clip != m_clipLibrary.end()) {
            duration = clip->second.durationSeconds;
            looping = clip->second.looping;
        }
        const QStringList values = {QString::fromStdString(name), QString::number(duration, 'g', 9),
                                    looping ? QStringLiteral("true") : QStringLiteral("false"),
                                    QString::fromStdString(state.clip.toString())};
        for (int column = 0; column < values.size(); ++column)
            m_states->setItem(row, column, editableItem(values.at(column)));
    }
    if (m_states->rowCount() > 0)
        m_states->setCurrentCell(0, 0);

    m_parameters->setRowCount(0);
    for (const auto& [name, parameter] : m_controllerAsset.parameters) {
        QString type;
        QString value;
        switch (parameter.type) {
        case fabgl::AnimatorParameterType::Boolean:
            type = QStringLiteral("Boolean");
            value = parameter.booleanDefault ? QStringLiteral("true") : QStringLiteral("false");
            break;
        case fabgl::AnimatorParameterType::Integer:
            type = QStringLiteral("Integer");
            value = QString::number(parameter.integerDefault);
            break;
        case fabgl::AnimatorParameterType::Float:
            type = QStringLiteral("Float");
            value = QString::number(parameter.floatDefault, 'g', 9);
            break;
        case fabgl::AnimatorParameterType::Trigger:
            type = QStringLiteral("Trigger");
            value.clear();
            break;
        }
        const int row = m_parameters->rowCount();
        m_parameters->insertRow(row);
        m_parameters->setItem(row, 0, editableItem(QString::fromStdString(name)));
        m_parameters->setItem(row, 1, editableItem(type));
        m_parameters->setItem(row, 2, editableItem(value));
    }

    m_transitions->setRowCount(0);
    for (std::size_t transitionIndex = 0; transitionIndex < m_controllerAsset.transitions.size();
         ++transitionIndex) {
        const auto& transition = m_controllerAsset.transitions[transitionIndex];
        const std::size_t rows = std::max<std::size_t>(1U, transition.conditions.size());
        for (std::size_t conditionIndex = 0; conditionIndex < rows; ++conditionIndex) {
            QString parameter;
            QString mode;
            QString expected;
            if (conditionIndex < transition.conditions.size()) {
                const auto& condition = transition.conditions[conditionIndex];
                parameter = QString::fromStdString(condition.parameter);
                switch (condition.mode) {
                case fabgl::AnimationConditionMode::BooleanEquals:
                    mode = QStringLiteral("BooleanEquals");
                    expected =
                        condition.booleanValue ? QStringLiteral("true") : QStringLiteral("false");
                    break;
                case fabgl::AnimationConditionMode::IntegerEquals:
                    mode = QStringLiteral("IntegerEquals");
                    expected = QString::number(condition.integerValue);
                    break;
                case fabgl::AnimationConditionMode::IntegerNotEquals:
                    mode = QStringLiteral("IntegerNotEquals");
                    expected = QString::number(condition.integerValue);
                    break;
                case fabgl::AnimationConditionMode::IntegerGreater:
                    mode = QStringLiteral("IntegerGreater");
                    expected = QString::number(condition.integerValue);
                    break;
                case fabgl::AnimationConditionMode::IntegerLess:
                    mode = QStringLiteral("IntegerLess");
                    expected = QString::number(condition.integerValue);
                    break;
                case fabgl::AnimationConditionMode::FloatGreater:
                    mode = QStringLiteral("FloatGreater");
                    expected = QString::number(condition.floatValue, 'g', 9);
                    break;
                case fabgl::AnimationConditionMode::FloatLess:
                    mode = QStringLiteral("FloatLess");
                    expected = QString::number(condition.floatValue, 'g', 9);
                    break;
                case fabgl::AnimationConditionMode::TriggerSet:
                    mode = QStringLiteral("TriggerSet");
                    break;
                }
            }
            const int row = m_transitions->rowCount();
            m_transitions->insertRow(row);
            const QStringList values = {QString::fromStdString(transition.fromState),
                                        QString::fromStdString(transition.toState),
                                        parameter,
                                        mode,
                                        expected,
                                        QString::number(transition.minimumNormalizedTime, 'g', 9),
                                        transition.hasExitTime
                                            ? QString::number(transition.exitTime, 'g', 9)
                                            : QStringLiteral("-1"),
                                        QString::number(transition.blendDurationSeconds, 'g', 9)};
            for (int column = 0; column < values.size(); ++column)
                m_transitions->setItem(row, column, editableItem(values.at(column)));
            m_transitions->item(row, 0)->setData(Qt::UserRole,
                                                 static_cast<qulonglong>(transitionIndex));
        }
    }
    m_updating = false;
}

void AnimatorEditorWidget::refreshClipTables() {
    m_updating = true;
    m_clipName->setText(QString::fromStdString(m_clipAsset.name));
    m_clipGuid->setText(QString::fromStdString(m_clipAsset.guid.toString()));
    m_clipDuration->setValue(m_clipAsset.durationSeconds);
    m_clipLooping->setChecked(m_clipAsset.looping);
    m_clipKeys->setRowCount(0);
    for (const auto& [property, curve] : m_clipAsset.tracks) {
        for (const auto& key : curve.keys()) {
            QString interpolation = QStringLiteral("Linear");
            if (key.interpolation == fabgl::CurveInterpolation::Step)
                interpolation = QStringLiteral("Step");
            else if (key.interpolation == fabgl::CurveInterpolation::CubicHermite)
                interpolation = QStringLiteral("CubicHermite");
            const int row = m_clipKeys->rowCount();
            m_clipKeys->insertRow(row);
            const QStringList values = {
                QString::fromStdString(property),        QString::number(key.time, 'g', 9),
                QString::number(key.value, 'g', 9),      QString::number(key.inTangent, 'g', 9),
                QString::number(key.outTangent, 'g', 9), interpolation};
            for (int column = 0; column < values.size(); ++column)
                m_clipKeys->setItem(row, column, editableItem(values.at(column)));
        }
    }
    m_clipEvents->setRowCount(0);
    for (const auto& event : m_clipAsset.events) {
        const int row = m_clipEvents->rowCount();
        m_clipEvents->insertRow(row);
        m_clipEvents->setItem(row, 0, editableItem(QString::number(event.time, 'g', 9)));
        m_clipEvents->setItem(row, 1, editableItem(QString::fromStdString(event.name)));
    }
    m_updating = false;
}

void AnimatorEditorWidget::setAssetModified(const bool modified) {
    m_assetModified = modified;
    updateAssetStatus();
}

void AnimatorEditorWidget::updateAssetStatus() {
    if (m_assetStatus == nullptr)
        return;
    const QString fallback = m_assetMode == AssetMode::Controller ? tr("Untitled.fglcontroller")
                                                                  : tr("Untitled.fglanim");
    const QString display =
        m_assetFilePath.isEmpty() ? fallback : QDir::toNativeSeparators(m_assetFilePath);
    m_assetStatus->setText(display + (m_assetModified ? QStringLiteral(" *") : QString{}));
    m_assetStatus->setToolTip(display);
}

void AnimatorEditorWidget::newController() {
    m_previewTimer->stop();
    m_previewPlayButton->setText(tr("Play"));
    m_assetMode = AssetMode::Controller;
    m_assetPages->setCurrentIndex(0);
    m_assetFilePath.clear();
    m_controllerAsset = {};
    m_controllerAsset.guid = fabgl::AssetGuid::generate();
    m_controllerAsset.name = "Untitled Controller";
    m_controllerAsset.initialState = "Idle";
    m_clipLibrary.clear();
    createInitialController();
    refreshControllerTables();
    m_timeline->setValue(0);
    setAssetModified(false);
    validateController();
    resetAnimatorHistory();
    emit statusMessage(tr("Created a new animator controller."));
}

void AnimatorEditorWidget::newAnimationClip() {
    m_previewTimer->stop();
    m_previewPlayButton->setText(tr("Play"));
    m_assetMode = AssetMode::AnimationClip;
    m_assetPages->setCurrentIndex(1);
    m_assetFilePath.clear();
    m_clipAsset = {};
    m_clipAsset.guid = fabgl::AssetGuid::generate();
    m_clipAsset.name = "Untitled Animation";
    m_clipAsset.durationSeconds = 1.0F;
    m_clipAsset.looping = true;
    fabgl::AnimationCurve curve;
    auto first = curve.addKey({0.0F, 0.0F});
    auto last = curve.addKey({1.0F, 1.0F});
    if (first && last)
        m_clipAsset.tracks.emplace("transform.position.x", std::move(curve));
    m_clipLibrary[m_clipAsset.guid] = m_clipAsset;
    refreshClipTables();
    m_timeline->setValue(0);
    setAssetModified(false);
    validateController();
    resetAnimatorHistory();
    emit statusMessage(tr("Created a new animation clip."));
}

void AnimatorEditorWidget::loadClipLibraryAround(const QString& filePath) {
    const QDir root(QFileInfo(filePath).absolutePath());
    QDirIterator iterator(root.absolutePath(), {QStringLiteral("*.fglanim")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        QFile file(iterator.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        auto clip = fabgl::deserializeAnimationClipAsset(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (clip)
            m_clipLibrary[clip.value().guid] = std::move(clip.value());
    }
}

bool AnimatorEditorWidget::openAssetFile(const QString& filePath, QString& errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = tr("Cannot open animator asset %1: %2")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    const auto view = std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    m_previewTimer->stop();
    m_previewPlayButton->setText(tr("Play"));
    if (view.starts_with("fglcontroller ")) {
        auto loaded = fabgl::deserializeAnimatorControllerAsset(view);
        if (!loaded) {
            errorMessage = animationError(loaded.error());
            return false;
        }
        m_assetMode = AssetMode::Controller;
        m_assetPages->setCurrentIndex(0);
        m_controllerAsset = std::move(loaded.value());
        m_clipLibrary.clear();
        loadClipLibraryAround(filePath);
        refreshControllerTables();
    } else if (view.starts_with("fglanim ")) {
        auto loaded = fabgl::deserializeAnimationClipAsset(view);
        if (!loaded) {
            errorMessage = animationError(loaded.error());
            return false;
        }
        m_assetMode = AssetMode::AnimationClip;
        m_assetPages->setCurrentIndex(1);
        m_clipAsset = std::move(loaded.value());
        m_clipLibrary.clear();
        loadClipLibraryAround(filePath);
        m_clipLibrary[m_clipAsset.guid] = m_clipAsset;
        refreshClipTables();
    } else {
        errorMessage = tr("File is neither a .fglcontroller nor a .fglanim v1 asset.");
        return false;
    }
    m_assetFilePath = QFileInfo(filePath).absoluteFilePath();
    m_timeline->setValue(0);
    setAssetModified(false);
    validateController();
    resetAnimatorHistory();
    errorMessage.clear();
    emit statusMessage(
        tr("Opened animator asset %1").arg(QDir::toNativeSeparators(m_assetFilePath)));
    return true;
}

bool AnimatorEditorWidget::saveAssetFile(const QString& filePath, QString& errorMessage) {
    const QString destination = filePath.trimmed().isEmpty() ? m_assetFilePath : filePath;
    if (destination.isEmpty()) {
        errorMessage = tr("Choose an animator asset destination before saving.");
        return false;
    }
    std::string source;
    if (m_assetMode == AssetMode::Controller) {
        if (!collectControllerFromTables(errorMessage))
            return false;
        auto serialized = fabgl::serializeAnimatorControllerAsset(m_controllerAsset);
        if (!serialized) {
            errorMessage = animationError(serialized.error());
            return false;
        }
        source = std::move(serialized.value());
    } else {
        if (!collectClipFromTables(errorMessage))
            return false;
        auto serialized = fabgl::serializeAnimationClipAsset(m_clipAsset);
        if (!serialized) {
            errorMessage = animationError(serialized.error());
            return false;
        }
        source = std::move(serialized.value());
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        errorMessage = tr("Cannot create the animator asset destination directory.");
        return false;
    }
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = tr("Cannot write animator asset: %1").arg(file.errorString());
        return false;
    }
    const QByteArray bytes(source.data(), static_cast<qsizetype>(source.size()));
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        errorMessage = tr("Cannot atomically save animator asset: %1").arg(file.errorString());
        return false;
    }
    m_assetFilePath = QFileInfo(destination).absoluteFilePath();
    setAssetModified(false);
    resetAnimatorHistory();
    errorMessage.clear();
    emit statusMessage(
        tr("Saved animator asset %1").arg(QDir::toNativeSeparators(m_assetFilePath)));
    return true;
}

void AnimatorEditorWidget::openAssetDialog() {
    if (m_assetModified &&
        QMessageBox::question(this, tr("Discard Animator Changes"),
                              tr("Open another asset and discard unsaved changes?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Animator Asset"), QFileInfo(m_assetFilePath).absolutePath(),
        tr("FabGL Animator Assets (*.fglcontroller *.fglanim);;All Files (*)"));
    if (path.isEmpty())
        return;
    QString errorMessage;
    if (!openAssetFile(path, errorMessage))
        QMessageBox::critical(this, tr("Animator Open Failed"), errorMessage);
}

void AnimatorEditorWidget::saveAssetDialog(const bool saveAs) {
    QString path = saveAs ? QString{} : m_assetFilePath;
    const QString extension = m_assetMode == AssetMode::Controller
                                  ? QStringLiteral(".fglcontroller")
                                  : QStringLiteral(".fglanim");
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Animator Asset"),
            m_assetFilePath.isEmpty() ? QStringLiteral("Untitled") + extension : m_assetFilePath,
            tr("FabGL Animator Assets (*.fglcontroller *.fglanim);;All Files (*)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(extension, Qt::CaseInsensitive))
            path += extension;
    }
    QString errorMessage;
    if (!saveAssetFile(path, errorMessage))
        QMessageBox::critical(this, tr("Animator Save Failed"), errorMessage);
}

void AnimatorEditorWidget::addClipKey() {
    m_updating = true;
    const int row = m_clipKeys->rowCount();
    m_clipKeys->insertRow(row);
    const float time = static_cast<float>(m_timeline->value()) /
                       static_cast<float>(std::max(1, m_timeline->maximum())) *
                       static_cast<float>(m_clipDuration->value());
    const QStringList values = {QStringLiteral("transform.position.x"),
                                QString::number(time, 'g', 9),
                                QStringLiteral("0"),
                                QStringLiteral("0"),
                                QStringLiteral("0"),
                                QStringLiteral("Linear")};
    for (int column = 0; column < values.size(); ++column)
        m_clipKeys->setItem(row, column, editableItem(values.at(column)));
    m_clipKeys->setCurrentCell(row, 0);
    m_updating = false;
    setAssetModified(true);
    validateController();
    recordAnimatorEdit(tr("Add Animation Key"));
}

void AnimatorEditorWidget::removeSelectedClipKey() {
    if (m_clipKeys->currentRow() >= 0) {
        m_updating = true;
        m_clipKeys->removeRow(m_clipKeys->currentRow());
        m_updating = false;
        setAssetModified(true);
        validateController();
        recordAnimatorEdit(tr("Remove Animation Key"));
    }
}

void AnimatorEditorWidget::addClipEvent() {
    m_updating = true;
    const int row = m_clipEvents->rowCount();
    m_clipEvents->insertRow(row);
    const float time = static_cast<float>(m_timeline->value()) /
                       static_cast<float>(std::max(1, m_timeline->maximum())) *
                       static_cast<float>(m_clipDuration->value());
    m_clipEvents->setItem(row, 0, editableItem(QString::number(time, 'g', 9)));
    m_clipEvents->setItem(row, 1, editableItem(tr("event%1").arg(row + 1)));
    m_clipEvents->setCurrentCell(row, 0);
    m_updating = false;
    setAssetModified(true);
    validateController();
    recordAnimatorEdit(tr("Add Animation Event"));
}

void AnimatorEditorWidget::removeSelectedClipEvent() {
    if (m_clipEvents->currentRow() >= 0) {
        m_updating = true;
        m_clipEvents->removeRow(m_clipEvents->currentRow());
        m_updating = false;
        setAssetModified(true);
        validateController();
        recordAnimatorEdit(tr("Remove Animation Event"));
    }
}

void AnimatorEditorWidget::addState() {
    m_updating = true;
    const int row = m_states->rowCount();
    m_states->insertRow(row);
    const QString name = tr("State %1").arg(row + 1);
    const auto clipGuid = fabgl::AssetGuid::generate();
    m_states->setItem(row, 0, editableItem(name));
    m_states->setItem(row, 1, editableItem(QStringLiteral("1.0")));
    m_states->setItem(row, 2, editableItem(QStringLiteral("true")));
    m_states->setItem(row, 3, editableItem(QString::fromStdString(clipGuid.toString())));
    fabgl::AnimationClipAsset clip;
    clip.guid = clipGuid;
    clip.name = name.toStdString();
    clip.durationSeconds = 1.0F;
    clip.looping = true;
    m_clipLibrary[clip.guid] = std::move(clip);
    m_states->setCurrentCell(row, 0);
    m_updating = false;
    setAssetModified(true);
    validateController();
    recordAnimatorEdit(tr("Add Animator State"));
}

void AnimatorEditorWidget::removeSelectedState() {
    const int row = m_states->currentRow();
    if (row >= 0) {
        m_updating = true;
        m_states->removeRow(row);
        m_updating = false;
        setAssetModified(true);
        validateController();
        recordAnimatorEdit(tr("Remove Animator State"));
    }
}

void AnimatorEditorWidget::addParameter() {
    m_updating = true;
    const int row = m_parameters->rowCount();
    m_parameters->insertRow(row);
    m_parameters->setItem(row, 0, editableItem(tr("parameter%1").arg(row + 1)));
    m_parameters->setItem(row, 1, editableItem(QStringLiteral("Boolean")));
    m_parameters->setItem(row, 2, editableItem(QStringLiteral("false")));
    m_parameters->setCurrentCell(row, 0);
    m_updating = false;
    setAssetModified(true);
    validateController();
    recordAnimatorEdit(tr("Add Animator Parameter"));
}

void AnimatorEditorWidget::removeSelectedParameter() {
    const int row = m_parameters->currentRow();
    if (row >= 0) {
        m_updating = true;
        m_parameters->removeRow(row);
        m_updating = false;
        setAssetModified(true);
        validateController();
        recordAnimatorEdit(tr("Remove Animator Parameter"));
    }
}

void AnimatorEditorWidget::addTransition() {
    m_updating = true;
    const int row = m_transitions->rowCount();
    m_transitions->insertRow(row);
    const QString from =
        m_states->rowCount() > 0 ? tableText(m_states, 0, 0) : QStringLiteral("Idle");
    const QString to = m_states->rowCount() > 1 ? tableText(m_states, 1, 0) : from;
    const QString parameter =
        m_parameters->rowCount() > 0 ? tableText(m_parameters, 0, 0) : QStringLiteral("condition");
    const QStringList values = {from,
                                to,
                                parameter,
                                QStringLiteral("BooleanEquals"),
                                QStringLiteral("true"),
                                QStringLiteral("0.0"),
                                QStringLiteral("-1.0"),
                                QStringLiteral("0.1")};
    for (int column = 0; column < values.size(); ++column) {
        m_transitions->setItem(row, column, editableItem(values.at(column)));
    }
    m_transitions->setCurrentCell(row, 0);
    m_updating = false;
    setAssetModified(true);
    validateController();
    recordAnimatorEdit(tr("Add Animator Transition"));
}

void AnimatorEditorWidget::removeSelectedTransition() {
    const int row = m_transitions->currentRow();
    if (row >= 0) {
        m_updating = true;
        m_transitions->removeRow(row);
        m_updating = false;
        setAssetModified(true);
        validateController();
        recordAnimatorEdit(tr("Remove Animator Transition"));
    }
}

void AnimatorEditorWidget::createInitialController() {
    const std::string stableRoot = "fabgl.studio.animator." + m_controllerAsset.guid.toString();
    const auto idleGuid = fabgl::AssetGuid::fromStableName(stableRoot + ".idle");
    const auto runGuid = fabgl::AssetGuid::fromStableName(stableRoot + ".run");
    fabgl::AnimationClipAsset idle;
    idle.guid = idleGuid;
    idle.name = "Idle";
    idle.durationSeconds = 1.0F;
    idle.looping = true;
    fabgl::AnimationClipAsset run;
    run.guid = runGuid;
    run.name = "Run";
    run.durationSeconds = 0.6F;
    run.looping = true;
    m_clipLibrary[idleGuid] = std::move(idle);
    m_clipLibrary[runGuid] = std::move(run);
    m_controllerAsset.states.emplace("Idle", fabgl::AnimatorStateDefinition{idleGuid});
    m_controllerAsset.states.emplace("Run", fabgl::AnimatorStateDefinition{runGuid});
    m_controllerAsset.parameters.emplace(
        "moving",
        fabgl::AnimatorParameterDefinition{fabgl::AnimatorParameterType::Boolean, false, 0, 0.0F});
    fabgl::AnimatorTransitionDefinition transition;
    transition.fromState = "Idle";
    transition.toState = "Run";
    transition.blendDurationSeconds = 0.1F;
    transition.conditions.push_back(
        {"moving", fabgl::AnimationConditionMode::BooleanEquals, true, 0, 0.0F});
    m_controllerAsset.transitions.push_back(std::move(transition));
}

MemoryAnalyzerWidget::MemoryAnalyzerWidget(SceneDocument* document, QWidget* parent)
    : QWidget(parent), m_document(document) {
    setObjectName(QStringLiteral("memoryAnalyzer"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* budgets = new QGroupBox(tr("Budgets"), this);
    auto* budgetLayout = new QFormLayout(budgets);
    const auto populateProfiles = [this, budgets](const QString& objectName) {
        auto* combo = new QComboBox(budgets);
        combo->setObjectName(objectName);
        combo->addItem(tr("Safe"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Safe));
        combo->addItem(tr("Balanced"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Balanced));
        combo->addItem(tr("Maximum"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Maximum));
        combo->addItem(tr("Custom"),
                       static_cast<int>(fabgl::project::PerformanceBudgetProfile::Custom));
        return combo;
    };
    m_pcProfile = populateProfiles(QStringLiteral("pcPerformanceProfileCombo"));
    m_esp32Profile = populateProfiles(QStringLiteral("esp32PerformanceProfileCombo"));
    m_pcProfile->setCurrentIndex(
        m_pcProfile->findData(static_cast<int>(m_performance.pcProfile)));
    m_esp32Profile->setCurrentIndex(
        m_esp32Profile->findData(static_cast<int>(m_performance.esp32Profile)));
    m_pcBudgetBar = new QProgressBar(budgets);
    m_pcBudgetBar->setObjectName(QStringLiteral("pcMemoryBudgetBar"));
    m_pcBudgetBar->setRange(0, 100);
    m_esp32BudgetBar = new QProgressBar(budgets);
    m_esp32BudgetBar->setObjectName(QStringLiteral("esp32MemoryBudgetBar"));
    m_esp32BudgetBar->setRange(0, 100);
    budgetLayout->addRow(tr("PC profile"), m_pcProfile);
    budgetLayout->addRow(tr("PC resident usage"), m_pcBudgetBar);
    budgetLayout->addRow(tr("ESP32 profile"), m_esp32Profile);
    budgetLayout->addRow(tr("ESP32 internal RAM usage"), m_esp32BudgetBar);
    layout->addWidget(budgets);

    m_summary = new QTableWidget(10, 4, this);
    m_summary->setObjectName(QStringLiteral("memorySummaryTable"));
    m_summary->setHorizontalHeaderLabels({tr("Category"), tr("Count"), tr("Bytes"), tr("Basis")});
    m_summary->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summary->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_summary, 1);
    auto* refreshButton = new QPushButton(tr("Refresh Analysis"), this);
    refreshButton->setObjectName(QStringLiteral("memoryRefreshButton"));
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("memoryAnalysisStatus"));
    m_status->setWordWrap(true);
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(refreshButton);
    layout->addLayout(statusRow);

    connect(refreshButton, &QPushButton::clicked, this, &MemoryAnalyzerWidget::refresh);
    const auto profileChanged = [this](int) {
        m_performance.pcProfile = static_cast<fabgl::project::PerformanceBudgetProfile>(
            m_pcProfile->currentData().toInt());
        m_performance.esp32Profile = static_cast<fabgl::project::PerformanceBudgetProfile>(
            m_esp32Profile->currentData().toInt());
        emit performanceProfilesChanged(static_cast<int>(m_performance.pcProfile),
                                        static_cast<int>(m_performance.esp32Profile));
        refresh();
    };
    connect(m_pcProfile, &QComboBox::currentIndexChanged, this, profileChanged);
    connect(m_esp32Profile, &QComboBox::currentIndexChanged, this, profileChanged);
    if (m_document != nullptr) {
        connect(m_document, &SceneDocument::sceneReset, this, &MemoryAnalyzerWidget::refresh);
        connect(m_document, &SceneDocument::entityAdded, this,
                [this](const QString&) { refresh(); });
        connect(m_document, &SceneDocument::entityRemoved, this,
                [this](const QString&) { refresh(); });
        connect(m_document, &SceneDocument::entityChanged, this,
                [this](const QString&) { refresh(); });
    }
    refresh();
}

void MemoryAnalyzerWidget::setProjectRoot(const QString& projectRoot) {
    m_projectRoot = projectRoot.trimmed().isEmpty() ? QString{} : QDir::cleanPath(projectRoot);
    refresh();
}

void MemoryAnalyzerWidget::setPerformanceBudgets(
    const fabgl::project::PerformanceBudgetSettings& settings) {
    m_performance = settings;
    const QSignalBlocker blockPc(m_pcProfile);
    const QSignalBlocker blockEsp32(m_esp32Profile);
    m_pcProfile->setCurrentIndex(
        m_pcProfile->findData(static_cast<int>(m_performance.pcProfile)));
    m_esp32Profile->setCurrentIndex(
        m_esp32Profile->findData(static_cast<int>(m_performance.esp32Profile)));
    refresh();
}

void MemoryAnalyzerWidget::setEsp32StorageEstimates(const std::uint64_t flashBytes,
                                                     const std::uint64_t internalRamBytes,
                                                     const std::uint64_t psramBytes,
                                                     const std::uint64_t sdBytes) {
    if (m_flashEstimate == flashBytes && m_internalRamEstimate == internalRamBytes &&
        m_psramEstimate == psramBytes && m_sdEstimate == sdBytes) {
        return;
    }
    m_flashEstimate = flashBytes;
    m_internalRamEstimate = internalRamBytes;
    m_psramEstimate = psramBytes;
    m_sdEstimate = sdBytes;
    refresh();
}

void MemoryAnalyzerWidget::setMeasuredPcResidentBytes(
    const std::optional<std::uint64_t> bytes) {
    if (m_measuredPcResidentBytes == bytes)
        return;
    m_measuredPcResidentBytes = bytes;
    refresh();
}

std::size_t MemoryAnalyzerWidget::entityCount() const noexcept {
    return m_entityCount;
}

std::size_t MemoryAnalyzerWidget::componentCount() const noexcept {
    return m_componentCount;
}

std::size_t MemoryAnalyzerWidget::assetCount() const noexcept {
    return m_assetCount;
}

std::size_t MemoryAnalyzerWidget::assetBytes() const noexcept {
    return m_assetBytes;
}

std::size_t MemoryAnalyzerWidget::estimatedRuntimeBytes() const noexcept {
    return m_estimatedRuntimeBytes;
}

void MemoryAnalyzerWidget::refresh() {
    m_entityCount = 0;
    m_componentCount = 0;
    m_assetCount = 0;
    m_assetBytes = 0;
    std::size_t sceneBytes = 0;
    if (m_document != nullptr) {
        const auto entities = m_document->scene().entities();
        m_entityCount = entities.size();
        for (const auto* entity : entities) {
            if (entity != nullptr) {
                m_componentCount += entity->components().size();
            }
        }
        QString serializationError;
        const QByteArray serializedScene = m_document->serialized(serializationError);
        if (serializationError.isEmpty()) {
            sceneBytes = static_cast<std::size_t>(serializedScene.size());
        }
    }

    QString assetsPath;
    if (!m_projectRoot.isEmpty() && QDir(m_projectRoot).exists()) {
        const QString upper = QDir(m_projectRoot).filePath(QStringLiteral("Assets"));
        const QString lower = QDir(m_projectRoot).filePath(QStringLiteral("assets"));
        assetsPath = QDir(upper).exists() ? upper : (QDir(lower).exists() ? lower : QString{});
    }
    if (!assetsPath.isEmpty()) {
        QDirIterator iterator(assetsPath, QDir::Files | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QFileInfo info(iterator.next());
            ++m_assetCount;
            m_assetBytes += static_cast<std::size_t>(std::max<qint64>(0, info.size()));
        }
    }

    constexpr std::size_t EstimatedEntityBytes = 256;
    constexpr std::size_t EstimatedComponentBytes = 192;
    const std::size_t entityBytes = m_entityCount * EstimatedEntityBytes;
    const std::size_t componentBytes = m_componentCount * EstimatedComponentBytes;
    m_estimatedRuntimeBytes = m_assetBytes + sceneBytes + entityBytes + componentBytes;

    const std::uint64_t estimatedEsp32Resident = m_internalRamEstimate + m_psramEstimate;
    const std::array<std::array<QString, 4>, 10> rows = {
        std::array{tr("Source assets"), QString::number(m_assetCount),
                   formattedBytes(m_assetBytes),
                   assetsPath.isEmpty() ? tr("Unavailable — no Assets directory")
                                        : tr("Measured file bytes (not resident RAM)")},
        std::array{tr("Serialized scene"), QStringLiteral("1"), formattedBytes(sceneBytes),
                   tr("Measured SceneDocument bytes")},
        std::array{tr("Entities"), QString::number(m_entityCount), formattedBytes(entityBytes),
                   tr("Estimated model: %1 B/entity").arg(EstimatedEntityBytes)},
        std::array{tr("Components"), QString::number(m_componentCount),
                   formattedBytes(componentBytes),
                   tr("Estimated model: %1 B/component").arg(EstimatedComponentBytes)},
        std::array{tr("PC resident assets"), QStringLiteral("—"),
                   m_measuredPcResidentBytes
                       ? formattedBytes(static_cast<std::size_t>(*m_measuredPcResidentBytes))
                       : tr("Unavailable"),
                   m_measuredPcResidentBytes ? tr("Measured Studio Play working set")
                                             : tr("Start Studio Play to measure")},
        std::array{tr("ESP32 resident assets"), QStringLiteral("—"),
                   formattedBytes(static_cast<std::size_t>(estimatedEsp32Resident)),
                   tr("Estimated from importer storage plan")},
        std::array{tr("ESP32 Internal RAM"), QStringLiteral("—"),
                   formattedBytes(static_cast<std::size_t>(m_internalRamEstimate)),
                   tr("Estimated from imported payload placement")},
        std::array{tr("ESP32 PSRAM"), QStringLiteral("—"),
                   formattedBytes(static_cast<std::size_t>(m_psramEstimate)),
                   tr("Estimated; zero budget means unavailable")},
        std::array{tr("ESP32 Flash"), QStringLiteral("—"),
                   formattedBytes(static_cast<std::size_t>(m_flashEstimate)),
                   tr("Estimated packed asset payload")},
        std::array{tr("ESP32 SD"), QStringLiteral("—"),
                   formattedBytes(static_cast<std::size_t>(m_sdEstimate)),
                   tr("Estimated external-storage payload")}};
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        for (int column = 0; column < static_cast<int>(rows[0].size()); ++column) {
            m_summary->setItem(
                row, column,
                new QTableWidgetItem(
                    rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]));
        }
    }

    const auto pc = fabgl::project::selectedPerformanceBudget(
        m_performance, fabgl::project::PerformanceTarget::Pc);
    const auto esp32 = fabgl::project::selectedPerformanceBudget(
        m_performance, fabgl::project::PerformanceTarget::Esp32);
    const std::uint64_t pcUsage =
        m_measuredPcResidentBytes.value_or(static_cast<std::uint64_t>(m_estimatedRuntimeBytes));
    const auto percentage = [](const std::uint64_t usage, const std::uint64_t budget) {
        if (budget == 0) {
            return usage == 0U ? 0 : 100;
        }
        const double value = static_cast<double>(usage) * 100.0 / static_cast<double>(budget);
        return std::clamp(static_cast<int>(std::ceil(value)), 0, 100);
    };
    m_pcBudgetBar->setValue(percentage(pcUsage, pc.assetResidentBytes));
    m_esp32BudgetBar->setValue(percentage(m_internalRamEstimate, esp32.internalRamBytes));
    m_pcBudgetBar->setToolTip(
        m_measuredPcResidentBytes
            ? tr("Measured PC resident assets: %1 / %2")
                  .arg(formattedBytes(static_cast<std::size_t>(pcUsage)),
                       formattedBytes(static_cast<std::size_t>(pc.assetResidentBytes)))
            : tr("Authoring estimate only: %1 / %2; measured PC value is unavailable")
                  .arg(formattedBytes(static_cast<std::size_t>(pcUsage)),
                       formattedBytes(static_cast<std::size_t>(pc.assetResidentBytes))));
    m_esp32BudgetBar->setToolTip(
        tr("Estimated ESP32 Internal RAM: %1 / %2")
            .arg(formattedBytes(static_cast<std::size_t>(m_internalRamEstimate)),
                 formattedBytes(static_cast<std::size_t>(esp32.internalRamBytes))));

    const std::array storageChecks = {
        std::pair{m_internalRamEstimate, esp32.internalRamBytes},
        std::pair{m_psramEstimate, esp32.psramBytes},
        std::pair{m_flashEstimate, esp32.flashBytes}, std::pair{m_sdEstimate, esp32.sdBytes}};
    bool warning = pcUsage > pc.assetResidentBytes;
    bool error = pcUsage > pc.assetResidentBytes * 5U / 4U;
    for (const auto& [usage, budget] : storageChecks) {
        const bool exceeded = budget == 0U ? usage > 0U : usage > budget;
        const bool severelyExceeded =
            budget == 0U ? usage > 0U : usage > budget + budget / 4U;
        warning = warning || exceeded;
        error = error || severelyExceeded;
    }
    const QString severity = error ? tr("ERROR") : (warning ? tr("WARNING") : tr("OK"));
    const QString recommendation =
        warning ? tr(" Downscale/compress assets or move eligible payloads to streaming storage.")
                : QString{};
    m_status->setText(
        tr("%1 — PC resident: %2 (%3); ESP32 Internal RAM estimate: %4 / %5.%6")
            .arg(severity,
                 formattedBytes(static_cast<std::size_t>(pcUsage)),
                 m_measuredPcResidentBytes ? tr("measured") : tr("authoring estimate"),
                 formattedBytes(static_cast<std::size_t>(m_internalRamEstimate)),
                 formattedBytes(static_cast<std::size_t>(esp32.internalRamBytes)),
                 recommendation));
    m_status->setStyleSheet(error   ? QStringLiteral("color: #ff4d4d; font-weight: bold;")
                            : warning ? QStringLiteral("color: #ffb347;")
                                      : QStringLiteral("color: #65d46e;"));
    emit statusMessage(m_status->text());
}

} // namespace fgl::studio
