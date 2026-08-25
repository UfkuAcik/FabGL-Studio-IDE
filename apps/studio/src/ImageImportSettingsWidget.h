#pragma once

#include <fabgl/core/result.h>

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace fgl::studio {

// Typed editor for the canonical project image-import schema. The widget never
// accepts or emits a partially valid settings object; source-bound validation
// remains in the importer where actual dimensions are available.
class ImageImportSettingsWidget final : public QWidget {
  public:
    explicit ImageImportSettingsWidget(QString settingsJson, QWidget* parent = nullptr);

    [[nodiscard]] fabgl::Result<QString> settingsJson() const;
    [[nodiscard]] QString loadError() const;

  private:
    void updateEnabledState();

    QString loadError_;
    QSpinBox* targetWidth_ = nullptr;
    QSpinBox* targetHeight_ = nullptr;
    QSpinBox* paletteSize_ = nullptr;
    QSpinBox* alphaThreshold_ = nullptr;
    QCheckBox* dither_ = nullptr;
    QCheckBox* transparentIndex_ = nullptr;
    QCheckBox* cropEnabled_ = nullptr;
    QSpinBox* cropX_ = nullptr;
    QSpinBox* cropY_ = nullptr;
    QSpinBox* cropWidth_ = nullptr;
    QSpinBox* cropHeight_ = nullptr;
    QCheckBox* sliceEnabled_ = nullptr;
    QSpinBox* frameWidth_ = nullptr;
    QSpinBox* frameHeight_ = nullptr;
    QSpinBox* frameMargin_ = nullptr;
    QSpinBox* frameSpacing_ = nullptr;
    QCheckBox* atlasEnabled_ = nullptr;
    QSpinBox* atlasMaximumWidth_ = nullptr;
    QSpinBox* atlasPadding_ = nullptr;
    QCheckBox* atlasPowerOfTwo_ = nullptr;
    QDoubleSpinBox* pivotX_ = nullptr;
    QDoubleSpinBox* pivotY_ = nullptr;
    QDoubleSpinBox* pixelsPerUnit_ = nullptr;
    QComboBox* compression_ = nullptr;
    QComboBox* residency_ = nullptr;
};

} // namespace fgl::studio
