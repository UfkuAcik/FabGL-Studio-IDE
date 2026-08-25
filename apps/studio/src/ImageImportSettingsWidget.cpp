#include "ImageImportSettingsWidget.h"

#include <project_format.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

namespace fgl::studio {
namespace {

[[nodiscard]] QSpinBox* integerField(QWidget* parent, const char* name, const int minimum,
                                     const int maximum, const int value) {
    auto* field = new QSpinBox(parent);
    field->setObjectName(QString::fromLatin1(name));
    field->setRange(minimum, maximum);
    field->setValue(value);
    return field;
}

[[nodiscard]] QDoubleSpinBox* realField(QWidget* parent, const char* name, const double minimum,
                                        const double maximum, const double value) {
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(QString::fromLatin1(name));
    field->setRange(minimum, maximum);
    field->setDecimals(4);
    field->setSingleStep(0.05);
    field->setValue(value);
    return field;
}

[[nodiscard]] QJsonObject rectangle(const QSpinBox* x, const QSpinBox* y, const QSpinBox* width,
                                    const QSpinBox* height) {
    return {{QStringLiteral("x"), x->value()},
            {QStringLiteral("y"), y->value()},
            {QStringLiteral("width"), width->value()},
            {QStringLiteral("height"), height->value()}};
}

} // namespace

ImageImportSettingsWidget::ImageImportSettingsWidget(QString settingsJson, QWidget* parent)
    : QWidget(parent) {
    auto decoded =
        fabgl::project::decodeProjectImageImportSettings(settingsJson.toUtf8().toStdString());
    fabgl::assets::ImageImportSettings settings;
    if (decoded) {
        settings = decoded.value();
    } else {
        loadError_ = QString::fromStdString(decoded.error().message());
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    if (!loadError_.isEmpty()) {
        auto* warning = new QLabel(tr("Stored settings are invalid: %1").arg(loadError_), this);
        warning->setObjectName(QStringLiteral("imageImportLoadError"));
        warning->setWordWrap(true);
        warning->setStyleSheet(QStringLiteral("color: #ff6b6b; font-weight: bold;"));
        root->addWidget(warning);
    }

    auto* colorGroup = new QGroupBox(tr("Resize, palette and transparency"), this);
    auto* colorForm = new QFormLayout(colorGroup);
    targetWidth_ = integerField(colorGroup, "imageTargetWidth", 0, 4096, settings.targetWidth);
    targetHeight_ = integerField(colorGroup, "imageTargetHeight", 0, 4096, settings.targetHeight);
    targetWidth_->setSpecialValueText(tr("Source"));
    targetHeight_->setSpecialValueText(tr("Source"));
    paletteSize_ = integerField(colorGroup, "imagePaletteSize", 2, 256, settings.paletteSize);
    alphaThreshold_ =
        integerField(colorGroup, "imageAlphaThreshold", 0, 255, settings.alphaThreshold);
    dither_ = new QCheckBox(tr("Floyd–Steinberg dithering"), colorGroup);
    dither_->setObjectName(QStringLiteral("imageDither"));
    dither_->setChecked(settings.dither);
    transparentIndex_ = new QCheckBox(tr("Reserve transparent palette index"), colorGroup);
    transparentIndex_->setObjectName(QStringLiteral("imageTransparentIndex"));
    transparentIndex_->setChecked(settings.reserveTransparentIndex);
    colorForm->addRow(tr("Target width (0 = source)"), targetWidth_);
    colorForm->addRow(tr("Target height (0 = source)"), targetHeight_);
    colorForm->addRow(tr("Palette colors"), paletteSize_);
    colorForm->addRow(tr("Alpha threshold"), alphaThreshold_);
    colorForm->addRow(dither_);
    colorForm->addRow(transparentIndex_);
    root->addWidget(colorGroup);

    auto* cropGroup = new QGroupBox(tr("Crop"), this);
    auto* cropForm = new QFormLayout(cropGroup);
    cropEnabled_ = new QCheckBox(tr("Enable validated crop rectangle"), cropGroup);
    cropEnabled_->setObjectName(QStringLiteral("imageCropEnabled"));
    cropEnabled_->setChecked(settings.cropEnabled);
    cropX_ = integerField(cropGroup, "imageCropX", 0, 8191, settings.crop.x);
    cropY_ = integerField(cropGroup, "imageCropY", 0, 8191, settings.crop.y);
    cropWidth_ = integerField(cropGroup, "imageCropWidth", 1, 8192,
                              settings.cropEnabled ? settings.crop.width : 1);
    cropHeight_ = integerField(cropGroup, "imageCropHeight", 1, 8192,
                               settings.cropEnabled ? settings.crop.height : 1);
    cropForm->addRow(cropEnabled_);
    cropForm->addRow(tr("X"), cropX_);
    cropForm->addRow(tr("Y"), cropY_);
    cropForm->addRow(tr("Width"), cropWidth_);
    cropForm->addRow(tr("Height"), cropHeight_);
    root->addWidget(cropGroup);

    auto* framesGroup = new QGroupBox(tr("Sprite slicing and atlas"), this);
    auto* framesForm = new QFormLayout(framesGroup);
    sliceEnabled_ = new QCheckBox(tr("Slice a uniform grid"), framesGroup);
    sliceEnabled_->setObjectName(QStringLiteral("imageSliceEnabled"));
    sliceEnabled_->setChecked(settings.sliceMode == fabgl::assets::ImageSliceMode::Grid);
    frameWidth_ = integerField(framesGroup, "imageFrameWidth", 1, 4096,
                               settings.frameWidth > 0 ? settings.frameWidth : 1);
    frameHeight_ = integerField(framesGroup, "imageFrameHeight", 1, 4096,
                                settings.frameHeight > 0 ? settings.frameHeight : 1);
    frameMargin_ = integerField(framesGroup, "imageFrameMargin", 0, 4096, settings.frameMargin);
    frameSpacing_ = integerField(framesGroup, "imageFrameSpacing", 0, 4096, settings.frameSpacing);
    atlasEnabled_ = new QCheckBox(tr("Build FGLS sprite atlas"), framesGroup);
    atlasEnabled_->setObjectName(QStringLiteral("imageAtlasEnabled"));
    atlasEnabled_->setChecked(settings.outputKind == fabgl::assets::ImageOutputKind::SpriteAtlas);
    atlasMaximumWidth_ =
        integerField(framesGroup, "imageAtlasMaximumWidth", 1, 4096, settings.atlasMaximumWidth);
    atlasPadding_ = integerField(framesGroup, "imageAtlasPadding", 0, 64, settings.atlasPadding);
    atlasPowerOfTwo_ = new QCheckBox(tr("Power-of-two atlas dimensions"), framesGroup);
    atlasPowerOfTwo_->setObjectName(QStringLiteral("imageAtlasPowerOfTwo"));
    atlasPowerOfTwo_->setChecked(settings.atlasPowerOfTwo);
    framesForm->addRow(sliceEnabled_);
    framesForm->addRow(tr("Frame width"), frameWidth_);
    framesForm->addRow(tr("Frame height"), frameHeight_);
    framesForm->addRow(tr("Outer margin"), frameMargin_);
    framesForm->addRow(tr("Frame spacing"), frameSpacing_);
    framesForm->addRow(atlasEnabled_);
    framesForm->addRow(tr("Atlas maximum width"), atlasMaximumWidth_);
    framesForm->addRow(tr("Atlas padding"), atlasPadding_);
    framesForm->addRow(atlasPowerOfTwo_);
    root->addWidget(framesGroup);

    auto* metadataGroup = new QGroupBox(tr("Runtime metadata"), this);
    auto* metadataForm = new QFormLayout(metadataGroup);
    pivotX_ = realField(metadataGroup, "imagePivotX", 0.0, 1.0, settings.pivotX);
    pivotY_ = realField(metadataGroup, "imagePivotY", 0.0, 1.0, settings.pivotY);
    pixelsPerUnit_ =
        realField(metadataGroup, "imagePixelsPerUnit", 0.0001, 100000.0, settings.pixelsPerUnit);
    compression_ = new QComboBox(metadataGroup);
    compression_->setObjectName(QStringLiteral("imageCompression"));
    compression_->addItem(tr("Indexed RLE (FGLI/FGLS)"), QStringLiteral("rle"));
    residency_ = new QComboBox(metadataGroup);
    residency_->setObjectName(QStringLiteral("imageResidency"));
    residency_->addItem(tr("Preload"), QStringLiteral("preload"));
    residency_->addItem(tr("Stream / bounded decode"), QStringLiteral("stream"));
    residency_->setCurrentIndex(settings.residency == fabgl::assets::ImageResidency::Stream ? 1
                                                                                            : 0);
    metadataForm->addRow(tr("Pivot X"), pivotX_);
    metadataForm->addRow(tr("Pivot Y"), pivotY_);
    metadataForm->addRow(tr("Pixels per unit"), pixelsPerUnit_);
    metadataForm->addRow(tr("Compression"), compression_);
    metadataForm->addRow(tr("Residency"), residency_);
    root->addWidget(metadataGroup);

    connect(cropEnabled_, &QCheckBox::toggled, this, [this] { updateEnabledState(); });
    connect(sliceEnabled_, &QCheckBox::toggled, this, [this](const bool enabled) {
        if (enabled)
            atlasEnabled_->setChecked(true);
        updateEnabledState();
    });
    connect(atlasEnabled_, &QCheckBox::toggled, this, [this](const bool enabled) {
        if (!enabled)
            sliceEnabled_->setChecked(false);
        updateEnabledState();
    });
    updateEnabledState();
}

void ImageImportSettingsWidget::updateEnabledState() {
    for (auto* field : {cropX_, cropY_, cropWidth_, cropHeight_})
        field->setEnabled(cropEnabled_->isChecked());
    for (auto* field : {frameWidth_, frameHeight_, frameMargin_, frameSpacing_})
        field->setEnabled(sliceEnabled_->isChecked());
    atlasMaximumWidth_->setEnabled(atlasEnabled_->isChecked());
    atlasPadding_->setEnabled(atlasEnabled_->isChecked());
    atlasPowerOfTwo_->setEnabled(atlasEnabled_->isChecked());
    targetWidth_->setEnabled(!atlasEnabled_->isChecked());
    targetHeight_->setEnabled(!atlasEnabled_->isChecked());
    if (atlasEnabled_->isChecked()) {
        targetWidth_->setValue(0);
        targetHeight_->setValue(0);
    }
}

fabgl::Result<QString> ImageImportSettingsWidget::settingsJson() const {
    QJsonObject root{
        {QStringLiteral("targetWidth"), targetWidth_->value()},
        {QStringLiteral("targetHeight"), targetHeight_->value()},
        {QStringLiteral("paletteSize"), paletteSize_->value()},
        {QStringLiteral("alphaThreshold"), alphaThreshold_->value()},
        {QStringLiteral("dither"), dither_->isChecked()},
        {QStringLiteral("reserveTransparentIndex"), transparentIndex_->isChecked()},
        {QStringLiteral("pivot"), QJsonObject{{QStringLiteral("x"), pivotX_->value()},
                                              {QStringLiteral("y"), pivotY_->value()}}},
        {QStringLiteral("pixelsPerUnit"), pixelsPerUnit_->value()},
        {QStringLiteral("compression"), compression_->currentData().toString()},
        {QStringLiteral("residency"), residency_->currentData().toString()}};
    if (cropEnabled_->isChecked())
        root.insert(QStringLiteral("crop"), rectangle(cropX_, cropY_, cropWidth_, cropHeight_));
    if (sliceEnabled_->isChecked()) {
        root.insert(QStringLiteral("slice"),
                    QJsonObject{{QStringLiteral("mode"), QStringLiteral("grid")},
                                {QStringLiteral("frameWidth"), frameWidth_->value()},
                                {QStringLiteral("frameHeight"), frameHeight_->value()},
                                {QStringLiteral("margin"), frameMargin_->value()},
                                {QStringLiteral("spacing"), frameSpacing_->value()}});
    }
    QJsonObject atlas{{QStringLiteral("enabled"), atlasEnabled_->isChecked()}};
    if (atlasEnabled_->isChecked()) {
        atlas.insert(QStringLiteral("maxWidth"), atlasMaximumWidth_->value());
        atlas.insert(QStringLiteral("padding"), atlasPadding_->value());
        atlas.insert(QStringLiteral("powerOfTwo"), atlasPowerOfTwo_->isChecked());
    }
    root.insert(QStringLiteral("atlas"), atlas);
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    auto validated = fabgl::project::decodeProjectImageImportSettings(bytes.toStdString());
    if (!validated)
        return fabgl::Result<QString>::failure(validated.error());
    return fabgl::Result<QString>::success(QString::fromUtf8(bytes));
}

QString ImageImportSettingsWidget::loadError() const {
    return loadError_;
}

} // namespace fgl::studio
