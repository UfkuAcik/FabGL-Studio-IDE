#pragma once

#include <fabgl/math/types.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/scene_presenter.h>

#include <QImage>
#include <QElapsedTimer>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>

class QDragEnterEvent;
class QDropEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace fabgl {
class Entity;
class Scene;
class SceneRuntime;
}

namespace fgl::studio {

class SceneDocument;

class SceneView final : public QWidget {
    Q_OBJECT

  public:
    enum class Tool { Select, Move, Rotate, Scale };
    Q_ENUM(Tool)
    enum class TransformSpace { Local, World };
    Q_ENUM(TransformSpace)
    enum class ViewMode { TwoDimensional, RaycastMap, ThreeDimensional };
    Q_ENUM(ViewMode)

    explicit SceneView(QWidget* parent = nullptr);

    void setDocument(SceneDocument* document);
    void setSelectedEntity(const QString& guid);
    void setSelectedEntities(const QStringList& guids);
    void setEditable(bool editable);
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const noexcept;
    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const noexcept;
    void setTransformSpace(TransformSpace space);
    [[nodiscard]] TransformSpace transformSpace() const noexcept;
    void setViewMode(ViewMode mode);
    [[nodiscard]] ViewMode viewMode() const noexcept;
    [[nodiscard]] QString selectedEntityGuid() const;
    [[nodiscard]] QStringList selectedEntityGuids() const;
    [[nodiscard]] float zoomFactor() const noexcept;
    [[nodiscard]] QPointF cameraOffset() const noexcept;

  public slots:
    void frameSelected();
    void zoomIn();
    void zoomOut();

  signals:
    void entitySelected(const QString& guid);
    void entitiesSelected(const QStringList& guids);
    void entityMovePreview(const QString& guid, float x, float y, float z);
    void entityMoveCommitted(const QString& guid, float oldX, float oldY, float oldZ, float newX,
                             float newY, float newZ);
    void entityRotationPreview(const QString& guid, float x, float y, float z);
    void entityRotationCommitted(const QString& guid, float oldX, float oldY, float oldZ,
                                 float newX, float newY, float newZ);
    void entityScalePreview(const QString& guid, float x, float y, float z);
    void entityScaleCommitted(const QString& guid, float oldX, float oldY, float oldZ, float newX,
                              float newY, float newZ);
    void assetDropped(const QString& filePath, float worldX, float worldY, float worldZ);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private:
    [[nodiscard]] QPointF worldToScreen(fabgl::Vec3 position) const;
    [[nodiscard]] QPointF projectWorld(fabgl::Vec3 position) const;
    [[nodiscard]] fabgl::Vec3 screenToWorld(QPointF position,
                                            fabgl::Vec3 referencePosition) const;
    [[nodiscard]] fabgl::Vec3 entityWorldPosition(const fabgl::Entity& entity) const;
    [[nodiscard]] QString pickEntity(QPointF position) const;
    void setZoomAround(float pixelsPerUnit, QPointF anchor);

    SceneDocument* m_document = nullptr;
    QString m_selectedGuid;
    QStringList m_selectedGuids;
    fabgl::Vec3 m_dragStartPosition{};
    fabgl::Vec3 m_dragStartWorldPosition{};
    fabgl::Vec3 m_dragStartRotation{};
    fabgl::Vec3 m_dragStartScale{1.0F, 1.0F, 1.0F};
    QPointF m_interactionStartScreen;
    QPointF m_lastPanScreen;
    QPointF m_cameraOffset;
    Tool m_tool = Tool::Select;
    TransformSpace m_transformSpace = TransformSpace::Local;
    ViewMode m_viewMode = ViewMode::TwoDimensional;
    bool m_editable = true;
    bool m_snapEnabled = true;
    bool m_dragging = false;
    bool m_panning = false;
    bool m_boxSelecting = false;
    QPointF m_boxSelectionStart;
    QPointF m_boxSelectionCurrent;
    float m_pixelsPerUnit = 32.0F;
    float m_snapStep = 0.5F;
};

struct FrameRenderStats final {
    double pcFrameMilliseconds = 0.0;
    std::uint32_t drawCalls = 0;
    std::uint32_t spritesSubmitted = 0;
    std::uint32_t triangles = 0;
    std::uint32_t rays = 0;
    std::uint32_t particles = 0;
};

class GameView final : public QWidget {
    Q_OBJECT

  public:
    enum class AspectMode { Preserve, Stretch, FourThree, SixteenNine };
    Q_ENUM(AspectMode)
    enum class PaletteMode { TrueColor, Esp32Rgb222, Monochrome };
    Q_ENUM(PaletteMode)

    explicit GameView(QWidget* parent = nullptr);

    void setPresentationResources(fabgl::rendering::ScenePresentationResources resources);
    [[nodiscard]] FrameRenderStats renderScene(const fabgl::Scene& scene, double elapsedSeconds,
                                               const fabgl::SceneRuntime* runtime = nullptr);
    void setOverlayText(const QString& text);
    void setTargetResolution(const QSize& resolution);
    [[nodiscard]] QSize targetResolution() const noexcept;
    void setAspectMode(AspectMode mode);
    [[nodiscard]] AspectMode aspectMode() const noexcept;
    void setIntegerScaling(bool enabled);
    [[nodiscard]] bool integerScaling() const noexcept;
    void setPixelPerfect(bool enabled);
    [[nodiscard]] bool pixelPerfect() const noexcept;
    void setPaletteMode(PaletteMode mode);
    [[nodiscard]] PaletteMode paletteMode() const noexcept;
    void setFpsOverlayVisible(bool visible);
    [[nodiscard]] bool fpsOverlayVisible() const noexcept;
    void setTargetFps(int fps);
    [[nodiscard]] int targetFps() const noexcept;
    void setSimulationSpeed(double speed);
    [[nodiscard]] double simulationSpeed() const noexcept;
    void setEsp32SimulationMode(bool enabled);
    [[nodiscard]] bool esp32SimulationMode() const noexcept;
    void setRuntimeInputEnabled(bool enabled);
    [[nodiscard]] bool runtimeInputEnabled() const noexcept;

  signals:
    void runtimeControlChanged(const QString& control, float value);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

  private:
    void uploadFramebuffer();
    void releaseHeldControls();

    fabgl::rendering::Framebuffer m_framebuffer;
    fabgl::rendering::ScenePresentationResources m_presentationResources;
    QImage m_image;
    QString m_overlayText = QStringLiteral("EDIT MODE");
    QElapsedTimer m_presentClock;
    AspectMode m_aspectMode = AspectMode::Preserve;
    PaletteMode m_paletteMode = PaletteMode::TrueColor;
    bool m_integerScaling = true;
    bool m_pixelPerfect = true;
    bool m_showFps = true;
    bool m_esp32Simulation = false;
    bool m_runtimeInputEnabled = false;
    int m_targetFps = 60;
    double m_simulationSpeed = 1.0;
    double m_measuredFps = 0.0;
    QPointF m_lastRuntimeMousePosition;
    QStringList m_heldRuntimeControls;
};

} // namespace fgl::studio
