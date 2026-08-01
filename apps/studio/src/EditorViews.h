#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/renderer_2d.h>

#include <QImage>
#include <QString>
#include <QWidget>

#include <cstdint>

class QDragEnterEvent;
class QDropEvent;
class QMouseEvent;
class QPaintEvent;

namespace fabgl {
class Scene;
}

namespace fgl::studio {

class SceneDocument;

class SceneView final : public QWidget {
    Q_OBJECT

  public:
    explicit SceneView(QWidget* parent = nullptr);

    void setDocument(SceneDocument* document);
    void setSelectedEntity(const QString& guid);
    void setEditable(bool editable);
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const noexcept;

  signals:
    void entitySelected(const QString& guid);
    void entityMovePreview(const QString& guid, float x, float y, float z);
    void entityMoveCommitted(const QString& guid, float oldX, float oldY, float oldZ, float newX,
                             float newY, float newZ);
    void assetDropped(const QString& filePath, float worldX, float worldY);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private:
    [[nodiscard]] QPointF worldToScreen(fabgl::Vec3 position) const;
    [[nodiscard]] fabgl::Vec3 screenToWorld(QPointF position, float z) const;
    [[nodiscard]] QString pickEntity(QPointF position) const;

    SceneDocument* m_document = nullptr;
    QString m_selectedGuid;
    fabgl::Vec3 m_dragStart{};
    bool m_editable = true;
    bool m_snapEnabled = true;
    bool m_dragging = false;
    float m_pixelsPerUnit = 32.0F;
    float m_snapStep = 0.5F;
};

struct FrameRenderStats final {
    double pcFrameMilliseconds = 0.0;
    std::uint32_t drawCalls = 0;
    std::uint32_t spritesSubmitted = 0;
};

class GameView final : public QWidget {
    Q_OBJECT

  public:
    explicit GameView(QWidget* parent = nullptr);

    [[nodiscard]] FrameRenderStats renderScene(const fabgl::Scene& scene, double elapsedSeconds);
    void setOverlayText(const QString& text);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void uploadFramebuffer();

    fabgl::rendering::Framebuffer m_framebuffer;
    fabgl::rendering::Sprite m_entitySprite;
    QImage m_image;
    QString m_overlayText = QStringLiteral("EDIT MODE");
};

} // namespace fgl::studio
