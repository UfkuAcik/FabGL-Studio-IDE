#include "EditorViews.h"

#include "SceneDocument.h"

#include <fabgl/scene/builtin_components.h>
#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <QDragEnterEvent>
#include <QCursor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fgl::studio {
namespace {

std::optional<fabgl::Vec3> inverseAffinePoint(const fabgl::Mat4& matrix,
                                              const fabgl::Vec3 point) {
    const float a = matrix.at(0U, 0U);
    const float b = matrix.at(0U, 1U);
    const float c = matrix.at(0U, 2U);
    const float d = matrix.at(1U, 0U);
    const float e = matrix.at(1U, 1U);
    const float f = matrix.at(1U, 2U);
    const float g = matrix.at(2U, 0U);
    const float h = matrix.at(2U, 1U);
    const float i = matrix.at(2U, 2U);
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) +
                              c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::fabs(determinant) < 0.000001F) {
        return std::nullopt;
    }
    const float inverseDeterminant = 1.0F / determinant;
    const fabgl::Vec3 translated{point.x - matrix.at(0U, 3U),
                                 point.y - matrix.at(1U, 3U),
                                 point.z - matrix.at(2U, 3U)};
    return fabgl::Vec3{
        ((e * i - f * h) * translated.x + (c * h - b * i) * translated.y +
         (b * f - c * e) * translated.z) *
            inverseDeterminant,
        ((f * g - d * i) * translated.x + (a * i - c * g) * translated.y +
         (c * d - a * f) * translated.z) *
            inverseDeterminant,
        ((d * h - e * g) * translated.x + (b * g - a * h) * translated.y +
         (a * e - b * d) * translated.z) *
            inverseDeterminant};
}

} // namespace

SceneView::SceneView(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(360, 240);
    setMouseTracking(true);
}

void SceneView::setDocument(SceneDocument* document) {
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document != nullptr) {
        connect(m_document, &SceneDocument::sceneReset, this, [this]() { update(); });
        connect(m_document, &SceneDocument::entityAdded, this,
                [this](const QString&) { update(); });
        connect(m_document, &SceneDocument::entityRemoved, this, [this](const QString& guid) {
            m_selectedGuids.removeAll(guid);
            m_selectedGuid = m_selectedGuids.isEmpty() ? QString{} : m_selectedGuids.constLast();
            update();
        });
        connect(m_document, &SceneDocument::entityChanged, this,
                [this](const QString&) { update(); });
    }
    m_selectedGuid.clear();
    m_selectedGuids.clear();
    update();
}

void SceneView::setSelectedEntity(const QString& guid) {
    setSelectedEntities(guid.isEmpty() ? QStringList{} : QStringList{guid});
}

void SceneView::setSelectedEntities(const QStringList& guids) {
    QStringList normalized;
    normalized.reserve(guids.size());
    for (const auto& guid : guids) {
        if (!guid.isEmpty() && !normalized.contains(guid)) {
            normalized.push_back(guid);
        }
    }
    const QString primary = normalized.isEmpty() ? QString{} : normalized.constLast();
    if (m_selectedGuid == primary && m_selectedGuids == normalized) {
        return;
    }
    m_selectedGuids = std::move(normalized);
    m_selectedGuid = primary;
    update();
}

void SceneView::setEditable(const bool editable) {
    m_editable = editable;
    if (!m_editable) {
        m_dragging = false;
        m_boxSelecting = false;
    }
}

void SceneView::setSnapEnabled(const bool enabled) {
    m_snapEnabled = enabled;
}

bool SceneView::snapEnabled() const noexcept {
    return m_snapEnabled;
}

void SceneView::setTool(const Tool tool) {
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    m_dragging = false;
    m_boxSelecting = false;
    update();
}

SceneView::Tool SceneView::tool() const noexcept {
    return m_tool;
}

void SceneView::setTransformSpace(const TransformSpace space) {
    if (m_transformSpace != space) {
        m_transformSpace = space;
        update();
    }
}

SceneView::TransformSpace SceneView::transformSpace() const noexcept {
    return m_transformSpace;
}

void SceneView::setViewMode(const ViewMode mode) {
    if (m_viewMode != mode) {
        m_viewMode = mode;
        update();
    }
}

SceneView::ViewMode SceneView::viewMode() const noexcept {
    return m_viewMode;
}

QString SceneView::selectedEntityGuid() const {
    return m_selectedGuid;
}

QStringList SceneView::selectedEntityGuids() const {
    return m_selectedGuids;
}

float SceneView::zoomFactor() const noexcept {
    return m_pixelsPerUnit;
}

QPointF SceneView::cameraOffset() const noexcept {
    return m_cameraOffset;
}

void SceneView::frameSelected() {
    if (m_document == nullptr || m_selectedGuid.isEmpty()) {
        return;
    }
    const auto id = SceneDocument::parseEntityGuid(m_selectedGuid);
    const auto* entity = id ? m_document->scene().findEntity(*id) : nullptr;
    if (entity == nullptr) {
        return;
    }
    const QPointF projected = projectWorld(entityWorldPosition(*entity));
    m_cameraOffset = {-projected.x() * static_cast<double>(m_pixelsPerUnit),
                      -projected.y() * static_cast<double>(m_pixelsPerUnit)};
    update();
}

void SceneView::zoomIn() {
    setZoomAround(m_pixelsPerUnit * 1.25F,
                  QPointF(static_cast<double>(width()) / 2.0,
                          static_cast<double>(height()) / 2.0));
}

void SceneView::zoomOut() {
    setZoomAround(m_pixelsPerUnit / 1.25F,
                  QPointF(static_cast<double>(width()) / 2.0,
                          static_cast<double>(height()) / 2.0));
}

void SceneView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor background =
        m_viewMode == ViewMode::RaycastMap
            ? QColor(QStringLiteral("#171d1d"))
            : (m_viewMode == ViewMode::ThreeDimensional ? QColor(QStringLiteral("#1b1b24"))
                                                        : QColor(QStringLiteral("#1d2025")));
    painter.fillRect(rect(), background);

    const QPointF origin(static_cast<double>(width()) / 2.0 + m_cameraOffset.x(),
                         static_cast<double>(height()) / 2.0 + m_cameraOffset.y());
    const int spacing = std::max(8, static_cast<int>(std::round(m_pixelsPerUnit)));
    const auto firstGridLine = [spacing](const double coordinate) {
        auto remainder = static_cast<int>(std::floor(coordinate)) % spacing;
        if (remainder < 0) {
            remainder += spacing;
        }
        return remainder;
    };
    if (m_viewMode != ViewMode::ThreeDimensional) {
        painter.setPen(QPen(QColor(QStringLiteral("#2a2e35")), 1.0));
        for (int x = firstGridLine(origin.x()); x < width(); x += spacing) {
            painter.drawLine(x, 0, x, height());
        }
        for (int y = firstGridLine(origin.y()); y < height(); y += spacing) {
            painter.drawLine(0, y, width(), y);
        }
        painter.setPen(QPen(QColor(QStringLiteral("#49505a")), 1.5));
        painter.drawLine(QPointF(0.0, origin.y()),
                         QPointF(static_cast<double>(width()), origin.y()));
        painter.drawLine(QPointF(origin.x(), 0.0),
                         QPointF(origin.x(), static_cast<double>(height())));
    } else {
        painter.setPen(QPen(QColor(QStringLiteral("#353747")), 1.0));
        constexpr int GridExtent = 24;
        for (int coordinate = -GridExtent; coordinate <= GridExtent; ++coordinate) {
            const float value = static_cast<float>(coordinate);
            painter.drawLine(worldToScreen({value, 0.0F, -static_cast<float>(GridExtent)}),
                             worldToScreen({value, 0.0F, static_cast<float>(GridExtent)}));
            painter.drawLine(worldToScreen({-static_cast<float>(GridExtent), 0.0F, value}),
                             worldToScreen({static_cast<float>(GridExtent), 0.0F, value}));
        }
        painter.setPen(QPen(QColor(QStringLiteral("#ef5350")), 1.5));
        painter.drawLine(worldToScreen({-static_cast<float>(GridExtent), 0.0F, 0.0F}),
                         worldToScreen({static_cast<float>(GridExtent), 0.0F, 0.0F}));
        painter.setPen(QPen(QColor(QStringLiteral("#42a5f5")), 1.5));
        painter.drawLine(worldToScreen({0.0F, 0.0F, -static_cast<float>(GridExtent)}),
                         worldToScreen({0.0F, 0.0F, static_cast<float>(GridExtent)}));
    }

    if (m_document == nullptr) {
        painter.setPen(Qt::lightGray);
        painter.drawText(rect(), Qt::AlignCenter, tr("No scene loaded"));
        return;
    }

    const auto entities = m_document->scene().entities();
    painter.setPen(QPen(QColor(QStringLiteral("#66717e")), 1.0, Qt::DashLine));
    for (const auto* entity : entities) {
        const auto parentId = entity->transform().parent();
        const auto* parent = parentId ? m_document->scene().findEntity(*parentId) : nullptr;
        if (parent != nullptr) {
            painter.drawLine(worldToScreen(entityWorldPosition(*parent)),
                             worldToScreen(entityWorldPosition(*entity)));
        }
    }
    for (const auto* entity : entities) {
        const QPointF point = worldToScreen(entityWorldPosition(*entity));
        const bool selected = m_selectedGuids.contains(SceneDocument::guidString(entity->id()));
        if (selected) {
            for (const auto* component : entity->components()) {
                const auto* dataComponent = dynamic_cast<const fabgl::DataComponent*>(component);
                if (dataComponent == nullptr) {
                    continue;
                }
                const std::string_view type = dataComponent->typeName();
                painter.setBrush(Qt::NoBrush);
                if (type == "fabgl.Camera") {
                    double size = 5.0;
                    if (const auto property = dataComponent->get("size"); property) {
                        if (const auto* value = std::get_if<double>(&property.value())) {
                            size = std::clamp(*value, 0.1, 1000.0);
                        }
                    }
                    const double halfHeight = size * static_cast<double>(m_pixelsPerUnit);
                    const double halfWidth = halfHeight * 4.0 / 3.0;
                    painter.setPen(QPen(QColor(QStringLiteral("#4fc3f7")), 1.0, Qt::DashLine));
                    painter.drawRect(QRectF(point.x() - halfWidth, point.y() - halfHeight,
                                            halfWidth * 2.0, halfHeight * 2.0));
                } else if (type == "fabgl.Collider2D") {
                    std::int64_t shape = 0;
                    fabgl::Vec2 size{1.0F, 1.0F};
                    double radius = 0.5;
                    if (const auto property = dataComponent->get("shape"); property) {
                        if (const auto* value = std::get_if<std::int64_t>(&property.value())) {
                            shape = *value;
                        }
                    }
                    if (const auto property = dataComponent->get("size"); property) {
                        if (const auto* value = std::get_if<fabgl::Vec2>(&property.value())) {
                            size = *value;
                        }
                    }
                    if (const auto property = dataComponent->get("radius"); property) {
                        if (const auto* value = std::get_if<double>(&property.value())) {
                            radius = std::max(0.0, *value);
                        }
                    }
                    painter.setPen(QPen(QColor(QStringLiteral("#81c784")), 1.5));
                    if (shape == 1) {
                        const double pixels = radius * static_cast<double>(m_pixelsPerUnit);
                        painter.drawEllipse(point, pixels, pixels);
                    } else {
                        const double widthPixels =
                            static_cast<double>(size.x * m_pixelsPerUnit);
                        const double heightPixels =
                            static_cast<double>(size.y * m_pixelsPerUnit);
                        painter.drawRect(QRectF(point.x() - widthPixels * 0.5,
                                                point.y() - heightPixels * 0.5, widthPixels,
                                                heightPixels));
                    }
                } else if (type == "fabgl.Collider3D") {
                    fabgl::Vec3 size{1.0F, 1.0F, 1.0F};
                    if (const auto property = dataComponent->get("size"); property) {
                        if (const auto* value = std::get_if<fabgl::Vec3>(&property.value())) {
                            size = *value;
                        }
                    }
                    painter.setPen(QPen(QColor(QStringLiteral("#66bb6a")), 1.5));
                    const double widthPixels = static_cast<double>(size.x * m_pixelsPerUnit);
                    const double heightPixels = static_cast<double>(size.y * m_pixelsPerUnit);
                    painter.drawRect(QRectF(point.x() - widthPixels * 0.5,
                                            point.y() - heightPixels * 0.5, widthPixels,
                                            heightPixels));
                    painter.drawRect(QRectF(point.x() - widthPixels * 0.35 - 6.0,
                                            point.y() - heightPixels * 0.35 - 6.0,
                                            widthPixels * 0.7, heightPixels * 0.7));
                } else if (type == "fabgl.Light") {
                    double intensity = 1.0;
                    if (const auto property = dataComponent->get("intensity"); property) {
                        if (const auto* value = std::get_if<double>(&property.value())) {
                            intensity = std::clamp(*value, 0.0, 8.0);
                        }
                    }
                    const double radius = 12.0 + intensity * 12.0;
                    painter.setPen(QPen(QColor(QStringLiteral("#fff176")), 1.5));
                    painter.drawEllipse(point, 5.0, 5.0);
                    painter.drawEllipse(point, radius, radius);
                    for (int angle = 0; angle < 360; angle += 45) {
                        const double radians =
                            static_cast<double>(angle) * 3.141592653589793 / 180.0;
                        painter.drawLine(point + QPointF(std::cos(radians) * (radius + 4.0),
                                                         std::sin(radians) * (radius + 4.0)),
                                         point + QPointF(std::cos(radians) * (radius + 12.0),
                                                         std::sin(radians) * (radius + 12.0)));
                    }
                } else if (type == "fabgl.AudioSource") {
                    painter.setPen(QPen(QColor(QStringLiteral("#ce93d8")), 1.5));
                    painter.drawArc(QRectF(point.x() - 14.0, point.y() - 14.0, 28.0, 28.0),
                                    -45 * 16, 90 * 16);
                    painter.drawArc(QRectF(point.x() - 24.0, point.y() - 24.0, 48.0, 48.0),
                                    -45 * 16, 90 * 16);
                } else if (type == "fabgl.NavigationAgent") {
                    const auto target = dataComponent->get("target");
                    const auto* targetId =
                        target ? std::get_if<fabgl::EntityGuid>(&target.value()) : nullptr;
                    const auto* targetEntity = targetId != nullptr && !targetId->isNil()
                                                   ? m_document->scene().findEntity(*targetId)
                                                   : nullptr;
                    if (targetEntity != nullptr) {
                        painter.setPen(
                            QPen(QColor(QStringLiteral("#ffb74d")), 1.5, Qt::DashDotLine));
                        painter.drawLine(point,
                                         worldToScreen(entityWorldPosition(*targetEntity)));
                    }
                }
            }
        }
        painter.setBrush(entity->active() ? QColor(QStringLiteral("#55aaff"))
                                          : QColor(QStringLiteral("#666b73")));
        painter.setPen(selected ? QPen(QColor(QStringLiteral("#ffd166")), 3.0)
                                : QPen(QColor(QStringLiteral("#d9e6f2")), 1.0));
        painter.drawEllipse(point, 8.0, 8.0);
        painter.setPen(entity->active() ? QColor(QStringLiteral("#e8edf2"))
                                        : QColor(QStringLiteral("#8a9098")));
        painter.drawText(point + QPointF(12.0, -8.0), QString::fromStdString(entity->name()));

        const auto oriented = [this, entity](const QPointF vector) {
            if (m_transformSpace == TransformSpace::World) {
                return vector;
            }
            const double angle = -static_cast<double>(entity->transform().localRotation().z);
            return QPointF(vector.x() * std::cos(angle) - vector.y() * std::sin(angle),
                           vector.x() * std::sin(angle) + vector.y() * std::cos(angle));
        };
        if (selected && m_tool == Tool::Move) {
            const QPointF xAxis = oriented(QPointF(38.0, 0.0));
            const QPointF yAxis = oriented(QPointF(0.0, -38.0));
            painter.setPen(QPen(QColor(QStringLiteral("#ef5350")), 2.0));
            painter.drawLine(point, point + xAxis);
            painter.drawEllipse(point + xAxis, 3.0, 3.0);
            painter.setPen(QPen(QColor(QStringLiteral("#66bb6a")), 2.0));
            painter.drawLine(point, point + yAxis);
            painter.drawEllipse(point + yAxis, 3.0, 3.0);
        } else if (selected && m_tool == Tool::Rotate) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(QStringLiteral("#ffca28")), 2.0));
            painter.drawEllipse(point, 30.0, 30.0);
            painter.drawArc(QRectF(point.x() - 35.0, point.y() - 35.0, 70.0, 70.0), 20 * 16,
                            110 * 16);
        } else if (selected && m_tool == Tool::Scale) {
            painter.setPen(QPen(QColor(QStringLiteral("#42a5f5")), 2.0));
            painter.drawLine(point, point + QPointF(34.0, 0.0));
            painter.drawLine(point, point + QPointF(0.0, -34.0));
            painter.setBrush(QColor(QStringLiteral("#42a5f5")));
            painter.drawRect(QRectF(point + QPointF(29.0, -5.0), QSizeF(10.0, 10.0)));
            painter.drawRect(QRectF(point + QPointF(-5.0, -39.0), QSizeF(10.0, 10.0)));
        } else if (selected) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(QStringLiteral("#ffd166")), 1.0, Qt::DashLine));
            painter.drawRect(QRectF(point.x() - 13.0, point.y() - 13.0, 26.0, 26.0));
        }
    }

    if (m_boxSelecting) {
        painter.setBrush(QColor(85, 170, 255, 32));
        painter.setPen(QPen(QColor(QStringLiteral("#55aaff")), 1.0, Qt::DashLine));
        painter.drawRect(QRectF(m_boxSelectionStart, m_boxSelectionCurrent).normalized());
    }

    const auto toolName = [this]() {
        switch (m_tool) {
        case Tool::Select:
            return tr("Select");
        case Tool::Move:
            return tr("Move");
        case Tool::Rotate:
            return tr("Rotate");
        case Tool::Scale:
            return tr("Scale");
        }
        return tr("Select");
    }();
    const auto modeName = m_viewMode == ViewMode::TwoDimensional
                              ? tr("2D")
                              : (m_viewMode == ViewMode::RaycastMap ? tr("Raycast Map")
                                                                   : tr("3D"));
    const auto spaceName = m_transformSpace == TransformSpace::Local ? tr("Local") : tr("World");
    painter.setPen(QColor(QStringLiteral("#9aa3ad")));
    painter.drawText(QRect(8, 32, width() - 16, 24), Qt::AlignLeft | Qt::AlignVCenter,
                     tr("%1 • %2 tool • %3 space • %4 px/unit • middle-drag pans")
                         .arg(modeName)
                         .arg(toolName)
                         .arg(spaceName)
                         .arg(m_pixelsPerUnit, 0, 'f', 1));
    painter.drawText(QRect(8, 8, width() - 16, 24), Qt::AlignLeft | Qt::AlignVCenter,
                     m_snapEnabled ? tr("Move gizmo • snap 0.5 units")
                                   : tr("Move gizmo • snap off"));
}

void SceneView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanScreen = event->position();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || m_document == nullptr) {
        QWidget::mousePressEvent(event);
        return;
    }
    const auto picked = pickEntity(event->position());
    const bool extendSelection = event->modifiers().testFlag(Qt::ControlModifier);
    if (m_tool == Tool::Select && picked.isEmpty()) {
        m_boxSelecting = true;
        m_boxSelectionStart = event->position();
        m_boxSelectionCurrent = event->position();
        if (!extendSelection) {
            m_selectedGuids.clear();
            m_selectedGuid.clear();
        }
        update();
        event->accept();
        return;
    }
    if (extendSelection && !picked.isEmpty()) {
        if (m_selectedGuids.contains(picked)) {
            m_selectedGuids.removeAll(picked);
        } else {
            m_selectedGuids.push_back(picked);
        }
    } else {
        m_selectedGuids = picked.isEmpty() ? QStringList{} : QStringList{picked};
    }
    m_selectedGuid = m_selectedGuids.isEmpty() ? QString{} : m_selectedGuids.constLast();
    emit entitySelected(m_selectedGuid);
    emit entitiesSelected(m_selectedGuids);
    m_dragging = false;
    if (m_editable && m_tool != Tool::Select && !m_selectedGuid.isEmpty()) {
        const auto id = SceneDocument::parseEntityGuid(m_selectedGuid);
        const auto entitySnapshot = id ? m_document->snapshot(*id) : std::nullopt;
        if (entitySnapshot) {
            m_dragStartPosition = entitySnapshot->position;
            const auto* entity = m_document->scene().findEntity(*id);
            m_dragStartWorldPosition = entity != nullptr ? entityWorldPosition(*entity)
                                                         : entitySnapshot->position;
            m_dragStartRotation = entitySnapshot->rotation;
            m_dragStartScale = entitySnapshot->scale;
            m_interactionStartScreen = event->position();
            m_dragging = true;
        }
    }
    update();
}

void SceneView::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        m_cameraOffset += event->position() - m_lastPanScreen;
        m_lastPanScreen = event->position();
        update();
        event->accept();
        return;
    }
    if (m_boxSelecting) {
        m_boxSelectionCurrent = event->position();
        update();
        event->accept();
        return;
    }
    if (!m_dragging || m_document == nullptr || m_selectedGuid.isEmpty()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (m_tool == Tool::Move) {
        auto world = screenToWorld(event->position(), m_dragStartWorldPosition);
        if (m_snapEnabled) {
            world.x = std::round(world.x / m_snapStep) * m_snapStep;
            world.y = std::round(world.y / m_snapStep) * m_snapStep;
            world.z = std::round(world.z / m_snapStep) * m_snapStep;
        }
        fabgl::Vec3 local = world;
        const auto id = SceneDocument::parseEntityGuid(m_selectedGuid);
        const auto* entity = id ? m_document->scene().findEntity(*id) : nullptr;
        const auto parentId = entity != nullptr ? entity->transform().parent() : std::nullopt;
        const auto* parent = parentId ? m_document->scene().findEntity(*parentId) : nullptr;
        if (m_transformSpace == TransformSpace::World && parent != nullptr) {
            const auto parentWorld = parent->transform().worldMatrix();
            const auto converted = parentWorld ? inverseAffinePoint(parentWorld.value(), world)
                                               : std::nullopt;
            if (converted) {
                local = *converted;
            }
        } else if (m_transformSpace == TransformSpace::Local) {
            const QPointF startScreen = worldToScreen(m_dragStartWorldPosition);
            const auto worldAtStart = screenToWorld(startScreen, m_dragStartWorldPosition);
            const auto worldAtPointer = screenToWorld(event->position(), m_dragStartWorldPosition);
            const fabgl::Vec3 worldDelta = worldAtPointer - worldAtStart;
            const auto parentWorld = parent != nullptr ? parent->transform().worldMatrix()
                                                       : fabgl::Result<fabgl::Mat4>::success(
                                                             fabgl::Mat4::identity());
            const auto convertedStart =
                parentWorld ? inverseAffinePoint(parentWorld.value(), m_dragStartWorldPosition)
                            : std::nullopt;
            const auto convertedEnd =
                parentWorld ? inverseAffinePoint(parentWorld.value(),
                                                 m_dragStartWorldPosition + worldDelta)
                            : std::nullopt;
            if (convertedStart && convertedEnd) {
                local = m_dragStartPosition + (*convertedEnd - *convertedStart);
            }
        }
        emit entityMovePreview(m_selectedGuid, local.x, local.y, local.z);
    } else if (m_tool == Tool::Rotate) {
        auto rotation = m_dragStartRotation;
        rotation.z += static_cast<float>(event->position().x() - m_interactionStartScreen.x()) *
                      0.5F;
        if (m_snapEnabled) {
            constexpr float RotationStep = 15.0F;
            rotation.z = std::round(rotation.z / RotationStep) * RotationStep;
        }
        emit entityRotationPreview(m_selectedGuid, rotation.x, rotation.y, rotation.z);
    } else if (m_tool == Tool::Scale) {
        float factor = 1.0F +
                       static_cast<float>(event->position().x() - m_interactionStartScreen.x()) /
                           100.0F;
        factor = std::max(0.05F, factor);
        if (m_snapEnabled) {
            constexpr float ScaleStep = 0.1F;
            factor = std::max(ScaleStep, std::round(factor / ScaleStep) * ScaleStep);
        }
        const fabgl::Vec3 scale{m_dragStartScale.x * factor, m_dragStartScale.y * factor,
                                m_dragStartScale.z * factor};
        emit entityScalePreview(m_selectedGuid, scale.x, scale.y, scale.z);
    }
}

void SceneView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_boxSelecting && m_document != nullptr) {
        m_boxSelectionCurrent = event->position();
        const QRectF selection(m_boxSelectionStart, m_boxSelectionCurrent);
        QStringList selected = event->modifiers().testFlag(Qt::ControlModifier)
                                   ? m_selectedGuids
                                   : QStringList{};
        const auto bounds = selection.normalized();
        for (const auto* entity : m_document->scene().entities()) {
            const auto guid = SceneDocument::guidString(entity->id());
            if (bounds.contains(worldToScreen(entityWorldPosition(*entity))) &&
                !selected.contains(guid)) {
                selected.push_back(guid);
            }
        }
        m_boxSelecting = false;
        m_selectedGuids = std::move(selected);
        m_selectedGuid = m_selectedGuids.isEmpty() ? QString{} : m_selectedGuids.constLast();
        emit entitySelected(m_selectedGuid);
        emit entitiesSelected(m_selectedGuids);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragging && m_document != nullptr) {
        const auto id = SceneDocument::parseEntityGuid(m_selectedGuid);
        const auto current = id ? m_document->snapshot(*id) : std::nullopt;
        if (current && m_tool == Tool::Move && current->position != m_dragStartPosition) {
            emit entityMoveCommitted(m_selectedGuid, m_dragStartPosition.x, m_dragStartPosition.y,
                                     m_dragStartPosition.z, current->position.x,
                                     current->position.y, current->position.z);
        } else if (current && m_tool == Tool::Rotate &&
                   current->rotation != m_dragStartRotation) {
            emit entityRotationCommitted(
                m_selectedGuid, m_dragStartRotation.x, m_dragStartRotation.y,
                m_dragStartRotation.z, current->rotation.x, current->rotation.y,
                current->rotation.z);
        } else if (current && m_tool == Tool::Scale && current->scale != m_dragStartScale) {
            emit entityScaleCommitted(m_selectedGuid, m_dragStartScale.x, m_dragStartScale.y,
                                      m_dragStartScale.z, current->scale.x, current->scale.y,
                                      current->scale.z);
        }
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void SceneView::wheelEvent(QWheelEvent* event) {
    const float factor = event->angleDelta().y() >= 0 ? 1.15F : (1.0F / 1.15F);
    setZoomAround(m_pixelsPerUnit * factor, event->position());
    event->accept();
}

void SceneView::dragEnterEvent(QDragEnterEvent* event) {
    if (m_editable && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void SceneView::dropEvent(QDropEvent* event) {
    if (!m_editable || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.constFirst().isLocalFile()) {
        event->ignore();
        return;
    }
    auto world = screenToWorld(event->position(), {});
    if (m_snapEnabled) {
        world.x = std::round(world.x / m_snapStep) * m_snapStep;
        world.y = std::round(world.y / m_snapStep) * m_snapStep;
        world.z = std::round(world.z / m_snapStep) * m_snapStep;
    }
    emit assetDropped(urls.constFirst().toLocalFile(), world.x, world.y, world.z);
    event->acceptProposedAction();
}

QPointF SceneView::projectWorld(const fabgl::Vec3 position) const {
    switch (m_viewMode) {
    case ViewMode::TwoDimensional:
        return {static_cast<double>(position.x), -static_cast<double>(position.y)};
    case ViewMode::RaycastMap:
        return {static_cast<double>(position.x), static_cast<double>(position.z)};
    case ViewMode::ThreeDimensional:
        constexpr double IsometricCosine = 0.8660254037844386;
        return {(static_cast<double>(position.x) - static_cast<double>(position.z)) *
                    IsometricCosine,
                -static_cast<double>(position.y) +
                    (static_cast<double>(position.x) + static_cast<double>(position.z)) * 0.5};
    }
    return {};
}

QPointF SceneView::worldToScreen(const fabgl::Vec3 position) const {
    const QPointF projected = projectWorld(position);
    return {static_cast<double>(width()) / 2.0 + m_cameraOffset.x() +
                projected.x() * static_cast<double>(m_pixelsPerUnit),
            static_cast<double>(height()) / 2.0 + m_cameraOffset.y() +
                projected.y() * static_cast<double>(m_pixelsPerUnit)};
}

fabgl::Vec3 SceneView::screenToWorld(const QPointF position,
                                     const fabgl::Vec3 referencePosition) const {
    const double projectedX =
        (position.x() - static_cast<double>(width()) / 2.0 - m_cameraOffset.x()) /
        static_cast<double>(m_pixelsPerUnit);
    const double projectedY =
        (position.y() - static_cast<double>(height()) / 2.0 - m_cameraOffset.y()) /
        static_cast<double>(m_pixelsPerUnit);
    switch (m_viewMode) {
    case ViewMode::TwoDimensional:
        return {static_cast<float>(projectedX), static_cast<float>(-projectedY),
                referencePosition.z};
    case ViewMode::RaycastMap:
        return {static_cast<float>(projectedX), referencePosition.y,
                static_cast<float>(projectedY)};
    case ViewMode::ThreeDimensional: {
        constexpr double IsometricCosine = 0.8660254037844386;
        const double z = static_cast<double>(referencePosition.z);
        const double x = projectedX / IsometricCosine + z;
        const double y = (x + z) * 0.5 - projectedY;
        return {static_cast<float>(x), static_cast<float>(y), referencePosition.z};
    }
    }
    return referencePosition;
}

fabgl::Vec3 SceneView::entityWorldPosition(const fabgl::Entity& entity) const {
    const auto world = entity.transform().worldMatrix();
    return world ? world.value().transformPoint({}) : entity.transform().localPosition();
}

void SceneView::setZoomAround(const float pixelsPerUnit, const QPointF anchor) {
    const auto worldAtAnchor = screenToWorld(anchor, {});
    m_pixelsPerUnit = std::clamp(pixelsPerUnit, 8.0F, 256.0F);
    const QPointF projected = projectWorld(worldAtAnchor);
    m_cameraOffset.setX(anchor.x() - static_cast<double>(width()) / 2.0 -
                        projected.x() * static_cast<double>(m_pixelsPerUnit));
    m_cameraOffset.setY(anchor.y() - static_cast<double>(height()) / 2.0 -
                        projected.y() * static_cast<double>(m_pixelsPerUnit));
    update();
}

QString SceneView::pickEntity(const QPointF position) const {
    if (m_document == nullptr) {
        return {};
    }
    const auto entities = m_document->scene().entities();
    for (auto iterator = entities.crbegin(); iterator != entities.crend(); ++iterator) {
        const QPointF delta = worldToScreen(entityWorldPosition(**iterator)) - position;
        if (delta.x() * delta.x() + delta.y() * delta.y() <= 196.0) {
            return SceneDocument::guidString((*iterator)->id());
        }
    }
    return {};
}

GameView::GameView(QWidget* parent)
    : QWidget(parent), m_framebuffer(320, 180) {
    setMinimumSize(360, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_framebuffer.clear({12, 16, 24, 255});
    uploadFramebuffer();
    m_presentClock.start();
}

void GameView::setPresentationResources(
    fabgl::rendering::ScenePresentationResources resources) {
    m_presentationResources = std::move(resources);
}

FrameRenderStats GameView::renderScene(const fabgl::Scene& scene, const double elapsedSeconds,
                                       const fabgl::SceneRuntime* runtime) {
    const qint64 presentNanoseconds = m_presentClock.nsecsElapsed();
    if (presentNanoseconds > 0) {
        const double currentFps = 1.0e9 / static_cast<double>(presentNanoseconds);
        m_measuredFps = m_measuredFps <= 0.0 ? currentFps : m_measuredFps * 0.85 + currentFps * 0.15;
    }
    m_presentClock.restart();
    QElapsedTimer timer;
    timer.start();
    fabgl::rendering::ScenePresenter presenter(m_framebuffer, m_presentationResources);
    const auto presentation =
        presenter.render(scene, runtime, static_cast<float>(elapsedSeconds));

    uploadFramebuffer();
    update();
    return {static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0, presentation.drawCalls,
            presentation.sprites, presentation.triangles, presentation.rays,
            presentation.particles};
}

void GameView::setOverlayText(const QString& text) {
    m_overlayText = text;
    update();
}

void GameView::setTargetResolution(const QSize& resolution) {
    if (!resolution.isValid() || resolution.width() > 4096 || resolution.height() > 4096 ||
        resolution == targetResolution()) {
        return;
    }
    m_framebuffer = fabgl::rendering::Framebuffer(resolution.width(), resolution.height());
    m_framebuffer.clear({12, 16, 24, 255});
    uploadFramebuffer();
    updateGeometry();
    update();
}

QSize GameView::targetResolution() const noexcept {
    return {m_framebuffer.width(), m_framebuffer.height()};
}

void GameView::setAspectMode(const AspectMode mode) {
    m_aspectMode = mode;
    update();
}

GameView::AspectMode GameView::aspectMode() const noexcept {
    return m_aspectMode;
}

void GameView::setIntegerScaling(const bool enabled) {
    m_integerScaling = enabled;
    update();
}

bool GameView::integerScaling() const noexcept {
    return m_integerScaling;
}

void GameView::setPixelPerfect(const bool enabled) {
    m_pixelPerfect = enabled;
    update();
}

bool GameView::pixelPerfect() const noexcept {
    return m_pixelPerfect;
}

void GameView::setPaletteMode(const PaletteMode mode) {
    if (m_paletteMode == mode) {
        return;
    }
    m_paletteMode = mode;
    uploadFramebuffer();
    update();
}

GameView::PaletteMode GameView::paletteMode() const noexcept {
    return m_paletteMode;
}

void GameView::setFpsOverlayVisible(const bool visible) {
    m_showFps = visible;
    update();
}

bool GameView::fpsOverlayVisible() const noexcept {
    return m_showFps;
}

void GameView::setTargetFps(const int fps) {
    m_targetFps = std::clamp(fps, 1, 240);
    update();
}

int GameView::targetFps() const noexcept {
    return m_targetFps;
}

void GameView::setSimulationSpeed(const double speed) {
    m_simulationSpeed = std::clamp(speed, 0.0, 8.0);
    update();
}

double GameView::simulationSpeed() const noexcept {
    return m_simulationSpeed;
}

void GameView::setEsp32SimulationMode(const bool enabled) {
    if (m_esp32Simulation == enabled) {
        return;
    }
    m_esp32Simulation = enabled;
    uploadFramebuffer();
    update();
}

bool GameView::esp32SimulationMode() const noexcept {
    return m_esp32Simulation;
}

void GameView::setRuntimeInputEnabled(const bool enabled) {
    if (m_runtimeInputEnabled == enabled) {
        return;
    }
    releaseHeldControls();
    m_runtimeInputEnabled = enabled;
    m_lastRuntimeMousePosition = mapFromGlobal(QCursor::pos());
    if (enabled) {
        setFocus(Qt::OtherFocusReason);
    }
}

bool GameView::runtimeInputEnabled() const noexcept {
    return m_runtimeInputEnabled;
}

namespace {

QString runtimeKeyControl(const int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QStringLiteral("Key.") + QChar(QLatin1Char('A').unicode() + key - Qt::Key_A);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QStringLiteral("Key.") + QChar(QLatin1Char('0').unicode() + key - Qt::Key_0);
    }
    switch (key) {
    case Qt::Key_Left:
        return QStringLiteral("Key.Left");
    case Qt::Key_Right:
        return QStringLiteral("Key.Right");
    case Qt::Key_Up:
        return QStringLiteral("Key.Up");
    case Qt::Key_Down:
        return QStringLiteral("Key.Down");
    case Qt::Key_Space:
        return QStringLiteral("Key.Space");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("Key.Enter");
    case Qt::Key_Escape:
        return QStringLiteral("Key.Escape");
    case Qt::Key_Shift:
        return QStringLiteral("Key.Shift");
    case Qt::Key_Control:
        return QStringLiteral("Key.Control");
    case Qt::Key_Alt:
        return QStringLiteral("Key.Alt");
    case Qt::Key_Tab:
        return QStringLiteral("Key.Tab");
    case Qt::Key_Backspace:
        return QStringLiteral("Key.Backspace");
    case Qt::Key_Delete:
        return QStringLiteral("Key.Delete");
    case Qt::Key_Home:
        return QStringLiteral("Key.Home");
    case Qt::Key_End:
        return QStringLiteral("Key.End");
    case Qt::Key_PageUp:
        return QStringLiteral("Key.PageUp");
    case Qt::Key_PageDown:
        return QStringLiteral("Key.PageDown");
    default:
        return {};
    }
}

QString runtimeMouseControl(const Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton:
        return QStringLiteral("Mouse.Left");
    case Qt::RightButton:
        return QStringLiteral("Mouse.Right");
    case Qt::MiddleButton:
        return QStringLiteral("Mouse.Middle");
    default:
        return {};
    }
}

} // namespace

void GameView::keyPressEvent(QKeyEvent* event) {
    const auto control = runtimeKeyControl(event->key());
    if (!m_runtimeInputEnabled || control.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (!event->isAutoRepeat() && !m_heldRuntimeControls.contains(control)) {
        m_heldRuntimeControls.push_back(control);
        emit runtimeControlChanged(control, 1.0F);
    }
    event->accept();
}

void GameView::keyReleaseEvent(QKeyEvent* event) {
    const auto control = runtimeKeyControl(event->key());
    if (!m_runtimeInputEnabled || control.isEmpty()) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    if (!event->isAutoRepeat()) {
        m_heldRuntimeControls.removeAll(control);
        emit runtimeControlChanged(control, 0.0F);
    }
    event->accept();
}

void GameView::mousePressEvent(QMouseEvent* event) {
    const auto control = runtimeMouseControl(event->button());
    if (!m_runtimeInputEnabled || control.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (!m_heldRuntimeControls.contains(control)) {
        m_heldRuntimeControls.push_back(control);
        emit runtimeControlChanged(control, 1.0F);
    }
    event->accept();
}

void GameView::mouseMoveEvent(QMouseEvent* event) {
    if (!m_runtimeInputEnabled) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPointF delta = event->position() - m_lastRuntimeMousePosition;
    m_lastRuntimeMousePosition = event->position();
    if (!qFuzzyIsNull(delta.x())) {
        emit runtimeControlChanged(QStringLiteral("Mouse.X"),
                                   std::clamp(static_cast<float>(delta.x() / 64.0), -1.0F, 1.0F));
    }
    if (!qFuzzyIsNull(delta.y())) {
        emit runtimeControlChanged(QStringLiteral("Mouse.Y"),
                                   std::clamp(static_cast<float>(delta.y() / 64.0), -1.0F, 1.0F));
    }
    event->accept();
}

void GameView::mouseReleaseEvent(QMouseEvent* event) {
    const auto control = runtimeMouseControl(event->button());
    if (!m_runtimeInputEnabled || control.isEmpty()) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_heldRuntimeControls.removeAll(control);
    emit runtimeControlChanged(control, 0.0F);
    event->accept();
}

void GameView::wheelEvent(QWheelEvent* event) {
    if (!m_runtimeInputEnabled) {
        QWidget::wheelEvent(event);
        return;
    }
    const QPoint delta = event->angleDelta();
    if (delta.x() != 0) {
        emit runtimeControlChanged(QStringLiteral("Mouse.WheelX"),
                                   std::clamp(static_cast<float>(delta.x()) / 120.0F, -1.0F,
                                              1.0F));
    }
    if (delta.y() != 0) {
        emit runtimeControlChanged(QStringLiteral("Mouse.WheelY"),
                                   std::clamp(static_cast<float>(delta.y()) / 120.0F, -1.0F,
                                              1.0F));
    }
    event->accept();
}

void GameView::focusOutEvent(QFocusEvent* event) {
    releaseHeldControls();
    QWidget::focusOutEvent(event);
}

void GameView::releaseHeldControls() {
    for (const auto& control : std::as_const(m_heldRuntimeControls)) {
        emit runtimeControlChanged(control, 0.0F);
    }
    m_heldRuntimeControls.clear();
}

void GameView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_image.isNull()) {
        QSize target;
        switch (m_aspectMode) {
        case AspectMode::Stretch:
            target = size();
            break;
        case AspectMode::FourThree:
            target = QSize(4, 3);
            target.scale(size(), Qt::KeepAspectRatio);
            break;
        case AspectMode::SixteenNine:
            target = QSize(16, 9);
            target.scale(size(), Qt::KeepAspectRatio);
            break;
        case AspectMode::Preserve:
            target = m_image.size();
            target.scale(size(), Qt::KeepAspectRatio);
            break;
        }
        if (m_integerScaling) {
            const int scale = std::min(width() / m_image.width(), height() / m_image.height());
            if (scale >= 1) {
                target = m_image.size() * scale;
            }
        }
        const QRect destination((width() - target.width()) / 2, (height() - target.height()) / 2,
                                target.width(), target.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, !m_pixelPerfect);
        painter.drawImage(destination, m_image);
    }
    painter.setPen(QColor(QStringLiteral("#f5f7fa")));
    painter.setBrush(QColor(0, 0, 0, 150));
    const QRect labelRect(8, 8, std::max(120, width() - 16), 28);
    painter.drawRoundedRect(labelRect, 4.0, 4.0);
    QString overlay = m_overlayText;
    if (m_esp32Simulation) {
        overlay += tr(" | ESP32 SIM");
    }
    if (m_showFps) {
        overlay += tr(" | %1 FPS (target %2) | %3×")
                       .arg(m_measuredFps, 0, 'f', 1)
                       .arg(m_targetFps)
                       .arg(m_simulationSpeed, 0, 'f', 2);
    }
    painter.drawText(labelRect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, overlay);
}

void GameView::uploadFramebuffer() {
    m_image = QImage(m_framebuffer.width(), m_framebuffer.height(), QImage::Format_RGBA8888);
    const auto& pixels = m_framebuffer.pixels();
    for (int y = 0; y < m_framebuffer.height(); ++y) {
        auto* line = m_image.scanLine(y);
        for (int x = 0; x < m_framebuffer.width(); ++x) {
            const auto offset =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(m_framebuffer.width()) +
                static_cast<std::size_t>(x);
            const auto color = pixels.at(offset);
            const int destination = x * 4;
            auto red = color.r;
            auto green = color.g;
            auto blue = color.b;
            const auto effectivePalette =
                m_esp32Simulation && m_paletteMode == PaletteMode::TrueColor
                    ? PaletteMode::Esp32Rgb222
                    : m_paletteMode;
            if (effectivePalette == PaletteMode::Esp32Rgb222) {
                const auto quantize = [](const std::uint8_t channel) {
                    return static_cast<std::uint8_t>((static_cast<unsigned>(channel) * 3U + 127U) /
                                                     255U * 85U);
                };
                red = quantize(red);
                green = quantize(green);
                blue = quantize(blue);
            } else if (effectivePalette == PaletteMode::Monochrome) {
                const unsigned luminance = static_cast<unsigned>(red) * 299U +
                                           static_cast<unsigned>(green) * 587U +
                                           static_cast<unsigned>(blue) * 114U;
                const auto monochrome = static_cast<std::uint8_t>(luminance >= 128000U ? 255 : 0);
                red = monochrome;
                green = monochrome;
                blue = monochrome;
            }
            line[destination] = red;
            line[destination + 1] = green;
            line[destination + 2] = blue;
            line[destination + 3] = color.a;
        }
    }
}

} // namespace fgl::studio
