#include "EditorViews.h"

#include "SceneDocument.h"

#include <fabgl/scene/entity.h>
#include <fabgl/scene/scene.h>

#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace fgl::studio {

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
            if (guid == m_selectedGuid) {
                m_selectedGuid.clear();
            }
            update();
        });
        connect(m_document, &SceneDocument::entityChanged, this,
                [this](const QString&) { update(); });
    }
    m_selectedGuid.clear();
    update();
}

void SceneView::setSelectedEntity(const QString& guid) {
    if (m_selectedGuid == guid) {
        return;
    }
    m_selectedGuid = guid;
    update();
}

void SceneView::setEditable(const bool editable) {
    m_editable = editable;
    if (!m_editable) {
        m_dragging = false;
    }
}

void SceneView::setSnapEnabled(const bool enabled) {
    m_snapEnabled = enabled;
}

bool SceneView::snapEnabled() const noexcept {
    return m_snapEnabled;
}

void SceneView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(QStringLiteral("#1d2025")));

    const QPointF origin(width() / 2.0, height() / 2.0);
    const int spacing = static_cast<int>(m_pixelsPerUnit);
    painter.setPen(QPen(QColor(QStringLiteral("#2a2e35")), 1.0));
    for (int x = static_cast<int>(origin.x()) % spacing; x < width(); x += spacing) {
        painter.drawLine(x, 0, x, height());
    }
    for (int y = static_cast<int>(origin.y()) % spacing; y < height(); y += spacing) {
        painter.drawLine(0, y, width(), y);
    }
    painter.setPen(QPen(QColor(QStringLiteral("#49505a")), 1.5));
    painter.drawLine(QPointF(0.0, origin.y()), QPointF(static_cast<double>(width()), origin.y()));
    painter.drawLine(QPointF(origin.x(), 0.0), QPointF(origin.x(), static_cast<double>(height())));

    if (m_document == nullptr) {
        painter.setPen(Qt::lightGray);
        painter.drawText(rect(), Qt::AlignCenter, tr("No scene loaded"));
        return;
    }

    const auto entities = m_document->scene().entities();
    for (const auto* entity : entities) {
        const QPointF point = worldToScreen(entity->transform().localPosition());
        const bool selected = SceneDocument::guidString(entity->id()) == m_selectedGuid;
        painter.setBrush(entity->active() ? QColor(QStringLiteral("#55aaff"))
                                          : QColor(QStringLiteral("#666b73")));
        painter.setPen(selected ? QPen(QColor(QStringLiteral("#ffd166")), 3.0)
                                : QPen(QColor(QStringLiteral("#d9e6f2")), 1.0));
        painter.drawEllipse(point, 8.0, 8.0);
        painter.setPen(entity->active() ? QColor(QStringLiteral("#e8edf2"))
                                        : QColor(QStringLiteral("#8a9098")));
        painter.drawText(point + QPointF(12.0, -8.0), QString::fromStdString(entity->name()));

        if (selected) {
            painter.setPen(QPen(QColor(QStringLiteral("#ef5350")), 2.0));
            painter.drawLine(point, point + QPointF(38.0, 0.0));
            painter.drawLine(point + QPointF(38.0, 0.0), point + QPointF(30.0, -5.0));
            painter.drawLine(point + QPointF(38.0, 0.0), point + QPointF(30.0, 5.0));
            painter.setPen(QPen(QColor(QStringLiteral("#66bb6a")), 2.0));
            painter.drawLine(point, point + QPointF(0.0, -38.0));
            painter.drawLine(point + QPointF(0.0, -38.0), point + QPointF(-5.0, -30.0));
            painter.drawLine(point + QPointF(0.0, -38.0), point + QPointF(5.0, -30.0));
        }
    }

    painter.setPen(QColor(QStringLiteral("#9aa3ad")));
    painter.drawText(QRect(8, 8, width() - 16, 24), Qt::AlignLeft | Qt::AlignVCenter,
                     m_snapEnabled ? tr("Move gizmo • snap 0.5 units")
                                   : tr("Move gizmo • snap off"));
}

void SceneView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_document == nullptr) {
        QWidget::mousePressEvent(event);
        return;
    }
    const auto picked = pickEntity(event->position());
    m_selectedGuid = picked;
    emit entitySelected(picked);
    m_dragging = false;
    if (m_editable && !picked.isEmpty()) {
        const auto id = SceneDocument::parseEntityGuid(picked);
        const auto entitySnapshot = id ? m_document->snapshot(*id) : std::nullopt;
        if (entitySnapshot) {
            m_dragStart = entitySnapshot->position;
            m_dragging = true;
        }
    }
    update();
}

void SceneView::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging || m_document == nullptr || m_selectedGuid.isEmpty()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    auto world = screenToWorld(event->position(), m_dragStart.z);
    if (m_snapEnabled) {
        world.x = std::round(world.x / m_snapStep) * m_snapStep;
        world.y = std::round(world.y / m_snapStep) * m_snapStep;
    }
    emit entityMovePreview(m_selectedGuid, world.x, world.y, world.z);
}

void SceneView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging && m_document != nullptr) {
        const auto id = SceneDocument::parseEntityGuid(m_selectedGuid);
        const auto current = id ? m_document->snapshot(*id) : std::nullopt;
        if (current && current->position != m_dragStart) {
            emit entityMoveCommitted(m_selectedGuid, m_dragStart.x, m_dragStart.y, m_dragStart.z,
                                     current->position.x, current->position.y, current->position.z);
        }
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
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
    auto world = screenToWorld(event->position(), 0.0F);
    if (m_snapEnabled) {
        world.x = std::round(world.x / m_snapStep) * m_snapStep;
        world.y = std::round(world.y / m_snapStep) * m_snapStep;
    }
    emit assetDropped(urls.constFirst().toLocalFile(), world.x, world.y);
    event->acceptProposedAction();
}

QPointF SceneView::worldToScreen(const fabgl::Vec3 position) const {
    return {static_cast<double>(width()) / 2.0 + static_cast<double>(position.x * m_pixelsPerUnit),
            static_cast<double>(height()) / 2.0 -
                static_cast<double>(position.y * m_pixelsPerUnit)};
}

fabgl::Vec3 SceneView::screenToWorld(const QPointF position, const float z) const {
    return {static_cast<float>((position.x() - static_cast<double>(width()) / 2.0) /
                               static_cast<double>(m_pixelsPerUnit)),
            static_cast<float>((static_cast<double>(height()) / 2.0 - position.y()) /
                               static_cast<double>(m_pixelsPerUnit)),
            z};
}

QString SceneView::pickEntity(const QPointF position) const {
    if (m_document == nullptr) {
        return {};
    }
    const auto entities = m_document->scene().entities();
    for (auto iterator = entities.crbegin(); iterator != entities.crend(); ++iterator) {
        const QPointF delta = worldToScreen((*iterator)->transform().localPosition()) - position;
        if (delta.x() * delta.x() + delta.y() * delta.y() <= 196.0) {
            return SceneDocument::guidString((*iterator)->id());
        }
    }
    return {};
}

GameView::GameView(QWidget* parent)
    : QWidget(parent), m_framebuffer(320, 180),
      m_entitySprite(
          fabgl::rendering::makeCheckerSprite(8, 8, {78, 171, 255, 255}, {235, 245, 255, 255})) {
    setMinimumSize(360, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_framebuffer.clear({12, 16, 24, 255});
    uploadFramebuffer();
}

FrameRenderStats GameView::renderScene(const fabgl::Scene& scene, const double elapsedSeconds) {
    QElapsedTimer timer;
    timer.start();
    m_framebuffer.clear({12, 16, 24, 255});
    for (int x = 0; x < m_framebuffer.width(); x += 16) {
        m_framebuffer.drawLine(x, 0, x, m_framebuffer.height() - 1, {22, 30, 43, 255});
    }
    for (int y = 0; y < m_framebuffer.height(); y += 16) {
        m_framebuffer.drawLine(0, y, m_framebuffer.width() - 1, y, {22, 30, 43, 255});
    }

    fabgl::rendering::Renderer2D renderer(m_framebuffer);
    renderer.resetCounters();
    const auto entities = scene.entities();
    for (const auto* entity : entities) {
        if (!entity->active()) {
            continue;
        }
        const auto position = entity->transform().localPosition();
        const int bob = static_cast<int>(std::sin(elapsedSeconds * 2.0) * 2.0);
        const int x = m_framebuffer.width() / 2 + static_cast<int>(position.x * 16.0F) - 8;
        const int y = m_framebuffer.height() / 2 - static_cast<int>(position.y * 16.0F) - 8 + bob;
        renderer.draw({&m_entitySprite, x, y, 2, false, false, {255, 255, 255, 255}});
    }

    uploadFramebuffer();
    update();
    return {static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0, renderer.drawCalls(),
            renderer.spritesSubmitted()};
}

void GameView::setOverlayText(const QString& text) {
    m_overlayText = text;
    update();
}

void GameView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_image.isNull()) {
        QSize target = m_image.size();
        target.scale(size(), Qt::KeepAspectRatio);
        const QRect destination((width() - target.width()) / 2, (height() - target.height()) / 2,
                                target.width(), target.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(destination, m_image);
    }
    painter.setPen(QColor(QStringLiteral("#f5f7fa")));
    painter.setBrush(QColor(0, 0, 0, 150));
    const QRect labelRect(8, 8, std::max(120, width() - 16), 28);
    painter.drawRoundedRect(labelRect, 4.0, 4.0);
    painter.drawText(labelRect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
                     m_overlayText);
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
            line[destination] = color.r;
            line[destination + 1] = color.g;
            line[destination + 2] = color.b;
            line[destination + 3] = color.a;
        }
    }
}

} // namespace fgl::studio
