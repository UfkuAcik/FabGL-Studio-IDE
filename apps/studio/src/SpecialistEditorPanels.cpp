#include <fabgl/particles/particle_system.h>

#include "SpecialistEditorPanels.h"

#include <fabgl/assets/tilemap_importer.h>
#include <fabgl/rendering/framebuffer.h>
#include <fabgl/rendering/racer_renderer.h>
#include <fabgl/rendering/raycast_renderer.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace fgl::studio {
namespace {

constexpr qint64 MaximumMaterialBytes = 1024LL * 1024LL;
constexpr qint64 MaximumRaycastBytes = 4LL * 1024LL * 1024LL;
constexpr qint64 MaximumTrackBytes = 4LL * 1024LL * 1024LL;
constexpr qint64 MaximumTilemapBytes = 64LL * 1024LL * 1024LL;
constexpr std::uint32_t MaximumEditorTilemapDimension = 1024U;
constexpr std::size_t MaximumEditorTilemapCells = 1'048'576U;
constexpr std::size_t MaximumEditorTilemapLayers = 32U;
constexpr std::size_t MaximumEditorObjects = 4096U;
constexpr std::size_t MaximumEditorChunks = 4096U;
constexpr std::size_t MaximumEditorAnimations = 256U;
constexpr std::size_t MaximumEditorAnimationFrames = 4096U;
constexpr int MaximumEditorRaycastDimension = 128;

[[nodiscard]] QString errorText(const fabgl::Error& error) {
    QString result = QString::fromStdString(error.message());
    for (const auto& item : error.context()) {
        result += QStringLiteral(" [%1=%2]")
                      .arg(QString::fromStdString(item.key), QString::fromStdString(item.value));
    }
    return result;
}

[[nodiscard]] bool readBoundedFile(const QString& path, const qint64 maximumBytes,
                                   QByteArray& output, QString& errorMessage) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        errorMessage = QObject::tr("File does not exist: %1").arg(path);
        return false;
    }
    if (info.size() < 0 || info.size() > maximumBytes) {
        errorMessage =
            QObject::tr("File exceeds the editor byte limit (%1 bytes).").arg(maximumBytes);
        return false;
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = file.errorString();
        return false;
    }
    output = file.read(maximumBytes + 1);
    if (output.size() > maximumBytes) {
        errorMessage = QObject::tr("File grew beyond the editor byte limit while reading.");
        return false;
    }
    return true;
}

[[nodiscard]] bool writeAtomicFile(const QString& path, const QByteArray& bytes,
                                   QString& errorMessage) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        errorMessage =
            QObject::tr("Could not create output directory: %1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        errorMessage = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        errorMessage = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        errorMessage = file.errorString();
        return false;
    }
    return true;
}

void showFramebuffer(QLabel* label, const fabgl::rendering::Framebuffer& framebuffer) {
    QImage image(framebuffer.width(), framebuffer.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < framebuffer.height(); ++y) {
        auto* row = image.scanLine(y);
        for (int x = 0; x < framebuffer.width(); ++x) {
            const auto color = framebuffer.pixel(x, y);
            const auto offset = x * 4;
            row[offset] = color.r;
            row[offset + 1] = color.g;
            row[offset + 2] = color.b;
            row[offset + 3] = color.a;
        }
    }
    label->setPixmap(QPixmap::fromImage(image));
    label->setFixedSize(framebuffer.width(), framebuffer.height());
}

[[nodiscard]] fabgl::Color multiplyColor(const fabgl::Color first,
                                         const fabgl::Color second) noexcept {
    const auto channel = [](const std::uint8_t lhs, const std::uint8_t rhs) {
        return static_cast<std::uint8_t>((static_cast<unsigned>(lhs) * rhs + 127U) / 255U);
    };
    return {channel(first.r, second.r), channel(first.g, second.g), channel(first.b, second.b),
            channel(first.a, second.a)};
}

[[nodiscard]] fabgl::Color lerpColor(const fabgl::Color first, const fabgl::Color second,
                                     const float amount) noexcept {
    const auto channel = [amount](const std::uint8_t lhs, const std::uint8_t rhs) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(static_cast<float>(lhs) +
                                   (static_cast<float>(rhs) - static_cast<float>(lhs)) * amount),
                       0L, 255L));
    };
    return {channel(first.r, second.r), channel(first.g, second.g), channel(first.b, second.b),
            channel(first.a, second.a)};
}

[[nodiscard]] QSpinBox* colorChannel(QWidget* parent, const QString& objectName) {
    auto* result = new QSpinBox(parent);
    result->setObjectName(objectName);
    result->setRange(0, 255);
    return result;
}

[[nodiscard]] QWidget* colorRow(QWidget* parent, const QString& prefix, QSpinBox* (&channels)[4]) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    constexpr std::array names{"Red", "Green", "Blue", "Alpha"};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        channels[index] =
            colorChannel(row, prefix + QString::fromLatin1(names[index]) + QStringLiteral("Spin"));
        layout->addWidget(channels[index]);
    }
    return row;
}

[[nodiscard]] fabgl::Color colorFrom(const QSpinBox* const (&channels)[4]) noexcept {
    return {static_cast<std::uint8_t>(channels[0]->value()),
            static_cast<std::uint8_t>(channels[1]->value()),
            static_cast<std::uint8_t>(channels[2]->value()),
            static_cast<std::uint8_t>(channels[3]->value())};
}

void setColor(QSpinBox* const (&channels)[4], const fabgl::Color color) {
    channels[0]->setValue(color.r);
    channels[1]->setValue(color.g);
    channels[2]->setValue(color.b);
    channels[3]->setValue(color.a);
}

template <typename Enum> void selectEnum(QComboBox* combo, const Enum value) {
    const auto index = combo->findData(static_cast<int>(value));
    if (index >= 0)
        combo->setCurrentIndex(index);
}

template <typename Enum> [[nodiscard]] Enum selectedEnum(const QComboBox* combo) {
    return static_cast<Enum>(combo->currentData().toInt());
}

[[nodiscard]] QTableWidgetItem* item(const QString& text) {
    auto* result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}

[[nodiscard]] bool finiteRect(const fabgl::Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height) && value.width > 0.0F && value.height > 0.0F;
}

} // namespace

MaterialEditorWidget::MaterialEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("materialEditor"));
    m_asset.id = fabgl::AssetGuid::fromStableName("fabgl.studio.material.untitled");
    m_asset.name = "Untitled Material";
    m_asset.material.colorMode = fabgl::MaterialColorMode::Flat;

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* formWidget = new QWidget(this);
    auto* form = new QFormLayout(formWidget);
    m_name = new QLineEdit(formWidget);
    m_name->setObjectName(QStringLiteral("materialNameEdit"));
    form->addRow(tr("Name"), m_name);
    m_backendCombo = new QComboBox(formWidget);
    m_backendCombo->setObjectName(QStringLiteral("materialRendererCombo"));
    m_backendCombo->addItem(tr("2D"), static_cast<int>(fabgl::RendererBackend::Renderer2D));
    m_backendCombo->addItem(tr("Raycast"), static_cast<int>(fabgl::RendererBackend::Raycast));
    m_backendCombo->addItem(tr("Racer"), static_cast<int>(fabgl::RendererBackend::Racer));
    m_backendCombo->addItem(tr("Low Poly"), static_cast<int>(fabgl::RendererBackend::LowPoly));
    form->addRow(tr("Preview renderer"), m_backendCombo);

    const auto makeCombo = [formWidget](const QString& name) {
        auto* combo = new QComboBox(formWidget);
        combo->setObjectName(name);
        return combo;
    };
    m_colorMode = makeCombo(QStringLiteral("materialColorModeCombo"));
    m_colorMode->addItem(tr("Texture"), static_cast<int>(fabgl::MaterialColorMode::Texture));
    m_colorMode->addItem(tr("Flat"), static_cast<int>(fabgl::MaterialColorMode::Flat));
    m_colorMode->addItem(tr("Vertex"), static_cast<int>(fabgl::MaterialColorMode::Vertex));
    form->addRow(tr("Color mode"), m_colorMode);
    m_dither = makeCombo(QStringLiteral("materialDitherCombo"));
    m_dither->addItem(tr("None"), static_cast<int>(fabgl::MaterialDitherMode::None));
    m_dither->addItem(tr("Ordered 2x2"), static_cast<int>(fabgl::MaterialDitherMode::Ordered2x2));
    m_dither->addItem(tr("Ordered 4x4"), static_cast<int>(fabgl::MaterialDitherMode::Ordered4x4));
    form->addRow(tr("Dither"), m_dither);
    m_sampling = makeCombo(QStringLiteral("materialSamplingCombo"));
    m_sampling->addItem(tr("Nearest"), static_cast<int>(fabgl::MaterialSamplingMode::Nearest));
    m_sampling->addItem(tr("Bilinear"), static_cast<int>(fabgl::MaterialSamplingMode::Bilinear));
    form->addRow(tr("Sampling"), m_sampling);
    m_lighting = makeCombo(QStringLiteral("materialLightingCombo"));
    m_lighting->addItem(tr("Unlit"), static_cast<int>(fabgl::MaterialLightingMode::Unlit));
    m_lighting->addItem(tr("Flat"), static_cast<int>(fabgl::MaterialLightingMode::Flat));
    m_lighting->addItem(tr("Vertex"), static_cast<int>(fabgl::MaterialLightingMode::Vertex));
    form->addRow(tr("Lighting"), m_lighting);
    m_blend = makeCombo(QStringLiteral("materialBlendCombo"));
    m_blend->addItem(tr("Opaque"), static_cast<int>(fabgl::MaterialBlendMode::Opaque));
    m_blend->addItem(tr("Alpha"), static_cast<int>(fabgl::MaterialBlendMode::Alpha));
    m_blend->addItem(tr("Additive"), static_cast<int>(fabgl::MaterialBlendMode::Additive));
    m_blend->addItem(tr("Multiply"), static_cast<int>(fabgl::MaterialBlendMode::Multiply));
    form->addRow(tr("Blend"), m_blend);

    QSpinBox* flatChannels[4]{};
    auto* flatRow = colorRow(formWidget, QStringLiteral("materialFlat"), flatChannels);
    m_flatRed = flatChannels[0];
    m_flatGreen = flatChannels[1];
    m_flatBlue = flatChannels[2];
    m_flatAlpha = flatChannels[3];
    form->addRow(tr("Flat RGBA"), flatRow);
    m_emissiveStrength = colorChannel(formWidget, QStringLiteral("materialEmissiveStrengthSpin"));
    form->addRow(tr("Emissive"), m_emissiveStrength);
    m_fog = new QCheckBox(tr("Fog"), formWidget);
    m_fog->setObjectName(QStringLiteral("materialFogCheck"));
    m_billboard = new QCheckBox(tr("Billboard"), formWidget);
    m_billboard->setObjectName(QStringLiteral("materialBillboardCheck"));
    m_doubleSided = new QCheckBox(tr("Double-sided"), formWidget);
    m_doubleSided->setObjectName(QStringLiteral("materialDoubleSidedCheck"));
    auto* flags = new QWidget(formWidget);
    auto* flagLayout = new QHBoxLayout(flags);
    flagLayout->setContentsMargins(0, 0, 0, 0);
    flagLayout->addWidget(m_fog);
    flagLayout->addWidget(m_billboard);
    flagLayout->addWidget(m_doubleSided);
    form->addRow(tr("Flags"), flags);
    auto* compatibility = new QWidget(formWidget);
    auto* compatibilityLayout = new QHBoxLayout(compatibility);
    compatibilityLayout->setContentsMargins(0, 0, 0, 0);
    constexpr std::array rendererNames{"2D", "Ray", "Racer", "LowPoly"};
    for (std::size_t index = 0U; index < rendererNames.size(); ++index) {
        m_rendererChecks[index] =
            new QCheckBox(QString::fromLatin1(rendererNames[index]), compatibility);
        m_rendererChecks[index]->setObjectName(
            QStringLiteral("materialCompatibility%1Check").arg(index));
        compatibilityLayout->addWidget(m_rendererChecks[index]);
    }
    form->addRow(tr("Compatibility"), compatibility);
    root->addWidget(formWidget);

    auto* previewColumn = new QVBoxLayout();
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("materialPreview"));
    previewColumn->addWidget(m_preview, 0, Qt::AlignCenter);
    m_costLabel = new QLabel(this);
    m_costLabel->setObjectName(QStringLiteral("materialCostLabel"));
    previewColumn->addWidget(m_costLabel);
    m_compatibilityLabel = new QLabel(this);
    m_compatibilityLabel->setObjectName(QStringLiteral("materialCompatibilityStatus"));
    previewColumn->addWidget(m_compatibilityLabel);
    m_validation = new QTableWidget(0, 3, this);
    m_validation->setObjectName(QStringLiteral("materialValidationTable"));
    m_validation->setHorizontalHeaderLabels({tr("Severity"), tr("Code"), tr("Message")});
    m_validation->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_validation->setEditTriggers(QAbstractItemView::NoEditTriggers);
    previewColumn->addWidget(m_validation, 1);
    root->addLayout(previewColumn, 1);

    const auto changed = [this]() {
        if (m_updating)
            return;
        applyControlsToAsset();
        setModified(true);
        refreshPreview();
    };
    connect(m_name, &QLineEdit::editingFinished, this, changed);
    for (auto* combo : {m_backendCombo, m_colorMode, m_dither, m_sampling, m_lighting, m_blend})
        connect(combo, &QComboBox::currentIndexChanged, this, [changed](int) { changed(); });
    for (auto* spin : {m_flatRed, m_flatGreen, m_flatBlue, m_flatAlpha, m_emissiveStrength})
        connect(spin, &QSpinBox::valueChanged, this, [changed](int) { changed(); });
    for (auto* check : {m_fog, m_billboard, m_doubleSided})
        connect(check, &QCheckBox::toggled, this, [changed](bool) { changed(); });
    for (auto* check : m_rendererChecks)
        connect(check, &QCheckBox::toggled, this, [changed](bool) { changed(); });
    syncControlsFromAsset();
    refreshPreview();
}

const fabgl::MaterialAsset& MaterialEditorWidget::materialAsset() const noexcept {
    return m_asset;
}

fabgl::RendererBackend MaterialEditorWidget::rendererBackend() const noexcept {
    return m_backend;
}

const fabgl::MaterialCostEstimate& MaterialEditorWidget::costEstimate() const noexcept {
    return m_cost;
}

std::uint64_t MaterialEditorWidget::previewChecksum() const noexcept {
    return m_previewChecksum;
}

qsizetype MaterialEditorWidget::validationIssueCount() const noexcept {
    return m_validationIssues;
}

bool MaterialEditorWidget::validForSelectedRenderer() const noexcept {
    return m_valid;
}

bool MaterialEditorWidget::rendererCompatible() const noexcept {
    return m_compatible;
}

QString MaterialEditorWidget::filePath() const {
    return m_filePath;
}

bool MaterialEditorWidget::modified() const noexcept {
    return m_modified;
}

bool MaterialEditorWidget::setMaterialAsset(fabgl::MaterialAsset asset, QString& errorMessage) {
    auto serialized = fabgl::MaterialSerializer::serialize(asset);
    if (!serialized) {
        errorMessage = errorText(serialized.error());
        return false;
    }
    m_asset = std::move(asset);
    syncControlsFromAsset();
    setModified(true);
    refreshPreview();
    return true;
}

bool MaterialEditorWidget::setFlatColor(const fabgl::Color color, QString& errorMessage) {
    auto candidate = m_asset;
    candidate.material.flatColor = color;
    candidate.material.colorMode = fabgl::MaterialColorMode::Flat;
    return setMaterialAsset(std::move(candidate), errorMessage);
}

void MaterialEditorWidget::setRendererBackend(const fabgl::RendererBackend backend) {
    m_backend = backend;
    m_updating = true;
    selectEnum(m_backendCombo, backend);
    m_updating = false;
    refreshPreview();
}

bool MaterialEditorWidget::openMaterialFile(const QString& filePath, QString& errorMessage) {
    QByteArray bytes;
    if (!readBoundedFile(filePath, MaximumMaterialBytes, bytes, errorMessage))
        return false;
    auto parsed = fabgl::MaterialSerializer::deserialize(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = errorText(parsed.error());
        return false;
    }
    m_asset = std::move(parsed.value());
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    syncControlsFromAsset();
    setModified(false);
    refreshPreview();
    emit statusMessage(tr("Opened material %1.").arg(m_filePath));
    return true;
}

bool MaterialEditorWidget::saveMaterialFile(const QString& filePath, QString& errorMessage) {
    applyControlsToAsset();
    auto serialized = fabgl::MaterialSerializer::serialize(m_asset);
    if (!serialized) {
        errorMessage = errorText(serialized.error());
        return false;
    }
    const QByteArray bytes(serialized.value().data(),
                           static_cast<qsizetype>(serialized.value().size()));
    if (!writeAtomicFile(filePath, bytes, errorMessage))
        return false;
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    setModified(false);
    refreshPreview();
    emit statusMessage(tr("Saved material %1.").arg(m_filePath));
    return true;
}

void MaterialEditorWidget::syncControlsFromAsset() {
    m_updating = true;
    m_name->setText(QString::fromStdString(m_asset.name));
    selectEnum(m_backendCombo, m_backend);
    selectEnum(m_colorMode, m_asset.material.colorMode);
    selectEnum(m_dither, m_asset.material.dither);
    selectEnum(m_sampling, m_asset.material.sampling);
    selectEnum(m_lighting, m_asset.material.lighting);
    selectEnum(m_blend, m_asset.material.blend);
    m_flatRed->setValue(m_asset.material.flatColor.r);
    m_flatGreen->setValue(m_asset.material.flatColor.g);
    m_flatBlue->setValue(m_asset.material.flatColor.b);
    m_flatAlpha->setValue(m_asset.material.flatColor.a);
    m_emissiveStrength->setValue(m_asset.material.emissiveStrength);
    m_fog->setChecked(m_asset.material.participatesInFog);
    m_billboard->setChecked(m_asset.material.billboard);
    m_doubleSided->setChecked(m_asset.material.doubleSided);
    const auto mask = static_cast<std::uint32_t>(m_asset.material.compatibleRenderers);
    for (std::size_t index = 0U; index < 4U; ++index)
        m_rendererChecks[index]->setChecked((mask & (1U << index)) != 0U);
    m_updating = false;
}

void MaterialEditorWidget::applyControlsToAsset() {
    m_asset.name = m_name->text().trimmed().toStdString();
    m_backend = selectedEnum<fabgl::RendererBackend>(m_backendCombo);
    m_asset.material.colorMode = selectedEnum<fabgl::MaterialColorMode>(m_colorMode);
    m_asset.material.dither = selectedEnum<fabgl::MaterialDitherMode>(m_dither);
    m_asset.material.sampling = selectedEnum<fabgl::MaterialSamplingMode>(m_sampling);
    m_asset.material.lighting = selectedEnum<fabgl::MaterialLightingMode>(m_lighting);
    m_asset.material.blend = selectedEnum<fabgl::MaterialBlendMode>(m_blend);
    m_asset.material.flatColor = {static_cast<std::uint8_t>(m_flatRed->value()),
                                  static_cast<std::uint8_t>(m_flatGreen->value()),
                                  static_cast<std::uint8_t>(m_flatBlue->value()),
                                  static_cast<std::uint8_t>(m_flatAlpha->value())};
    m_asset.material.emissiveStrength = static_cast<std::uint8_t>(m_emissiveStrength->value());
    m_asset.material.participatesInFog = m_fog->isChecked();
    m_asset.material.billboard = m_billboard->isChecked();
    m_asset.material.doubleSided = m_doubleSided->isChecked();
    std::uint32_t mask = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
        mask |= m_rendererChecks[index]->isChecked() ? 1U << index : 0U;
    m_asset.material.compatibleRenderers = static_cast<fabgl::RendererCompatibility>(mask);
}

void MaterialEditorWidget::setModified(const bool modified) {
    m_modified = modified;
}

void MaterialEditorWidget::refreshPreview() {
    const auto report = fabgl::validateMaterial(m_asset.material, m_backend);
    m_valid = report.valid();
    const auto backendBit = fabgl::rendererCompatibilityBit(m_backend);
    m_compatible =
        (m_asset.material.compatibleRenderers & backendBit) != fabgl::RendererCompatibility::None;
    m_validation->setRowCount(static_cast<int>(report.issues.size()));
    for (int row = 0; row < static_cast<int>(report.issues.size()); ++row) {
        const auto& issue = report.issues[static_cast<std::size_t>(row)];
        m_validation->setItem(row, 0,
                              item(issue.severity == fabgl::MaterialIssueSeverity::Error
                                       ? tr("Error")
                                       : tr("Warning")));
        m_validation->setItem(row, 1, item(QString::fromStdString(issue.code)));
        m_validation->setItem(row, 2, item(QString::fromStdString(issue.message)));
    }
    m_validationIssues = static_cast<qsizetype>(report.issues.size());
    fabgl::MaterialCostContext context;
    context.renderer = m_backend;
    context.textureWidth = 64U;
    context.textureHeight = 64U;
    context.indexedTexture = !m_asset.material.palette.empty();
    context.sourceBytesPerPixel = context.indexedTexture ? 1U : 4U;
    context.vertexCount = m_backend == fabgl::RendererBackend::LowPoly ? 256U : 4U;
    m_cost = fabgl::estimateMaterialCost(m_asset.material, context);
    m_costLabel->setText(tr("RAM %1 B | Flash %2 B | %3 ops/pixel")
                             .arg(static_cast<qulonglong>(m_cost.persistentRamBytes))
                             .arg(static_cast<qulonglong>(m_cost.flashBytes))
                             .arg(m_cost.operationsPerPixel));
    m_compatibilityLabel->setText(m_compatible ? tr("Compatible with selected renderer")
                                               : tr("Incompatible with selected renderer"));

    fabgl::rendering::Framebuffer framebuffer(160, 96);
    constexpr fabgl::Color dark{28U, 31U, 38U, 255U};
    constexpr fabgl::Color light{54U, 59U, 70U, 255U};
    for (int y = 0; y < framebuffer.height(); ++y) {
        for (int x = 0; x < framebuffer.width(); ++x) {
            framebuffer.setPixel(x, y, ((x / 8 + y / 8) & 1) == 0 ? dark : light);
        }
    }
    for (int y = 12; y < 84; ++y) {
        for (int x = 18; x < 142; ++x) {
            const auto u = static_cast<float>(x - 18) / 123.0F;
            const auto v = static_cast<float>(y - 12) / 71.0F;
            fabgl::Color source = m_asset.material.flatColor;
            if (m_asset.material.colorMode == fabgl::MaterialColorMode::Texture) {
                source = ((x / 12 + y / 12) & 1) == 0 ? fabgl::Color{210U, 220U, 235U, 255U}
                                                      : fabgl::Color{80U, 105U, 145U, 255U};
            } else if (m_asset.material.colorMode == fabgl::MaterialColorMode::Vertex) {
                source = lerpColor({235U, 75U, 75U, 255U}, {70U, 130U, 245U, 255U}, (u + v) * 0.5F);
            }
            source = multiplyColor(source, m_asset.material.tint);
            if (m_asset.material.lighting != fabgl::MaterialLightingMode::Unlit) {
                const auto lighting = std::clamp(0.35F + u * 0.65F, 0.0F, 1.0F);
                source.r = static_cast<std::uint8_t>(source.r * lighting);
                source.g = static_cast<std::uint8_t>(source.g * lighting);
                source.b = static_cast<std::uint8_t>(source.b * lighting);
            }
            const int threshold = m_asset.material.dither == fabgl::MaterialDitherMode::Ordered2x2
                                      ? ((x & 1) + (y & 1) * 2) * 16
                                  : m_asset.material.dither == fabgl::MaterialDitherMode::Ordered4x4
                                      ? ((x & 3) + (y & 3) * 4) * 4
                                      : 0;
            source.r = static_cast<std::uint8_t>(std::max(0, source.r - threshold));
            source.g = static_cast<std::uint8_t>(std::max(0, source.g - threshold));
            source.b = static_cast<std::uint8_t>(std::max(0, source.b - threshold));
            framebuffer.blendPixel(x, y, source);
        }
    }
    const auto glow = static_cast<std::uint8_t>(m_asset.material.emissiveStrength);
    if (glow > 0U) {
        framebuffer.fillRect(18, 78, 124, 6,
                             {m_asset.material.emissive.r, m_asset.material.emissive.g,
                              m_asset.material.emissive.b, glow});
    }
    if (!m_compatible)
        framebuffer.drawLine(18, 12, 141, 83, {255U, 65U, 65U, 255U});
    m_previewChecksum = framebuffer.checksum();
    showFramebuffer(m_preview, framebuffer);
}

ParticleEditorWidget::ParticleEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("particleEditor"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* formWidget = new QWidget(this);
    auto* form = new QFormLayout(formWidget);
    const auto doubleSpin = [formWidget](const QString& name, const double minimum,
                                         const double maximum) {
        auto* spin = new QDoubleSpinBox(formWidget);
        spin->setObjectName(name);
        spin->setRange(minimum, maximum);
        spin->setDecimals(3);
        return spin;
    };
    m_spawnRate = doubleSpin(QStringLiteral("particleSpawnRateSpin"), 0.0, 10000.0);
    m_burst = new QSpinBox(formWidget);
    m_burst->setObjectName(QStringLiteral("particleBurstSpin"));
    m_burst->setRange(0, 4096);
    m_lifetime = doubleSpin(QStringLiteral("particleLifetimeSpin"), 0.01, 60.0);
    m_velocityX = doubleSpin(QStringLiteral("particleVelocityXSpin"), -10000.0, 10000.0);
    m_velocityY = doubleSpin(QStringLiteral("particleVelocityYSpin"), -10000.0, 10000.0);
    m_gravityX = doubleSpin(QStringLiteral("particleGravityXSpin"), -10000.0, 10000.0);
    m_gravityY = doubleSpin(QStringLiteral("particleGravityYSpin"), -10000.0, 10000.0);
    m_startSize = doubleSpin(QStringLiteral("particleStartSizeSpin"), 0.0, 128.0);
    m_endSize = doubleSpin(QStringLiteral("particleEndSizeSpin"), 0.0, 128.0);
    m_startRotation = doubleSpin(QStringLiteral("particleStartRotationSpin"), -36000.0, 36000.0);
    m_endRotation = doubleSpin(QStringLiteral("particleEndRotationSpin"), -36000.0, 36000.0);
    m_maximumParticles = new QSpinBox(formWidget);
    m_maximumParticles->setObjectName(QStringLiteral("particleMaximumSpin"));
    m_maximumParticles->setRange(1, 4096);
    form->addRow(tr("Spawn / second"), m_spawnRate);
    form->addRow(tr("Burst"), m_burst);
    form->addRow(tr("Lifetime"), m_lifetime);
    form->addRow(tr("Velocity X"), m_velocityX);
    form->addRow(tr("Velocity Y"), m_velocityY);
    form->addRow(tr("Gravity X"), m_gravityX);
    form->addRow(tr("Gravity Y"), m_gravityY);
    form->addRow(tr("Start color"),
                 colorRow(formWidget, QStringLiteral("particleStartColor"), m_startColor));
    form->addRow(tr("End color"),
                 colorRow(formWidget, QStringLiteral("particleEndColor"), m_endColor));
    form->addRow(tr("Start size"), m_startSize);
    form->addRow(tr("End size"), m_endSize);
    form->addRow(tr("Start rotation"), m_startRotation);
    form->addRow(tr("End rotation"), m_endRotation);
    form->addRow(tr("Maximum"), m_maximumParticles);
    root->addWidget(formWidget);
    auto* previewColumn = new QVBoxLayout();
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("particlePreview"));
    previewColumn->addWidget(m_preview, 0, Qt::AlignCenter);
    m_costLabel = new QLabel(this);
    m_costLabel->setObjectName(QStringLiteral("particleCostLabel"));
    previewColumn->addWidget(m_costLabel);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("particleValidationStatus"));
    previewColumn->addWidget(m_status);
    previewColumn->addStretch();
    root->addLayout(previewColumn, 1);

    const auto changed = [this]() {
        if (m_updating)
            return;
        applyControlsToSettings();
        refreshPreview();
    };
    for (auto* spin : {m_spawnRate, m_lifetime, m_velocityX, m_velocityY, m_gravityX, m_gravityY,
                       m_startSize, m_endSize, m_startRotation, m_endRotation}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [changed](double) { changed(); });
    }
    for (auto* spin : {m_burst, m_maximumParticles})
        connect(spin, &QSpinBox::valueChanged, this, [changed](int) { changed(); });
    for (auto* spin : m_startColor)
        connect(spin, &QSpinBox::valueChanged, this, [changed](int) { changed(); });
    for (auto* spin : m_endColor)
        connect(spin, &QSpinBox::valueChanged, this, [changed](int) { changed(); });
    syncControlsFromSettings();
    refreshPreview();
}

const ParticleAuthoringSettings& ParticleEditorWidget::settings() const noexcept {
    return m_settings;
}

bool ParticleEditorWidget::setSettings(ParticleAuthoringSettings settings, QString& errorMessage) {
    if (!std::isfinite(settings.spawnRate) || settings.spawnRate < 0.0F ||
        settings.spawnRate > 10000.0F || settings.maximumParticles == 0U ||
        settings.maximumParticles > 4096U || settings.burstCount > settings.maximumParticles ||
        !std::isfinite(settings.lifetimeSeconds) || settings.lifetimeSeconds < 0.01F ||
        settings.lifetimeSeconds > 60.0F || !std::isfinite(settings.velocity.x) ||
        !std::isfinite(settings.velocity.y) || !std::isfinite(settings.gravity.x) ||
        !std::isfinite(settings.gravity.y) || !std::isfinite(settings.startSize) ||
        !std::isfinite(settings.endSize) || settings.startSize < 0.0F || settings.endSize < 0.0F ||
        settings.startSize > 128.0F || settings.endSize > 128.0F ||
        !std::isfinite(settings.startRotationDegrees) ||
        !std::isfinite(settings.endRotationDegrees)) {
        errorMessage = tr("Particle authoring settings are outside bounded editor limits.");
        return false;
    }
    fabgl::ParticleSystem system(settings.maximumParticles);
    fabgl::ParticleEmitterSettings emitterSettings;
    emitterSettings.particle.velocity = settings.velocity;
    emitterSettings.particle.acceleration = settings.gravity;
    emitterSettings.particle.color = settings.startColor;
    emitterSettings.particle.size = settings.startSize;
    emitterSettings.particle.lifetimeSeconds = settings.lifetimeSeconds;
    emitterSettings.particle.rotationDegrees = settings.startRotationDegrees;
    emitterSettings.spawnRate = settings.spawnRate;
    emitterSettings.maximumAlive = settings.maximumParticles;
    emitterSettings.overLifetime.colorEnabled = true;
    emitterSettings.overLifetime.endColor = settings.endColor;
    emitterSettings.overLifetime.sizeEnabled = true;
    emitterSettings.overLifetime.endSize = settings.endSize;
    emitterSettings.overLifetime.rotationEnabled = true;
    emitterSettings.overLifetime.endRotationDegrees = settings.endRotationDegrees;
    fabgl::ParticleEmitter emitter(system);
    const auto configured = emitter.setSettings(emitterSettings);
    if (!configured) {
        errorMessage = errorText(configured.error());
        return false;
    }
    m_settings = settings;
    syncControlsFromSettings();
    refreshPreview();
    return true;
}

std::uint64_t ParticleEditorWidget::previewChecksum() const noexcept {
    return m_previewChecksum;
}

std::size_t ParticleEditorWidget::previewParticleCount() const noexcept {
    return m_previewParticleCount;
}

std::size_t ParticleEditorWidget::estimatedRuntimeBytes() const noexcept {
    return m_estimatedRuntimeBytes;
}

bool ParticleEditorWidget::settingsValid() const noexcept {
    return m_valid;
}

void ParticleEditorWidget::syncControlsFromSettings() {
    m_updating = true;
    m_spawnRate->setValue(m_settings.spawnRate);
    m_burst->setValue(static_cast<int>(m_settings.burstCount));
    m_lifetime->setValue(m_settings.lifetimeSeconds);
    m_velocityX->setValue(m_settings.velocity.x);
    m_velocityY->setValue(m_settings.velocity.y);
    m_gravityX->setValue(m_settings.gravity.x);
    m_gravityY->setValue(m_settings.gravity.y);
    setColor(m_startColor, m_settings.startColor);
    setColor(m_endColor, m_settings.endColor);
    m_startSize->setValue(m_settings.startSize);
    m_endSize->setValue(m_settings.endSize);
    m_startRotation->setValue(m_settings.startRotationDegrees);
    m_endRotation->setValue(m_settings.endRotationDegrees);
    m_maximumParticles->setValue(static_cast<int>(m_settings.maximumParticles));
    m_updating = false;
}

void ParticleEditorWidget::applyControlsToSettings() {
    m_settings.spawnRate = static_cast<float>(m_spawnRate->value());
    m_settings.burstCount = static_cast<std::size_t>(m_burst->value());
    m_settings.lifetimeSeconds = static_cast<float>(m_lifetime->value());
    m_settings.velocity = {static_cast<float>(m_velocityX->value()),
                           static_cast<float>(m_velocityY->value())};
    m_settings.gravity = {static_cast<float>(m_gravityX->value()),
                          static_cast<float>(m_gravityY->value())};
    m_settings.startColor = colorFrom(m_startColor);
    m_settings.endColor = colorFrom(m_endColor);
    m_settings.startSize = static_cast<float>(m_startSize->value());
    m_settings.endSize = static_cast<float>(m_endSize->value());
    m_settings.startRotationDegrees = static_cast<float>(m_startRotation->value());
    m_settings.endRotationDegrees = static_cast<float>(m_endRotation->value());
    m_settings.maximumParticles = static_cast<std::size_t>(m_maximumParticles->value());
}

void ParticleEditorWidget::refreshPreview() {
    m_valid = std::isfinite(m_settings.spawnRate) && m_settings.spawnRate >= 0.0F &&
              m_settings.spawnRate <= 10000.0F && m_settings.maximumParticles > 0U &&
              m_settings.maximumParticles <= 4096U &&
              m_settings.burstCount <= m_settings.maximumParticles &&
              std::isfinite(m_settings.lifetimeSeconds) && m_settings.lifetimeSeconds >= 0.01F &&
              m_settings.lifetimeSeconds <= 60.0F && std::isfinite(m_settings.startSize) &&
              std::isfinite(m_settings.endSize) && m_settings.startSize >= 0.0F &&
              m_settings.endSize >= 0.0F;
    if (!m_valid) {
        m_status->setText(tr("Particle settings are outside bounded editor limits."));
        return;
    }

    fabgl::ParticleSystem system(m_settings.maximumParticles);
    fabgl::ParticleEmitterSettings emitterSettings;
    emitterSettings.particle.position = {0.0F, 0.0F};
    emitterSettings.particle.velocity = m_settings.velocity;
    emitterSettings.particle.acceleration = m_settings.gravity;
    emitterSettings.particle.color = m_settings.startColor;
    emitterSettings.particle.size = m_settings.startSize;
    emitterSettings.particle.lifetimeSeconds = m_settings.lifetimeSeconds;
    emitterSettings.particle.rotationDegrees = m_settings.startRotationDegrees;
    emitterSettings.spawnRate = m_settings.spawnRate;
    emitterSettings.maximumAlive = m_settings.maximumParticles;
    emitterSettings.overLifetime.colorEnabled = true;
    emitterSettings.overLifetime.endColor = m_settings.endColor;
    emitterSettings.overLifetime.sizeEnabled = true;
    emitterSettings.overLifetime.endSize = m_settings.endSize;
    emitterSettings.overLifetime.rotationEnabled = true;
    emitterSettings.overLifetime.endRotationDegrees = m_settings.endRotationDegrees;
    fabgl::ParticleEmitter emitter(system);
    auto configured = emitter.setSettings(emitterSettings);
    if (!configured) {
        m_valid = false;
        m_status->setText(errorText(configured.error()));
        return;
    }
    emitter.setPosition({80.0F, 70.0F});
    auto burst = emitter.burst(m_settings.burstCount);
    if (!burst) {
        m_valid = false;
        m_status->setText(errorText(burst.error()));
        return;
    }
    for (int step = 0; step < 30; ++step) {
        auto updated = emitter.update(1.0F / 60.0F);
        if (!updated) {
            m_valid = false;
            m_status->setText(errorText(updated.error()));
            return;
        }
    }

    fabgl::rendering::Framebuffer framebuffer(160, 96);
    framebuffer.clear({17U, 22U, 31U, 255U});
    m_previewParticleCount = system.activeCount();
    for (std::size_t slot = 0U; slot < system.capacity(); ++slot) {
        const auto* particle = system.particleAtSlot(slot);
        if (particle == nullptr)
            continue;
        const auto radius = std::clamp(static_cast<int>(std::lround(particle->size)), 1, 8);
        const auto x = static_cast<int>(std::lround(particle->position.x));
        const auto y = static_cast<int>(std::lround(particle->position.y));
        framebuffer.fillRect(x - radius, y - radius, radius * 2 + 1, radius * 2 + 1,
                             particle->color);
        const auto radians = particle->rotationDegrees * 0.01745329251994329577F;
        framebuffer.drawLine(x, y,
                             x + static_cast<int>(std::cos(radians) * static_cast<float>(radius)),
                             y + static_cast<int>(std::sin(radians) * static_cast<float>(radius)),
                             {255U, 255U, 255U, particle->color.a});
    }
    m_estimatedRuntimeBytes =
        m_settings.maximumParticles * (sizeof(fabgl::Particle) + sizeof(fabgl::ParticleHandle));
    m_previewChecksum = framebuffer.checksum();
    m_costLabel->setText(tr("%1 / %2 active | estimated pool %3 B")
                             .arg(static_cast<qulonglong>(m_previewParticleCount))
                             .arg(static_cast<qulonglong>(m_settings.maximumParticles))
                             .arg(static_cast<qulonglong>(m_estimatedRuntimeBytes)));
    m_status->setText(tr("Valid deterministic emitter preview"));
    showFramebuffer(m_preview, framebuffer);
}

TilemapEditorWidget::TilemapEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("tilemapEditor"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* tableColumn = new QVBoxLayout();
    auto* history = new QHBoxLayout();
    m_undoButton = new QPushButton(tr("Undo"), this);
    m_undoButton->setObjectName(QStringLiteral("tilemapUndoButton"));
    m_redoButton = new QPushButton(tr("Redo"), this);
    m_redoButton->setObjectName(QStringLiteral("tilemapRedoButton"));
    history->addWidget(m_undoButton);
    history->addWidget(m_redoButton);
    history->addStretch();
    tableColumn->addLayout(history);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("tilemapAuthoringTabs"));
    m_layerTable = new QTableWidget(0, 3, tabs);
    m_layerTable->setObjectName(QStringLiteral("tilemapLayerTable"));
    m_layerTable->setHorizontalHeaderLabels({tr("Layer"), tr("Collision"), tr("Non-empty")});
    m_layerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_layerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_layerTable, tr("Layers / Collision"));
    m_objectTable = new QTableWidget(0, 6, tabs);
    m_objectTable->setObjectName(QStringLiteral("tilemapObjectChunkTable"));
    m_objectTable->setHorizontalHeaderLabels(
        {tr("Kind"), tr("ID/Type"), tr("X"), tr("Y"), tr("Width"), tr("Height")});
    m_objectTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_objectTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_objectTable, tr("Objects / Chunks"));
    m_animationTable = new QTableWidget(0, 3, tabs);
    m_animationTable->setObjectName(QStringLiteral("tilemapAnimationTable"));
    m_animationTable->setHorizontalHeaderLabels({tr("Output tile"), tr("Frames"), tr("Duration")});
    m_animationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_animationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_animationTable, tr("Animations"));
    tableColumn->addWidget(tabs, 1);
    root->addLayout(tableColumn, 1);

    auto* tools = new QTabWidget(this);
    tools->setObjectName(QStringLiteral("tilemapAuthoringControls"));
    auto* layerPage = new QWidget(tools);
    auto* layerForm = new QFormLayout(layerPage);
    m_layerName = new QLineEdit(QStringLiteral("Layer"), layerPage);
    m_layerName->setObjectName(QStringLiteral("tilemapLayerNameEdit"));
    m_layerCollision = new QCheckBox(tr("Collision"), layerPage);
    m_layerCollision->setObjectName(QStringLiteral("tilemapLayerCollisionCheck"));
    auto* addLayerButton = new QPushButton(tr("Add Layer"), layerPage);
    addLayerButton->setObjectName(QStringLiteral("tilemapAddLayerButton"));
    layerForm->addRow(tr("Name"), m_layerName);
    layerForm->addRow({}, m_layerCollision);
    layerForm->addRow({}, addLayerButton);
    tools->addTab(layerPage, tr("Layer"));

    auto* paintPage = new QWidget(tools);
    auto* paintForm = new QFormLayout(paintPage);
    m_paintLayer = new QComboBox(paintPage);
    m_paintLayer->setObjectName(QStringLiteral("tilemapPaintLayerCombo"));
    m_paintX = new QSpinBox(paintPage);
    m_paintX->setObjectName(QStringLiteral("tilemapPaintXSpin"));
    m_paintY = new QSpinBox(paintPage);
    m_paintY->setObjectName(QStringLiteral("tilemapPaintYSpin"));
    m_paintTile = new QSpinBox(paintPage);
    m_paintTile->setObjectName(QStringLiteral("tilemapBrushTileSpin"));
    m_paintTile->setRange(0, 65535);
    auto* paintButton = new QPushButton(tr("Paint Cell"), paintPage);
    paintButton->setObjectName(QStringLiteral("tilemapPaintButton"));
    paintForm->addRow(tr("Layer"), m_paintLayer);
    paintForm->addRow(tr("X"), m_paintX);
    paintForm->addRow(tr("Y"), m_paintY);
    paintForm->addRow(tr("Tile brush"), m_paintTile);
    paintForm->addRow({}, paintButton);
    tools->addTab(paintPage, tr("Paint"));

    const auto boundedDouble = [tools](const QString& name) {
        auto* spin = new QDoubleSpinBox(tools);
        spin->setObjectName(name);
        spin->setDecimals(2);
        spin->setSingleStep(0.25);
        return spin;
    };
    auto* objectPage = new QWidget(tools);
    auto* objectForm = new QFormLayout(objectPage);
    m_objectType = new QLineEdit(QStringLiteral("Spawn"), objectPage);
    m_objectType->setObjectName(QStringLiteral("tilemapObjectTypeEdit"));
    m_objectX = boundedDouble(QStringLiteral("tilemapObjectXSpin"));
    m_objectY = boundedDouble(QStringLiteral("tilemapObjectYSpin"));
    m_objectWidth = boundedDouble(QStringLiteral("tilemapObjectWidthSpin"));
    m_objectHeight = boundedDouble(QStringLiteral("tilemapObjectHeightSpin"));
    m_objectWidth->setValue(1.0);
    m_objectHeight->setValue(1.0);
    auto* addObjectButton = new QPushButton(tr("Add Object"), objectPage);
    addObjectButton->setObjectName(QStringLiteral("tilemapAddObjectButton"));
    objectForm->addRow(tr("Type"), m_objectType);
    objectForm->addRow(tr("X"), m_objectX);
    objectForm->addRow(tr("Y"), m_objectY);
    objectForm->addRow(tr("Width"), m_objectWidth);
    objectForm->addRow(tr("Height"), m_objectHeight);
    objectForm->addRow({}, addObjectButton);
    tools->addTab(objectPage, tr("Object"));

    auto* chunkPage = new QWidget(tools);
    auto* chunkForm = new QFormLayout(chunkPage);
    m_chunkX = new QSpinBox(chunkPage);
    m_chunkX->setObjectName(QStringLiteral("tilemapChunkXSpin"));
    m_chunkY = new QSpinBox(chunkPage);
    m_chunkY->setObjectName(QStringLiteral("tilemapChunkYSpin"));
    m_chunkWidth = new QSpinBox(chunkPage);
    m_chunkWidth->setObjectName(QStringLiteral("tilemapChunkWidthSpin"));
    m_chunkHeight = new QSpinBox(chunkPage);
    m_chunkHeight->setObjectName(QStringLiteral("tilemapChunkHeightSpin"));
    m_chunkWidth->setValue(1);
    m_chunkHeight->setValue(1);
    auto* addChunkButton = new QPushButton(tr("Add Chunk"), chunkPage);
    addChunkButton->setObjectName(QStringLiteral("tilemapAddChunkButton"));
    chunkForm->addRow(tr("X"), m_chunkX);
    chunkForm->addRow(tr("Y"), m_chunkY);
    chunkForm->addRow(tr("Width"), m_chunkWidth);
    chunkForm->addRow(tr("Height"), m_chunkHeight);
    chunkForm->addRow({}, addChunkButton);
    tools->addTab(chunkPage, tr("Chunk"));

    auto* animationPage = new QWidget(tools);
    auto* animationForm = new QFormLayout(animationPage);
    m_animationOutput = new QSpinBox(animationPage);
    m_animationOutput->setObjectName(QStringLiteral("tilemapAnimationOutputSpin"));
    m_animationFrame = new QSpinBox(animationPage);
    m_animationFrame->setObjectName(QStringLiteral("tilemapAnimationFrameSpin"));
    m_animationDuration = new QSpinBox(animationPage);
    m_animationDuration->setObjectName(QStringLiteral("tilemapAnimationDurationSpin"));
    for (auto* spin : {m_animationOutput, m_animationFrame})
        spin->setRange(0, 65535);
    m_animationDuration->setRange(1, 60000);
    m_animationDuration->setValue(100);
    auto* addAnimationButton = new QPushButton(tr("Add Animation"), animationPage);
    addAnimationButton->setObjectName(QStringLiteral("tilemapAddAnimationButton"));
    animationForm->addRow(tr("Output tile"), m_animationOutput);
    animationForm->addRow(tr("Frame tile"), m_animationFrame);
    animationForm->addRow(tr("Duration ms"), m_animationDuration);
    animationForm->addRow({}, addAnimationButton);
    tools->addTab(animationPage, tr("Animation"));
    root->addWidget(tools);

    auto* previewColumn = new QVBoxLayout();
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("tilemapPreview"));
    previewColumn->addWidget(m_preview, 0, Qt::AlignCenter);
    m_costLabel = new QLabel(this);
    m_costLabel->setObjectName(QStringLiteral("tilemapCostLabel"));
    previewColumn->addWidget(m_costLabel);
    previewColumn->addStretch();
    root->addLayout(previewColumn);

    const auto report = [this](const bool success, const QString& error,
                               const QString& successMessage) {
        const QString message = success ? successMessage : error;
        if (!message.isEmpty()) {
            emit statusMessage(message);
        }
    };
    connect(m_undoButton, &QPushButton::clicked, this, &TilemapEditorWidget::undo);
    connect(m_redoButton, &QPushButton::clicked, this, &TilemapEditorWidget::redo);
    connect(addLayerButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const bool success = addLayer(m_layerName->text(), m_layerCollision->isChecked(), error);
        report(success, error, tr("Tilemap layer added."));
    });
    connect(paintButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const bool success = paintTile(static_cast<std::size_t>(m_paintLayer->currentIndex()),
                                       static_cast<std::uint32_t>(m_paintX->value()),
                                       static_cast<std::uint32_t>(m_paintY->value()),
                                       static_cast<std::uint32_t>(m_paintTile->value()), error);
        report(success, error, tr("Tilemap cell painted."));
    });
    connect(addObjectButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const bool success = addObject(m_objectType->text(),
                                       {static_cast<float>(m_objectX->value()),
                                        static_cast<float>(m_objectY->value()),
                                        static_cast<float>(m_objectWidth->value()),
                                        static_cast<float>(m_objectHeight->value())},
                                       error);
        report(success, error, tr("Tilemap object added."));
    });
    connect(addChunkButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const bool success = addChunk({static_cast<std::uint32_t>(m_chunkX->value()),
                                       static_cast<std::uint32_t>(m_chunkY->value()),
                                       static_cast<std::uint32_t>(m_chunkWidth->value()),
                                       static_cast<std::uint32_t>(m_chunkHeight->value())},
                                      error);
        report(success, error, tr("Tilemap chunk added."));
    });
    connect(addAnimationButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        TileAnimationModel animation;
        animation.outputTile = static_cast<std::uint32_t>(m_animationOutput->value());
        animation.frames.push_back({static_cast<std::uint32_t>(m_animationFrame->value()),
                                    static_cast<std::uint32_t>(m_animationDuration->value())});
        const bool success = addAnimation(std::move(animation), error);
        report(success, error, tr("Tile animation added."));
    });
    QString ignored;
    static_cast<void>(newMap(16U, 12U, ignored));
}

bool TilemapEditorWidget::newMap(const std::uint32_t widthValue, const std::uint32_t heightValue,
                                 QString& errorMessage) {
    if (widthValue == 0U || heightValue == 0U || widthValue > MaximumEditorTilemapDimension ||
        heightValue > MaximumEditorTilemapDimension ||
        static_cast<std::size_t>(widthValue) >
            MaximumEditorTilemapCells / static_cast<std::size_t>(heightValue)) {
        errorMessage = tr("Tilemap dimensions exceed the bounded editor model.");
        return false;
    }
    m_guid = fabgl::AssetGuid::generate();
    m_width = widthValue;
    m_height = heightValue;
    m_tileWidth = 8U;
    m_tileHeight = 8U;
    TilemapLayerModel ground;
    ground.name = tr("Ground");
    ground.cells.resize(static_cast<std::size_t>(widthValue) * heightValue);
    m_layers = {std::move(ground)};
    m_objects.clear();
    m_chunks.clear();
    m_animations.clear();
    m_tilesets.clear();
    m_nextObjectId = 1U;
    m_undoHistory.clear();
    m_redoHistory.clear();
    refreshTables();
    refreshPreview();
    emit statusMessage(tr("Created %1 x %2 tilemap.").arg(widthValue).arg(heightValue));
    return true;
}

bool TilemapEditorWidget::addLayer(const QString& name, const bool collision,
                                   QString& errorMessage) {
    const auto normalized = name.trimmed();
    if (normalized.isEmpty() || normalized.size() > 128 ||
        m_layers.size() >= MaximumEditorTilemapLayers ||
        std::any_of(m_layers.begin(), m_layers.end(), [&normalized](const auto& layer) {
            return layer.name.compare(normalized, Qt::CaseInsensitive) == 0;
        })) {
        errorMessage = tr("Layer name is empty, repeated, or the layer limit was reached.");
        return false;
    }
    recordUndoPoint();
    TilemapLayerModel layer;
    layer.name = normalized;
    layer.collision = collision;
    layer.kind = collision ? fabgl::assets::TilemapLayerKind::Collision
                           : fabgl::assets::TilemapLayerKind::Tiles;
    layer.cells.resize(static_cast<std::size_t>(m_width) * m_height);
    m_layers.push_back(std::move(layer));
    refreshTables();
    refreshPreview();
    return true;
}

bool TilemapEditorWidget::paintTile(const std::size_t layer, const std::uint32_t x,
                                    const std::uint32_t y, const std::uint32_t tile,
                                    QString& errorMessage) {
    if (layer >= m_layers.size() || x >= m_width || y >= m_height || tile > 65535U) {
        errorMessage = tr("Tile paint request is outside the layer, map, or tile-ID bounds.");
        return false;
    }
    const auto offset = static_cast<std::size_t>(y) * m_width + x;
    if (m_layers[layer].cells[offset] == tile) {
        errorMessage.clear();
        return true;
    }
    recordUndoPoint();
    m_layers[layer].cells[offset] = tile;
    for (auto& chunk : m_chunks) {
        if (chunk.layer != layer || x < chunk.x || y < chunk.y || x >= chunk.x + chunk.width ||
            y >= chunk.y + chunk.height || chunk.cells.empty())
            continue;
        chunk.cells[static_cast<std::size_t>(y - chunk.y) * chunk.width + (x - chunk.x)] = tile;
    }
    refreshTables();
    refreshPreview();
    return true;
}

bool TilemapEditorWidget::addObject(QString type, const fabgl::Rect bounds, QString& errorMessage) {
    type = type.trimmed();
    if (type.isEmpty() || type.size() > 128 || !finiteRect(bounds) || bounds.x < 0.0F ||
        bounds.y < 0.0F || bounds.x + bounds.width > static_cast<float>(m_width) ||
        bounds.y + bounds.height > static_cast<float>(m_height) ||
        m_objects.size() >= MaximumEditorObjects ||
        m_nextObjectId == std::numeric_limits<std::uint32_t>::max()) {
        errorMessage = tr("Tilemap object is invalid or outside bounded map space.");
        return false;
    }
    recordUndoPoint();
    TilemapObjectModel object;
    object.id = m_nextObjectId++;
    object.type = std::move(type);
    object.bounds = bounds;
    m_objects.push_back(std::move(object));
    refreshTables();
    refreshPreview();
    return true;
}

bool TilemapEditorWidget::addChunk(TilemapChunkModel chunk, QString& errorMessage) {
    if (chunk.width == 0U || chunk.height == 0U || chunk.x >= m_width || chunk.y >= m_height ||
        chunk.width > m_width - chunk.x || chunk.height > m_height - chunk.y ||
        chunk.layer >= m_layers.size() || m_chunks.size() >= MaximumEditorChunks) {
        errorMessage = tr("Tilemap chunk is empty, outside the map, or exceeds the chunk limit.");
        return false;
    }
    const auto chunkCellCount = static_cast<std::size_t>(chunk.width) * chunk.height;
    if (!chunk.cells.empty() && chunk.cells.size() != chunkCellCount) {
        errorMessage = tr("Tilemap chunk data does not match its bounded dimensions.");
        return false;
    }
    if (chunk.cells.empty()) {
        chunk.cells.reserve(chunkCellCount);
        for (std::uint32_t y = 0U; y < chunk.height; ++y) {
            const auto begin = m_layers[chunk.layer].cells.begin() +
                               static_cast<std::ptrdiff_t>((chunk.y + y) * m_width + chunk.x);
            chunk.cells.insert(chunk.cells.end(), begin,
                               begin + static_cast<std::ptrdiff_t>(chunk.width));
        }
    }
    recordUndoPoint();
    m_chunks.push_back(chunk);
    refreshTables();
    refreshPreview();
    return true;
}

bool TilemapEditorWidget::addAnimation(TileAnimationModel animation, QString& errorMessage) {
    const auto existingFrames =
        std::accumulate(m_animations.begin(), m_animations.end(), std::size_t{0U},
                        [](const std::size_t count, const TileAnimationModel& value) {
                            return count + value.frames.size();
                        });
    if (animation.outputTile > 65535U || animation.frames.empty() ||
        m_animations.size() >= MaximumEditorAnimations ||
        animation.frames.size() > MaximumEditorAnimationFrames - existingFrames ||
        std::any_of(animation.frames.begin(), animation.frames.end(), [](const auto& frame) {
            return frame.tile > 65535U || frame.durationMilliseconds == 0U ||
                   frame.durationMilliseconds > 60'000U;
        })) {
        errorMessage = tr("Tile animation is empty or exceeds bounded frame/duration limits.");
        return false;
    }
    recordUndoPoint();
    m_animations.push_back(std::move(animation));
    refreshTables();
    refreshPreview();
    return true;
}

bool TilemapEditorWidget::importTilemapFile(const QString& filePath, QString& errorMessage) {
    QByteArray bytes;
    if (!readBoundedFile(filePath, MaximumTilemapBytes, bytes, errorMessage))
        return false;
    fabgl::Result<fabgl::assets::Tilemap> imported = fabgl::Result<fabgl::assets::Tilemap>::failure(
        fabgl::Error(fabgl::ErrorCode::InvalidFormat, "unsupported tilemap source"));
    const auto suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("csv")) {
        imported = fabgl::assets::importCsvTilemap(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    } else if (suffix == QStringLiteral("json")) {
        imported = fabgl::assets::importJsonTilemap(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    } else if (suffix == QStringLiteral("fglt") || suffix == QStringLiteral("fgltilemap")) {
        imported =
            fabgl::assets::inspectTilemap(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    } else {
        errorMessage = tr("Tilemap input must use .csv, .json, .fglt, or .fgltilemap.");
        return false;
    }
    if (!imported) {
        errorMessage = errorText(imported.error());
        return false;
    }
    const auto& source = imported.value();
    if (source.width == 0U || source.height == 0U || source.width > MaximumEditorTilemapDimension ||
        source.height > MaximumEditorTilemapDimension ||
        static_cast<std::size_t>(source.width) >
            MaximumEditorTilemapCells / static_cast<std::size_t>(source.height) ||
        source.layers.size() > MaximumEditorTilemapLayers ||
        source.objects.size() > MaximumEditorObjects ||
        source.chunks.size() > MaximumEditorChunks ||
        source.animations.size() > MaximumEditorAnimations) {
        errorMessage = tr("Tilemap exceeds the bounded editor authoring model.");
        return false;
    }
    m_guid = source.guid.isNil() ? fabgl::AssetGuid::generate() : source.guid;
    m_width = source.width;
    m_height = source.height;
    m_tileWidth = source.tileWidth;
    m_tileHeight = source.tileHeight;
    m_layers.clear();
    if (source.layers.empty()) {
        TilemapLayerModel layer;
        layer.name = tr("Ground");
        layer.cells = source.tiles;
        m_layers.push_back(std::move(layer));
    } else {
        m_layers.reserve(source.layers.size());
        for (const auto& sourceLayer : source.layers) {
            TilemapLayerModel layer;
            layer.name = QString::fromUtf8(sourceLayer.name.data(),
                                           static_cast<qsizetype>(sourceLayer.name.size()));
            layer.collision = sourceLayer.kind == fabgl::assets::TilemapLayerKind::Collision;
            layer.kind = sourceLayer.kind;
            layer.cells = sourceLayer.cells;
            layer.visible = sourceLayer.visible;
            layer.opacity = sourceLayer.opacity;
            layer.parallaxX = sourceLayer.parallaxX;
            layer.parallaxY = sourceLayer.parallaxY;
            m_layers.push_back(std::move(layer));
        }
    }
    m_objects.clear();
    m_objects.reserve(source.objects.size());
    std::uint32_t highestObjectId = 0U;
    for (const auto& sourceObject : source.objects) {
        TilemapObjectModel object;
        object.id = sourceObject.id;
        object.type = QString::fromUtf8(sourceObject.type.data(),
                                        static_cast<qsizetype>(sourceObject.type.size()));
        object.bounds = sourceObject.bounds;
        object.layer = sourceObject.layer;
        object.asset = sourceObject.asset;
        highestObjectId = std::max(highestObjectId, object.id);
        m_objects.push_back(std::move(object));
    }
    m_chunks.clear();
    m_chunks.reserve(source.chunks.size());
    for (const auto& sourceChunk : source.chunks) {
        TilemapChunkModel chunk;
        chunk.x = sourceChunk.x;
        chunk.y = sourceChunk.y;
        chunk.width = sourceChunk.width;
        chunk.height = sourceChunk.height;
        chunk.layer = sourceChunk.layer;
        chunk.cells = sourceChunk.cells;
        m_chunks.push_back(std::move(chunk));
    }
    m_animations.clear();
    m_animations.reserve(source.animations.size());
    for (const auto& sourceAnimation : source.animations) {
        TileAnimationModel animation;
        animation.outputTile = sourceAnimation.outputTile;
        animation.frames.reserve(sourceAnimation.frames.size());
        for (const auto& sourceFrame : sourceAnimation.frames)
            animation.frames.push_back({sourceFrame.tile, sourceFrame.durationMilliseconds});
        m_animations.push_back(std::move(animation));
    }
    m_tilesets = source.tilesets;
    m_nextObjectId = highestObjectId == std::numeric_limits<std::uint32_t>::max()
                         ? highestObjectId
                         : highestObjectId + 1U;
    m_undoHistory.clear();
    m_redoHistory.clear();
    refreshTables();
    refreshPreview();
    emit statusMessage(tr("Imported tilemap %1 through the asset pipeline.")
                           .arg(QFileInfo(filePath).absoluteFilePath()));
    return true;
}

bool TilemapEditorWidget::exportTilemapFile(const QString& filePath, const std::size_t layer,
                                            QString& errorMessage) const {
    if (QFileInfo(filePath).suffix().compare(QStringLiteral("fgltilemap"), Qt::CaseInsensitive) !=
        0) {
        errorMessage = tr("New tilemaps must use the canonical .fgltilemap extension.");
        return false;
    }
    if (layer >= m_layers.size()) {
        errorMessage = tr("Tilemap export layer is out of range.");
        return false;
    }
    fabgl::assets::Tilemap tilemap;
    tilemap.width = m_width;
    tilemap.height = m_height;
    tilemap.tiles = m_layers.front().cells;
    tilemap.guid = m_guid;
    tilemap.tileWidth = m_tileWidth;
    tilemap.tileHeight = m_tileHeight;
    tilemap.tilesets = m_tilesets;
    tilemap.layers.reserve(m_layers.size());
    for (const auto& sourceLayer : m_layers) {
        fabgl::assets::TilemapLayer targetLayer;
        const auto name = sourceLayer.name.toUtf8();
        targetLayer.name.assign(name.constData(), static_cast<std::size_t>(name.size()));
        targetLayer.kind = sourceLayer.kind;
        targetLayer.cells = sourceLayer.cells;
        targetLayer.parallaxX = sourceLayer.parallaxX;
        targetLayer.parallaxY = sourceLayer.parallaxY;
        targetLayer.opacity = sourceLayer.opacity;
        targetLayer.visible = sourceLayer.visible;
        tilemap.layers.push_back(std::move(targetLayer));
    }
    tilemap.objects.reserve(m_objects.size());
    for (const auto& sourceObject : m_objects) {
        fabgl::assets::TilemapObject targetObject;
        targetObject.id = sourceObject.id;
        targetObject.layer = sourceObject.layer;
        const auto type = sourceObject.type.toUtf8();
        targetObject.type.assign(type.constData(), static_cast<std::size_t>(type.size()));
        targetObject.bounds = sourceObject.bounds;
        targetObject.asset = sourceObject.asset;
        tilemap.objects.push_back(std::move(targetObject));
    }
    tilemap.chunks.reserve(m_chunks.size());
    for (const auto& sourceChunk : m_chunks) {
        fabgl::assets::TilemapChunk targetChunk;
        targetChunk.layer = sourceChunk.layer;
        targetChunk.x = sourceChunk.x;
        targetChunk.y = sourceChunk.y;
        targetChunk.width = sourceChunk.width;
        targetChunk.height = sourceChunk.height;
        targetChunk.cells = sourceChunk.cells;
        tilemap.chunks.push_back(std::move(targetChunk));
    }
    tilemap.animations.reserve(m_animations.size());
    for (const auto& sourceAnimation : m_animations) {
        fabgl::assets::TileAnimation targetAnimation;
        targetAnimation.outputTile = sourceAnimation.outputTile;
        targetAnimation.frames.reserve(sourceAnimation.frames.size());
        for (const auto& sourceFrame : sourceAnimation.frames) {
            targetAnimation.frames.push_back({sourceFrame.tile, sourceFrame.durationMilliseconds});
        }
        tilemap.animations.push_back(std::move(targetAnimation));
    }
    auto encoded = fabgl::assets::encodeTilemap(tilemap);
    if (!encoded) {
        errorMessage = errorText(encoded.error());
        return false;
    }
    const QByteArray bytes(reinterpret_cast<const char*>(encoded.value().data()),
                           static_cast<qsizetype>(encoded.value().size()));
    return writeAtomicFile(filePath, bytes, errorMessage);
}

std::uint32_t TilemapEditorWidget::width() const noexcept {
    return m_width;
}

std::uint32_t TilemapEditorWidget::height() const noexcept {
    return m_height;
}

qsizetype TilemapEditorWidget::layerCount() const noexcept {
    return static_cast<qsizetype>(m_layers.size());
}

qsizetype TilemapEditorWidget::collisionLayerCount() const noexcept {
    return static_cast<qsizetype>(std::count_if(m_layers.begin(), m_layers.end(),
                                                [](const auto& layer) { return layer.collision; }));
}

qsizetype TilemapEditorWidget::objectCount() const noexcept {
    return static_cast<qsizetype>(m_objects.size());
}

qsizetype TilemapEditorWidget::chunkCount() const noexcept {
    return static_cast<qsizetype>(m_chunks.size());
}

qsizetype TilemapEditorWidget::animationCount() const noexcept {
    return static_cast<qsizetype>(m_animations.size());
}

std::uint32_t TilemapEditorWidget::tileAt(const std::size_t layer, const std::uint32_t x,
                                          const std::uint32_t y) const noexcept {
    if (layer >= m_layers.size() || x >= m_width || y >= m_height)
        return 0U;
    return m_layers[layer].cells[static_cast<std::size_t>(y) * m_width + x];
}

std::size_t TilemapEditorWidget::estimatedRuntimeBytes() const noexcept {
    return m_estimatedRuntimeBytes;
}

std::uint64_t TilemapEditorWidget::previewChecksum() const noexcept {
    return m_previewChecksum;
}

bool TilemapEditorWidget::canUndo() const noexcept {
    return !m_undoHistory.empty();
}

bool TilemapEditorWidget::canRedo() const noexcept {
    return !m_redoHistory.empty();
}

TilemapEditorWidget::Snapshot TilemapEditorWidget::snapshot() const {
    Snapshot result;
    result.guid = m_guid;
    result.width = m_width;
    result.height = m_height;
    result.tileWidth = m_tileWidth;
    result.tileHeight = m_tileHeight;
    result.layers = m_layers;
    result.objects = m_objects;
    result.chunks = m_chunks;
    result.animations = m_animations;
    result.tilesets = m_tilesets;
    result.nextObjectId = m_nextObjectId;
    return result;
}

void TilemapEditorWidget::restoreSnapshot(Snapshot value) {
    m_guid = value.guid;
    m_width = value.width;
    m_height = value.height;
    m_tileWidth = value.tileWidth;
    m_tileHeight = value.tileHeight;
    m_layers = std::move(value.layers);
    m_objects = std::move(value.objects);
    m_chunks = std::move(value.chunks);
    m_animations = std::move(value.animations);
    m_tilesets = std::move(value.tilesets);
    m_nextObjectId = value.nextObjectId;
    refreshTables();
    refreshPreview();
}

void TilemapEditorWidget::recordUndoPoint() {
    constexpr std::size_t MaximumHistory = 100U;
    if (m_undoHistory.size() >= MaximumHistory)
        m_undoHistory.erase(m_undoHistory.begin());
    m_undoHistory.push_back(snapshot());
    m_redoHistory.clear();
    updateHistoryActions();
}

void TilemapEditorWidget::undo() {
    if (m_undoHistory.empty())
        return;
    m_redoHistory.push_back(snapshot());
    auto value = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();
    restoreSnapshot(std::move(value));
    updateHistoryActions();
    emit statusMessage(tr("Tilemap edit undone."));
}

void TilemapEditorWidget::redo() {
    if (m_redoHistory.empty())
        return;
    m_undoHistory.push_back(snapshot());
    auto value = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();
    restoreSnapshot(std::move(value));
    updateHistoryActions();
    emit statusMessage(tr("Tilemap edit redone."));
}

void TilemapEditorWidget::updateHistoryActions() {
    if (m_undoButton != nullptr)
        m_undoButton->setEnabled(canUndo());
    if (m_redoButton != nullptr)
        m_redoButton->setEnabled(canRedo());
}

void TilemapEditorWidget::refreshAuthoringControls() {
    if (m_paintLayer == nullptr)
        return;
    const QSignalBlocker blocker(m_paintLayer);
    const int previousLayer = m_paintLayer->currentIndex();
    m_paintLayer->clear();
    for (const auto& layer : m_layers)
        m_paintLayer->addItem(layer.name);
    if (m_paintLayer->count() > 0)
        m_paintLayer->setCurrentIndex(std::clamp(previousLayer, 0, m_paintLayer->count() - 1));
    const int maximumX = m_width == 0U ? 0 : static_cast<int>(m_width - 1U);
    const int maximumY = m_height == 0U ? 0 : static_cast<int>(m_height - 1U);
    for (auto* spin : {m_paintX, m_chunkX})
        spin->setRange(0, maximumX);
    for (auto* spin : {m_paintY, m_chunkY})
        spin->setRange(0, maximumY);
    m_chunkWidth->setRange(1, std::max(1, static_cast<int>(m_width)));
    m_chunkHeight->setRange(1, std::max(1, static_cast<int>(m_height)));
    for (auto* spin : {m_objectX, m_objectWidth})
        spin->setRange(spin == m_objectWidth ? 0.01 : 0.0,
                       std::max(0.01, static_cast<double>(m_width)));
    for (auto* spin : {m_objectY, m_objectHeight})
        spin->setRange(spin == m_objectHeight ? 0.01 : 0.0,
                       std::max(0.01, static_cast<double>(m_height)));
    updateHistoryActions();
}

void TilemapEditorWidget::refreshTables() {
    m_layerTable->setRowCount(static_cast<int>(m_layers.size()));
    for (int row = 0; row < static_cast<int>(m_layers.size()); ++row) {
        const auto& layer = m_layers[static_cast<std::size_t>(row)];
        m_layerTable->setItem(row, 0, item(layer.name));
        m_layerTable->setItem(row, 1, item(layer.collision ? tr("Yes") : tr("No")));
        m_layerTable->setItem(
            row, 2,
            item(QString::number(std::count_if(layer.cells.begin(), layer.cells.end(),
                                               [](const auto tile) { return tile != 0U; }))));
    }
    m_objectTable->setRowCount(static_cast<int>(m_objects.size() + m_chunks.size()));
    int row = 0;
    for (const auto& object : m_objects) {
        m_objectTable->setItem(row, 0, item(tr("Object")));
        m_objectTable->setItem(row, 1,
                               item(QStringLiteral("%1: %2").arg(object.id).arg(object.type)));
        m_objectTable->setItem(row, 2, item(QString::number(object.bounds.x)));
        m_objectTable->setItem(row, 3, item(QString::number(object.bounds.y)));
        m_objectTable->setItem(row, 4, item(QString::number(object.bounds.width)));
        m_objectTable->setItem(row, 5, item(QString::number(object.bounds.height)));
        ++row;
    }
    for (const auto& chunk : m_chunks) {
        m_objectTable->setItem(row, 0, item(tr("Chunk")));
        m_objectTable->setItem(row, 1, item(QString::number(row)));
        m_objectTable->setItem(row, 2, item(QString::number(chunk.x)));
        m_objectTable->setItem(row, 3, item(QString::number(chunk.y)));
        m_objectTable->setItem(row, 4, item(QString::number(chunk.width)));
        m_objectTable->setItem(row, 5, item(QString::number(chunk.height)));
        ++row;
    }
    m_animationTable->setRowCount(static_cast<int>(m_animations.size()));
    for (int animationRow = 0; animationRow < static_cast<int>(m_animations.size());
         ++animationRow) {
        const auto& animation = m_animations[static_cast<std::size_t>(animationRow)];
        const auto duration =
            std::accumulate(animation.frames.begin(), animation.frames.end(), std::uint64_t{0U},
                            [](const std::uint64_t total, const auto& frame) {
                                return total + frame.durationMilliseconds;
                            });
        m_animationTable->setItem(animationRow, 0, item(QString::number(animation.outputTile)));
        m_animationTable->setItem(animationRow, 1, item(QString::number(animation.frames.size())));
        m_animationTable->setItem(animationRow, 2,
                                  item(tr("%1 ms").arg(static_cast<qulonglong>(duration))));
    }
    refreshAuthoringControls();
}

void TilemapEditorWidget::refreshPreview() {
    fabgl::rendering::Framebuffer framebuffer(160, 96);
    framebuffer.clear({17U, 22U, 31U, 255U});
    if (m_width == 0U || m_height == 0U || m_layers.empty()) {
        m_previewChecksum = framebuffer.checksum();
        showFramebuffer(m_preview, framebuffer);
        return;
    }
    for (int py = 0; py < framebuffer.height(); ++py) {
        const auto y =
            std::min(m_height - 1U,
                     static_cast<std::uint32_t>((static_cast<std::uint64_t>(py) * m_height) /
                                                static_cast<std::uint32_t>(framebuffer.height())));
        for (int px = 0; px < framebuffer.width(); ++px) {
            const auto x = std::min(
                m_width - 1U,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(px) * m_width) /
                                           static_cast<std::uint32_t>(framebuffer.width())));
            std::uint32_t tile = 0U;
            bool collision = false;
            for (const auto& layer : m_layers) {
                const auto value = layer.cells[static_cast<std::size_t>(y) * m_width + x];
                if (value != 0U)
                    tile = value;
                collision = collision || (layer.collision && value != 0U);
            }
            if (tile != 0U) {
                const auto value = static_cast<std::uint8_t>(tile & 0xFFU);
                framebuffer.setPixel(px, py,
                                     {static_cast<std::uint8_t>(45U + value % 180U),
                                      static_cast<std::uint8_t>(60U + value * 3U % 170U),
                                      static_cast<std::uint8_t>(75U + value * 7U % 160U), 255U});
            }
            if (collision && ((px + py) & 3) == 0)
                framebuffer.blendPixel(px, py, {255U, 65U, 65U, 150U});
        }
    }
    const auto mapX = [this, &framebuffer](const float x) {
        return static_cast<int>(x / static_cast<float>(m_width) *
                                static_cast<float>(framebuffer.width()));
    };
    const auto mapY = [this, &framebuffer](const float y) {
        return static_cast<int>(y / static_cast<float>(m_height) *
                                static_cast<float>(framebuffer.height()));
    };
    for (const auto& object : m_objects) {
        const auto left = mapX(object.bounds.x);
        const auto top = mapY(object.bounds.y);
        const auto right = mapX(object.bounds.x + object.bounds.width) - 1;
        const auto bottom = mapY(object.bounds.y + object.bounds.height) - 1;
        framebuffer.drawLine(left, top, right, top, {255U, 210U, 70U, 255U});
        framebuffer.drawLine(left, bottom, right, bottom, {255U, 210U, 70U, 255U});
        framebuffer.drawLine(left, top, left, bottom, {255U, 210U, 70U, 255U});
        framebuffer.drawLine(right, top, right, bottom, {255U, 210U, 70U, 255U});
    }
    for (const auto& chunk : m_chunks) {
        const auto left = mapX(static_cast<float>(chunk.x));
        const auto top = mapY(static_cast<float>(chunk.y));
        const auto right = mapX(static_cast<float>(chunk.x + chunk.width)) - 1;
        const auto bottom = mapY(static_cast<float>(chunk.y + chunk.height)) - 1;
        framebuffer.drawLine(left, top, right, top, {70U, 220U, 245U, 255U});
        framebuffer.drawLine(left, bottom, right, bottom, {70U, 220U, 245U, 255U});
    }
    const auto animationFrames =
        std::accumulate(m_animations.begin(), m_animations.end(), std::size_t{0U},
                        [](const std::size_t count, const auto& animation) {
                            return count + animation.frames.size();
                        });
    m_estimatedRuntimeBytes =
        m_layers.size() * static_cast<std::size_t>(m_width) * m_height * sizeof(std::uint32_t) +
        m_objects.size() * sizeof(TilemapObjectModel) +
        m_chunks.size() * sizeof(TilemapChunkModel) +
        animationFrames * sizeof(TileAnimationFrameModel);
    m_costLabel->setText(tr("%1 x %2 | %3 layers | estimated %4 B")
                             .arg(m_width)
                             .arg(m_height)
                             .arg(m_layers.size())
                             .arg(static_cast<qulonglong>(m_estimatedRuntimeBytes)));
    m_previewChecksum = framebuffer.checksum();
    showFramebuffer(m_preview, framebuffer);
}

RaycastMapEditorWidget::RaycastMapEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("raycastMapEditor"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* gridColumn = new QVBoxLayout();
    m_brush = new QComboBox(this);
    m_brush->setObjectName(QStringLiteral("raycastCellBrushCombo"));
    m_brush->addItem(tr("Empty"), static_cast<int>(RaycastCellType::Empty));
    m_brush->addItem(tr("Wall"), static_cast<int>(RaycastCellType::Wall));
    m_brush->addItem(tr("Door"), static_cast<int>(RaycastCellType::Door));
    m_brush->addItem(tr("Secret"), static_cast<int>(RaycastCellType::Secret));
    m_brush->addItem(tr("Light"), static_cast<int>(RaycastCellType::Light));
    gridColumn->addWidget(m_brush);
    m_grid = new QTableWidget(this);
    m_grid->setObjectName(QStringLiteral("raycastPaintGrid"));
    m_grid->horizontalHeader()->setDefaultSectionSize(22);
    m_grid->verticalHeader()->setDefaultSectionSize(22);
    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    gridColumn->addWidget(m_grid, 1);
    root->addLayout(gridColumn, 1);
    auto* previewColumn = new QVBoxLayout();
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("raycastMapPreview"));
    previewColumn->addWidget(m_preview, 0, Qt::AlignCenter);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("raycastMapValidationStatus"));
    previewColumn->addWidget(m_status);
    previewColumn->addStretch();
    root->addLayout(previewColumn);
    connect(m_grid, &QTableWidget::cellClicked, this, [this](const int row, const int column) {
        QString error;
        const auto type = static_cast<RaycastCellType>(m_brush->currentData().toInt());
        if (!paintCell(column, row, type, error))
            m_status->setText(error);
    });
    QString ignored;
    static_cast<void>(newMap(12, 12, ignored));
}

bool RaycastMapEditorWidget::newMap(const int widthValue, const int heightValue,
                                    QString& errorMessage) {
    if (widthValue < 3 || heightValue < 3 || widthValue > MaximumEditorRaycastDimension ||
        heightValue > MaximumEditorRaycastDimension ||
        static_cast<std::size_t>(widthValue) >
            static_cast<std::size_t>(MaximumEditorRaycastDimension) *
                MaximumEditorRaycastDimension / static_cast<std::size_t>(heightValue)) {
        errorMessage = tr("Raycast map dimensions exceed the bounded editor model.");
        return false;
    }
    m_asset.guid = fabgl::AssetGuid::fromStableName("fabgl.studio.raycast.untitled");
    m_asset.map.width = widthValue;
    m_asset.map.height = heightValue;
    const auto widthSize = static_cast<std::size_t>(widthValue);
    const auto heightSize = static_cast<std::size_t>(heightValue);
    m_asset.map.cells.assign(widthSize * heightSize, 0U);
    m_asset.map.wallPalette = {{25U, 30U, 40U, 255U},
                               {170U, 175U, 190U, 255U},
                               {210U, 135U, 55U, 255U},
                               {120U, 70U, 155U, 255U},
                               {245U, 225U, 95U, 255U}};
    for (int y = 0; y < heightValue; ++y) {
        for (int x = 0; x < widthValue; ++x) {
            if (x == 0 || y == 0 || x == widthValue - 1 || y == heightValue - 1)
                m_asset.map
                    .cells[static_cast<std::size_t>(y) * widthSize + static_cast<std::size_t>(x)] =
                    1U;
        }
    }
    m_filePath.clear();
    refreshGrid();
    refreshPreview();
    return m_valid;
}

bool RaycastMapEditorWidget::paintCell(const int x, const int y, const RaycastCellType type,
                                       QString& errorMessage) {
    if (x < 0 || y < 0 || x >= m_asset.map.width || y >= m_asset.map.height ||
        static_cast<unsigned>(type) > static_cast<unsigned>(RaycastCellType::Light)) {
        errorMessage = tr("Raycast paint request is outside the map or cell-type bounds.");
        return false;
    }
    if (type == RaycastCellType::Empty &&
        (x == 0 || y == 0 || x == m_asset.map.width - 1 || y == m_asset.map.height - 1)) {
        errorMessage = tr("The strict .fglray format requires a solid outer boundary.");
        return false;
    }
    auto candidate = m_asset;
    candidate.map
        .cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(candidate.map.width) +
               static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(type);
    auto valid = fabgl::rendering::validateRaycastMapAsset(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    m_asset = std::move(candidate);
    refreshGrid();
    refreshPreview();
    return true;
}

RaycastCellType RaycastMapEditorWidget::cellType(const int x, const int y) const noexcept {
    if (x < 0 || y < 0 || x >= m_asset.map.width || y >= m_asset.map.height)
        return RaycastCellType::Empty;
    const auto value =
        m_asset.map
            .cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_asset.map.width) +
                   static_cast<std::size_t>(x)];
    return value <= static_cast<std::uint8_t>(RaycastCellType::Light)
               ? static_cast<RaycastCellType>(value)
               : RaycastCellType::Wall;
}

bool RaycastMapEditorWidget::openMapFile(const QString& filePath, QString& errorMessage) {
    QByteArray bytes;
    if (!readBoundedFile(filePath, MaximumRaycastBytes, bytes, errorMessage))
        return false;
    auto parsed = fabgl::rendering::deserializeRaycastMapAsset(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = errorText(parsed.error());
        return false;
    }
    if (parsed.value().map.width > MaximumEditorRaycastDimension ||
        parsed.value().map.height > MaximumEditorRaycastDimension) {
        errorMessage = tr("Map is valid but exceeds the interactive editor dimension limit.");
        return false;
    }
    constexpr std::array extraColors{fabgl::Color{210U, 135U, 55U, 255U},
                                     fabgl::Color{120U, 70U, 155U, 255U},
                                     fabgl::Color{245U, 225U, 95U, 255U}};
    while (parsed.value().map.wallPalette.size() < 5U) {
        parsed.value().map.wallPalette.push_back(
            extraColors[parsed.value().map.wallPalette.size() - 2U]);
    }
    m_asset = std::move(parsed.value());
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    refreshGrid();
    refreshPreview();
    emit statusMessage(tr("Opened strict raycast map %1.").arg(m_filePath));
    return true;
}

bool RaycastMapEditorWidget::saveMapFile(const QString& filePath, QString& errorMessage) {
    auto serialized = fabgl::rendering::serializeRaycastMapAsset(m_asset);
    if (!serialized) {
        errorMessage = errorText(serialized.error());
        return false;
    }
    if (!writeAtomicFile(filePath,
                         QByteArray(serialized.value().data(),
                                    static_cast<qsizetype>(serialized.value().size())),
                         errorMessage)) {
        return false;
    }
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    emit statusMessage(tr("Saved strict raycast map %1.").arg(m_filePath));
    return true;
}

const fabgl::rendering::RaycastMapAsset& RaycastMapEditorWidget::mapAsset() const noexcept {
    return m_asset;
}

bool RaycastMapEditorWidget::mapValid() const noexcept {
    return m_valid;
}

qsizetype RaycastMapEditorWidget::cellCount(const RaycastCellType type) const noexcept {
    const auto expected = static_cast<std::uint8_t>(type);
    return static_cast<qsizetype>(
        std::count(m_asset.map.cells.begin(), m_asset.map.cells.end(), expected));
}

std::uint64_t RaycastMapEditorWidget::previewChecksum() const noexcept {
    return m_previewChecksum;
}

QString RaycastMapEditorWidget::filePath() const {
    return m_filePath;
}

void RaycastMapEditorWidget::refreshGrid() {
    const auto rows = std::min(m_asset.map.height, 64);
    const auto columns = std::min(m_asset.map.width, 64);
    m_grid->setRowCount(rows);
    m_grid->setColumnCount(columns);
    constexpr std::array labels{"", "W", "D", "S", "L"};
    constexpr std::array colors{QColor(25, 30, 40), QColor(170, 175, 190), QColor(210, 135, 55),
                                QColor(120, 70, 155), QColor(245, 225, 95)};
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const auto type = cellType(x, y);
            const auto index = static_cast<std::size_t>(type);
            auto* cell = item(QString::fromLatin1(labels[index]));
            cell->setTextAlignment(Qt::AlignCenter);
            cell->setBackground(colors[index]);
            m_grid->setItem(y, x, cell);
        }
    }
}

void RaycastMapEditorWidget::refreshPreview() {
    auto validation = fabgl::rendering::validateRaycastMapAsset(m_asset);
    m_valid = static_cast<bool>(validation);
    fabgl::rendering::Framebuffer framebuffer(192, 128);
    framebuffer.clear({17U, 22U, 31U, 255U});
    if (m_valid) {
        fabgl::rendering::RaycastCamera camera;
        camera.position = {static_cast<float>(m_asset.map.width) * 0.5F,
                           static_cast<float>(m_asset.map.height) * 0.5F};
        auto centerX = std::clamp(static_cast<int>(camera.position.x), 1, m_asset.map.width - 2);
        auto centerY = std::clamp(static_cast<int>(camera.position.y), 1, m_asset.map.height - 2);
        if (m_asset.map.cell(centerX, centerY) != 0U) {
            for (int y = 1; y < m_asset.map.height - 1; ++y) {
                for (int x = 1; x < m_asset.map.width - 1; ++x) {
                    if (m_asset.map.cell(x, y) == 0U) {
                        centerX = x;
                        centerY = y;
                        y = m_asset.map.height;
                        break;
                    }
                }
            }
        }
        camera.position = {static_cast<float>(centerX) + 0.5F, static_cast<float>(centerY) + 0.5F};
        fabgl::rendering::RaycastRenderer renderer(framebuffer);
        static_cast<void>(renderer.render(m_asset.map, camera));
        const auto scale =
            std::max(1, std::min({3, 48 / m_asset.map.width, 48 / m_asset.map.height}));
        for (int y = 0; y < m_asset.map.height; ++y) {
            for (int x = 0; x < m_asset.map.width; ++x) {
                const auto value = m_asset.map.cell(x, y);
                const auto palette =
                    std::min<std::size_t>(value, m_asset.map.wallPalette.size() - 1U);
                framebuffer.fillRect(3 + x * scale, 3 + y * scale, scale, scale,
                                     m_asset.map.wallPalette[palette]);
            }
        }
        framebuffer.fillRect(3 + centerX * scale, 3 + centerY * scale, scale, scale,
                             {60U, 245U, 120U, 255U});
        m_status->setText(tr("Valid | wall %1, door %2, secret %3, light %4")
                              .arg(cellCount(RaycastCellType::Wall))
                              .arg(cellCount(RaycastCellType::Door))
                              .arg(cellCount(RaycastCellType::Secret))
                              .arg(cellCount(RaycastCellType::Light)));
    } else {
        m_status->setText(errorText(validation.error()));
    }
    m_previewChecksum = framebuffer.checksum();
    showFramebuffer(m_preview, framebuffer);
}

TrackEditorWidget::TrackEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("trackEditor"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* tableColumn = new QVBoxLayout();
    auto* history = new QHBoxLayout();
    m_undoButton = new QPushButton(tr("Undo"), this);
    m_undoButton->setObjectName(QStringLiteral("trackUndoButton"));
    m_redoButton = new QPushButton(tr("Redo"), this);
    m_redoButton->setObjectName(QStringLiteral("trackRedoButton"));
    history->addWidget(m_undoButton);
    history->addWidget(m_redoButton);
    history->addStretch();
    tableColumn->addLayout(history);
    auto* tables = new QTabWidget(this);
    tables->setObjectName(QStringLiteral("trackAuthoringTabs"));
    m_segmentTable = new QTableWidget(0, 4, tables);
    m_segmentTable->setObjectName(QStringLiteral("trackSegmentTable"));
    m_segmentTable->setHorizontalHeaderLabels({tr("#"), tr("Curve"), tr("Hill"), tr("Width")});
    m_segmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_segmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tables->addTab(m_segmentTable, tr("Segments / Checkpoints"));
    m_sceneryTable = new QTableWidget(0, 5, tables);
    m_sceneryTable->setObjectName(QStringLiteral("trackSceneryTable"));
    m_sceneryTable->setHorizontalHeaderLabels(
        {tr("ID"), tr("Segment"), tr("Lateral"), tr("Scale"), tr("Sprite")});
    m_sceneryTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_sceneryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tables->addTab(m_sceneryTable, tr("Scenery"));
    m_opponentTable = new QTableWidget(0, 6, tables);
    m_opponentTable->setObjectName(QStringLiteral("trackOpponentTable"));
    m_opponentTable->setHorizontalHeaderLabels(
        {tr("ID"), tr("Segment"), tr("Lateral"), tr("Speed"), tr("Skill"), tr("Sprite")});
    m_opponentTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_opponentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tables->addTab(m_opponentTable, tr("Opponents"));
    tableColumn->addWidget(tables, 1);
    root->addLayout(tableColumn, 1);

    const auto decimal = [this](const QString& name, const double minimum, const double maximum,
                                const double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(name);
        spin->setRange(minimum, maximum);
        spin->setDecimals(3);
        spin->setSingleStep(0.05);
        spin->setValue(value);
        return spin;
    };
    auto* authoring = new QTabWidget(this);
    authoring->setObjectName(QStringLiteral("trackAuthoringControls"));
    auto* segmentPage = new QWidget(authoring);
    auto* segmentForm = new QFormLayout(segmentPage);
    m_segmentIndex = new QSpinBox(segmentPage);
    m_segmentIndex->setObjectName(QStringLiteral("trackSegmentIndexSpin"));
    m_segmentCurve = decimal(QStringLiteral("trackSegmentCurveSpin"), -4.0, 4.0, 0.0);
    m_segmentHill = decimal(QStringLiteral("trackSegmentHillSpin"), -4.0, 4.0, 0.0);
    m_segmentWidth = decimal(QStringLiteral("trackSegmentWidthSpin"), 0.1, 16.0, 1.0);
    auto* segmentButtons = new QWidget(segmentPage);
    auto* segmentButtonLayout = new QHBoxLayout(segmentButtons);
    segmentButtonLayout->setContentsMargins(0, 0, 0, 0);
    auto* appendSegmentButton = new QPushButton(tr("Add"), segmentButtons);
    appendSegmentButton->setObjectName(QStringLiteral("trackAddSegmentButton"));
    auto* updateSegmentButton = new QPushButton(tr("Apply"), segmentButtons);
    updateSegmentButton->setObjectName(QStringLiteral("trackUpdateSegmentButton"));
    segmentButtonLayout->addWidget(appendSegmentButton);
    segmentButtonLayout->addWidget(updateSegmentButton);
    segmentForm->addRow(tr("Segment"), m_segmentIndex);
    segmentForm->addRow(tr("Curve"), m_segmentCurve);
    segmentForm->addRow(tr("Hill"), m_segmentHill);
    segmentForm->addRow(tr("Width"), m_segmentWidth);
    segmentForm->addRow({}, segmentButtons);
    authoring->addTab(segmentPage, tr("Segment"));

    auto* checkpointPage = new QWidget(authoring);
    auto* checkpointForm = new QFormLayout(checkpointPage);
    m_checkpointSegment = new QSpinBox(checkpointPage);
    m_checkpointSegment->setObjectName(QStringLiteral("trackCheckpointSegmentSpin"));
    m_checkpointName = new QLineEdit(QStringLiteral("Checkpoint"), checkpointPage);
    m_checkpointName->setObjectName(QStringLiteral("trackCheckpointNameEdit"));
    auto* addCheckpointButton = new QPushButton(tr("Add Checkpoint"), checkpointPage);
    addCheckpointButton->setObjectName(QStringLiteral("trackAddCheckpointButton"));
    checkpointForm->addRow(tr("Segment"), m_checkpointSegment);
    checkpointForm->addRow(tr("Name"), m_checkpointName);
    checkpointForm->addRow({}, addCheckpointButton);
    authoring->addTab(checkpointPage, tr("Checkpoint"));

    auto* sceneryPage = new QWidget(authoring);
    auto* sceneryForm = new QFormLayout(sceneryPage);
    m_scenerySegment = new QSpinBox(sceneryPage);
    m_scenerySegment->setObjectName(QStringLiteral("trackScenerySegmentSpin"));
    m_sceneryLateral = decimal(QStringLiteral("trackSceneryLateralSpin"), -4.0, 4.0, 0.0);
    m_sceneryScale = decimal(QStringLiteral("trackSceneryScaleSpin"), 0.01, 16.0, 1.0);
    m_scenerySprite = new QLineEdit(sceneryPage);
    m_scenerySprite->setObjectName(QStringLiteral("trackScenerySpriteEdit"));
    m_scenerySprite->setText(QString::fromStdString(
        fabgl::AssetGuid::fromStableName("fabgl.studio.track.scenery").toString()));
    auto* addSceneryButton = new QPushButton(tr("Add Scenery"), sceneryPage);
    addSceneryButton->setObjectName(QStringLiteral("trackAddSceneryButton"));
    sceneryForm->addRow(tr("Segment"), m_scenerySegment);
    sceneryForm->addRow(tr("Lateral"), m_sceneryLateral);
    sceneryForm->addRow(tr("Scale"), m_sceneryScale);
    sceneryForm->addRow(tr("Sprite GUID"), m_scenerySprite);
    sceneryForm->addRow({}, addSceneryButton);
    authoring->addTab(sceneryPage, tr("Scenery"));

    auto* opponentPage = new QWidget(authoring);
    auto* opponentForm = new QFormLayout(opponentPage);
    m_opponentSegment = new QSpinBox(opponentPage);
    m_opponentSegment->setObjectName(QStringLiteral("trackOpponentSegmentSpin"));
    m_opponentLateral = decimal(QStringLiteral("trackOpponentLateralSpin"), -4.0, 4.0, 0.0);
    m_opponentSpeed = decimal(QStringLiteral("trackOpponentSpeedSpin"), 0.0, 1000.0, 40.0);
    m_opponentSkill = decimal(QStringLiteral("trackOpponentSkillSpin"), 0.0, 1.0, 0.5);
    m_opponentSprite = new QLineEdit(opponentPage);
    m_opponentSprite->setObjectName(QStringLiteral("trackOpponentSpriteEdit"));
    m_opponentSprite->setText(QString::fromStdString(
        fabgl::AssetGuid::fromStableName("fabgl.studio.track.opponent").toString()));
    auto* addOpponentButton = new QPushButton(tr("Add Opponent"), opponentPage);
    addOpponentButton->setObjectName(QStringLiteral("trackAddOpponentButton"));
    opponentForm->addRow(tr("Segment"), m_opponentSegment);
    opponentForm->addRow(tr("Lateral"), m_opponentLateral);
    opponentForm->addRow(tr("Speed"), m_opponentSpeed);
    opponentForm->addRow(tr("Skill"), m_opponentSkill);
    opponentForm->addRow(tr("Sprite GUID"), m_opponentSprite);
    opponentForm->addRow({}, addOpponentButton);
    authoring->addTab(opponentPage, tr("Opponent"));
    root->addWidget(authoring);

    auto* previewColumn = new QVBoxLayout();
    m_weather = new QComboBox(this);
    m_weather->setObjectName(QStringLiteral("trackWeatherCombo"));
    m_weather->addItem(tr("Clear"), static_cast<int>(fabgl::rendering::RacerWeatherKind::Clear));
    m_weather->addItem(tr("Rain"), static_cast<int>(fabgl::rendering::RacerWeatherKind::Rain));
    m_weather->addItem(tr("Fog"), static_cast<int>(fabgl::rendering::RacerWeatherKind::Fog));
    m_weather->addItem(tr("Storm"), static_cast<int>(fabgl::rendering::RacerWeatherKind::Storm));
    previewColumn->addWidget(m_weather);
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("trackPreview"));
    previewColumn->addWidget(m_preview, 0, Qt::AlignCenter);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("trackValidationStatus"));
    previewColumn->addWidget(m_status);
    previewColumn->addStretch();
    root->addLayout(previewColumn);
    connect(m_undoButton, &QPushButton::clicked, this, &TrackEditorWidget::undo);
    connect(m_redoButton, &QPushButton::clicked, this, &TrackEditorWidget::redo);
    const auto segmentFromControls = [this]() {
        fabgl::rendering::RoadSegment segment;
        segment.curve = static_cast<float>(m_segmentCurve->value());
        segment.hill = static_cast<float>(m_segmentHill->value());
        segment.width = static_cast<float>(m_segmentWidth->value());
        return segment;
    };
    const auto report = [this](const bool success, const QString& error,
                               const QString& successMessage) {
        m_status->setText(success ? successMessage : error);
        if (success)
            emit statusMessage(successMessage);
    };
    connect(appendSegmentButton, &QPushButton::clicked, this,
            [this, segmentFromControls, report]() {
                QString error;
                const bool success = appendSegment(segmentFromControls(), error);
                report(success, error, tr("Track segment added."));
            });
    connect(
        updateSegmentButton, &QPushButton::clicked, this, [this, segmentFromControls, report]() {
            QString error;
            const bool success = updateSegment(static_cast<std::size_t>(m_segmentIndex->value()),
                                               segmentFromControls(), error);
            report(success, error, tr("Track segment updated."));
        });
    connect(addCheckpointButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const bool success = addCheckpoint(static_cast<std::uint32_t>(m_checkpointSegment->value()),
                                           m_checkpointName->text(), error);
        report(success, error, tr("Track checkpoint added."));
    });
    connect(addSceneryButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const auto guid = fabgl::AssetGuid::parse(m_scenerySprite->text().trimmed().toStdString());
        if (!guid) {
            report(false, tr("Scenery sprite GUID is invalid."), {});
            return;
        }
        const bool success =
            addScenery(static_cast<std::uint32_t>(m_scenerySegment->value()),
                       static_cast<float>(m_sceneryLateral->value()),
                       static_cast<float>(m_sceneryScale->value()), guid.value(), error);
        report(success, error, tr("Track scenery added."));
    });
    connect(addOpponentButton, &QPushButton::clicked, this, [this, report]() {
        QString error;
        const auto guid = fabgl::AssetGuid::parse(m_opponentSprite->text().trimmed().toStdString());
        if (!guid) {
            report(false, tr("Opponent sprite GUID is invalid."), {});
            return;
        }
        const bool success =
            addOpponent(static_cast<std::uint32_t>(m_opponentSegment->value()),
                        static_cast<float>(m_opponentLateral->value()),
                        static_cast<float>(m_opponentSpeed->value()),
                        static_cast<float>(m_opponentSkill->value()), guid.value(), error);
        report(success, error, tr("Track opponent added."));
    });
    connect(m_segmentTable, &QTableWidget::cellClicked, this, [this](const int row, const int) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_track.segments.size())
            return;
        m_segmentIndex->setValue(row);
        const auto& segment = m_track.segments[static_cast<std::size_t>(row)];
        m_segmentCurve->setValue(segment.curve);
        m_segmentHill->setValue(segment.hill);
        m_segmentWidth->setValue(segment.width);
    });
    connect(m_weather, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updatingControls)
            return;
        auto candidate = m_track.weather;
        candidate.kind = selectedEnum<fabgl::rendering::RacerWeatherKind>(m_weather);
        QString error;
        if (!setWeather(candidate, error))
            m_status->setText(error);
    });
    QString ignored;
    static_cast<void>(newTrack(tr("Untitled Track"), ignored));
}

bool TrackEditorWidget::newTrack(const QString& name, QString& errorMessage) {
    const auto normalized = name.trimmed();
    if (normalized.isEmpty() || normalized.toUtf8().size() > 1024) {
        errorMessage = tr("Track name is empty or exceeds the strict string limit.");
        return false;
    }
    fabgl::rendering::RacerTrackAsset track;
    track.guid = fabgl::AssetGuid::fromStableName(std::string("fabgl.studio.track.") +
                                                  normalized.toStdString());
    track.name = normalized.toStdString();
    track.segmentLength = 1.0F;
    track.startSegment = 0U;
    track.finishSegment = 15U;
    track.segments.resize(16U);
    for (std::size_t index = 0U; index < track.segments.size(); ++index) {
        track.segments[index].curve = index > 3U && index < 8U ? 0.08F : 0.0F;
        track.segments[index].hill = index > 9U ? 0.03F : 0.0F;
    }
    track.checkpoints = {{0U, "Start"}, {15U, "Finish"}};
    auto valid = fabgl::rendering::validateRacerTrack(track);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    m_track = std::move(track);
    m_filePath.clear();
    m_undoHistory.clear();
    m_redoHistory.clear();
    refreshTables();
    refreshPreview();
    return true;
}

bool TrackEditorWidget::appendSegment(const fabgl::rendering::RoadSegment segment,
                                      QString& errorMessage) {
    auto candidate = m_track;
    candidate.segments.push_back(segment);
    candidate.finishSegment = static_cast<std::uint32_t>(candidate.segments.size() - 1U);
    auto valid = fabgl::rendering::validateRacerTrack(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    recordUndoPoint();
    m_track = std::move(candidate);
    refreshTables();
    refreshPreview();
    return true;
}

bool TrackEditorWidget::updateSegment(const std::size_t index,
                                      const fabgl::rendering::RoadSegment segment,
                                      QString& errorMessage) {
    if (index >= m_track.segments.size()) {
        errorMessage = tr("Track segment index is out of range.");
        return false;
    }
    auto candidate = m_track;
    candidate.segments[index] = segment;
    auto valid = fabgl::rendering::validateRacerTrack(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    recordUndoPoint();
    m_track = std::move(candidate);
    refreshTables();
    refreshPreview();
    return true;
}

bool TrackEditorWidget::addCheckpoint(const std::uint32_t segment, QString name,
                                      QString& errorMessage) {
    name = name.trimmed();
    auto candidate = m_track;
    candidate.checkpoints.push_back({segment, name.toStdString()});
    auto valid = fabgl::rendering::validateRacerTrack(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    recordUndoPoint();
    m_track = std::move(candidate);
    refreshTables();
    refreshPreview();
    return true;
}

bool TrackEditorWidget::addScenery(const std::uint32_t segment, const float lateral,
                                   const float scale, const fabgl::AssetGuid sprite,
                                   QString& errorMessage) {
    std::uint16_t id = 1U;
    for (const auto& object : m_track.roadsideObjects) {
        if (object.id >= id && object.id < std::numeric_limits<std::uint16_t>::max())
            id = static_cast<std::uint16_t>(object.id + 1U);
    }
    auto candidate = m_track;
    candidate.roadsideObjects.push_back(
        {id, segment, lateral, scale, sprite, {255U, 255U, 255U, 255U}});
    auto valid = fabgl::rendering::validateRacerTrack(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    recordUndoPoint();
    m_track = std::move(candidate);
    refreshTables();
    refreshPreview();
    return true;
}

bool TrackEditorWidget::addOpponent(const std::uint32_t segment, const float lateral,
                                    const float speed, const float skill,
                                    const fabgl::AssetGuid sprite, QString& errorMessage) {
    std::uint16_t id = 1U;
    for (const auto& opponent : m_track.opponentSpawns) {
        if (opponent.id >= id && opponent.id < std::numeric_limits<std::uint16_t>::max())
            id = static_cast<std::uint16_t>(opponent.id + 1U);
    }
    auto candidate = m_track;
    candidate.opponentSpawns.push_back({id, segment, lateral, speed, skill, sprite});
    auto valid = fabgl::rendering::validateRacerTrack(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    recordUndoPoint();
    m_track = std::move(candidate);
    refreshTables();
    refreshPreview();
    return true;
}

bool TrackEditorWidget::setWeather(const fabgl::rendering::RacerWeatherMetadata weather,
                                   QString& errorMessage) {
    auto candidate = m_track;
    candidate.weather = weather;
    auto valid = fabgl::rendering::validateRacerTrack(candidate);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    recordUndoPoint();
    m_track = std::move(candidate);
    m_updatingControls = true;
    selectEnum(m_weather, m_track.weather.kind);
    m_updatingControls = false;
    refreshPreview();
    return true;
}

bool TrackEditorWidget::openTrackFile(const QString& filePath, QString& errorMessage) {
    QByteArray bytes;
    if (!readBoundedFile(filePath, MaximumTrackBytes, bytes, errorMessage))
        return false;
    auto parsed = fabgl::rendering::deserializeRacerTrack(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed) {
        errorMessage = errorText(parsed.error());
        return false;
    }
    m_track = std::move(parsed.value());
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    m_undoHistory.clear();
    m_redoHistory.clear();
    refreshTables();
    refreshPreview();
    emit statusMessage(tr("Opened strict racer track %1.").arg(m_filePath));
    return true;
}

bool TrackEditorWidget::saveTrackFile(const QString& filePath, QString& errorMessage) {
    auto serialized = fabgl::rendering::serializeRacerTrack(m_track);
    if (!serialized) {
        errorMessage = errorText(serialized.error());
        return false;
    }
    if (!writeAtomicFile(filePath,
                         QByteArray(serialized.value().data(),
                                    static_cast<qsizetype>(serialized.value().size())),
                         errorMessage)) {
        return false;
    }
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    emit statusMessage(tr("Saved strict racer track %1.").arg(m_filePath));
    return true;
}

const fabgl::rendering::RacerTrackAsset& TrackEditorWidget::track() const noexcept {
    return m_track;
}

bool TrackEditorWidget::trackValid() const noexcept {
    return m_valid;
}

qsizetype TrackEditorWidget::segmentCount() const noexcept {
    return static_cast<qsizetype>(m_track.segments.size());
}

qsizetype TrackEditorWidget::checkpointCount() const noexcept {
    return static_cast<qsizetype>(m_track.checkpoints.size());
}

qsizetype TrackEditorWidget::sceneryCount() const noexcept {
    return static_cast<qsizetype>(m_track.roadsideObjects.size());
}

qsizetype TrackEditorWidget::opponentCount() const noexcept {
    return static_cast<qsizetype>(m_track.opponentSpawns.size());
}

std::uint64_t TrackEditorWidget::previewChecksum() const noexcept {
    return m_previewChecksum;
}

QString TrackEditorWidget::filePath() const {
    return m_filePath;
}

bool TrackEditorWidget::canUndo() const noexcept {
    return !m_undoHistory.empty();
}

bool TrackEditorWidget::canRedo() const noexcept {
    return !m_redoHistory.empty();
}

TrackEditorWidget::Snapshot TrackEditorWidget::snapshot() const {
    return {m_track};
}

void TrackEditorWidget::restoreSnapshot(Snapshot value) {
    m_track = std::move(value.track);
    refreshTables();
    refreshPreview();
}

void TrackEditorWidget::recordUndoPoint() {
    constexpr std::size_t MaximumHistory = 100U;
    if (m_undoHistory.size() >= MaximumHistory)
        m_undoHistory.erase(m_undoHistory.begin());
    m_undoHistory.push_back(snapshot());
    m_redoHistory.clear();
    updateHistoryActions();
}

void TrackEditorWidget::undo() {
    if (m_undoHistory.empty())
        return;
    m_redoHistory.push_back(snapshot());
    auto value = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();
    restoreSnapshot(std::move(value));
    updateHistoryActions();
    emit statusMessage(tr("Track edit undone."));
}

void TrackEditorWidget::redo() {
    if (m_redoHistory.empty())
        return;
    m_undoHistory.push_back(snapshot());
    auto value = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();
    restoreSnapshot(std::move(value));
    updateHistoryActions();
    emit statusMessage(tr("Track edit redone."));
}

void TrackEditorWidget::updateHistoryActions() {
    if (m_undoButton != nullptr)
        m_undoButton->setEnabled(canUndo());
    if (m_redoButton != nullptr)
        m_redoButton->setEnabled(canRedo());
}

void TrackEditorWidget::refreshAuthoringControls() {
    if (m_segmentIndex == nullptr)
        return;
    const int maximum =
        m_track.segments.empty() ? 0 : static_cast<int>(m_track.segments.size() - 1U);
    for (auto* spin : {m_segmentIndex, m_checkpointSegment, m_scenerySegment, m_opponentSegment})
        spin->setRange(0, maximum);
    if (!m_track.segments.empty()) {
        const auto index =
            static_cast<std::size_t>(std::clamp(m_segmentIndex->value(), 0, maximum));
        const auto& segment = m_track.segments[index];
        m_segmentCurve->setValue(segment.curve);
        m_segmentHill->setValue(segment.hill);
        m_segmentWidth->setValue(segment.width);
    }
    updateHistoryActions();
}

void TrackEditorWidget::refreshTables() {
    m_segmentTable->setRowCount(static_cast<int>(m_track.segments.size()));
    for (int row = 0; row < static_cast<int>(m_track.segments.size()); ++row) {
        const auto& segment = m_track.segments[static_cast<std::size_t>(row)];
        const auto checkpoint = std::find_if(
            m_track.checkpoints.begin(), m_track.checkpoints.end(),
            [row](const auto& value) { return value.segment == static_cast<std::uint32_t>(row); });
        const auto label =
            checkpoint == m_track.checkpoints.end()
                ? QString::number(row)
                : QStringLiteral("%1 (%2)").arg(row).arg(QString::fromStdString(checkpoint->name));
        m_segmentTable->setItem(row, 0, item(label));
        m_segmentTable->setItem(row, 1, item(QString::number(segment.curve)));
        m_segmentTable->setItem(row, 2, item(QString::number(segment.hill)));
        m_segmentTable->setItem(row, 3, item(QString::number(segment.width)));
    }
    m_sceneryTable->setRowCount(static_cast<int>(m_track.roadsideObjects.size()));
    for (int row = 0; row < static_cast<int>(m_track.roadsideObjects.size()); ++row) {
        const auto& object = m_track.roadsideObjects[static_cast<std::size_t>(row)];
        m_sceneryTable->setItem(row, 0, item(QString::number(object.id)));
        m_sceneryTable->setItem(row, 1, item(QString::number(object.segment)));
        m_sceneryTable->setItem(row, 2, item(QString::number(object.lateral)));
        m_sceneryTable->setItem(row, 3, item(QString::number(object.scale)));
        m_sceneryTable->setItem(row, 4, item(QString::fromStdString(object.sprite.toString())));
    }
    m_opponentTable->setRowCount(static_cast<int>(m_track.opponentSpawns.size()));
    for (int row = 0; row < static_cast<int>(m_track.opponentSpawns.size()); ++row) {
        const auto& opponent = m_track.opponentSpawns[static_cast<std::size_t>(row)];
        m_opponentTable->setItem(row, 0, item(QString::number(opponent.id)));
        m_opponentTable->setItem(row, 1, item(QString::number(opponent.segment)));
        m_opponentTable->setItem(row, 2, item(QString::number(opponent.lateral)));
        m_opponentTable->setItem(row, 3, item(QString::number(opponent.targetSpeed)));
        m_opponentTable->setItem(row, 4, item(QString::number(opponent.skill)));
        m_opponentTable->setItem(row, 5, item(QString::fromStdString(opponent.sprite.toString())));
    }
    m_updatingControls = true;
    selectEnum(m_weather, m_track.weather.kind);
    m_updatingControls = false;
    refreshAuthoringControls();
}

void TrackEditorWidget::refreshPreview() {
    auto validation = fabgl::rendering::validateRacerTrack(m_track);
    m_valid = static_cast<bool>(validation);
    fabgl::rendering::Framebuffer framebuffer(192, 128);
    framebuffer.clear({17U, 22U, 31U, 255U});
    if (m_valid) {
        fabgl::rendering::RacerRenderer renderer(framebuffer);
        fabgl::rendering::RacerCamera camera;
        camera.speed = 45.0F;
        camera.distance = static_cast<float>(m_track.startSegment) * m_track.segmentLength;
        const auto stats = renderer.render(m_track, camera);
        m_status->setText(tr("Valid | %1 segments, %2 scenery, %3 opponents, %4 road rows")
                              .arg(segmentCount())
                              .arg(sceneryCount())
                              .arg(opponentCount())
                              .arg(stats.scanlines));
    } else {
        m_status->setText(errorText(validation.error()));
    }
    m_previewChecksum = framebuffer.checksum();
    showFramebuffer(m_preview, framebuffer);
}

namespace {

[[nodiscard]] QString uiWidgetTypeName(const fabgl::UIWidgetType type) {
    switch (type) {
    case fabgl::UIWidgetType::Panel:
        return QObject::tr("Panel");
    case fabgl::UIWidgetType::Image:
        return QObject::tr("Image");
    case fabgl::UIWidgetType::Text:
        return QObject::tr("Text");
    case fabgl::UIWidgetType::Button:
        return QObject::tr("Button");
    case fabgl::UIWidgetType::Toggle:
        return QObject::tr("Toggle");
    case fabgl::UIWidgetType::Slider:
        return QObject::tr("Slider");
    case fabgl::UIWidgetType::Progress:
        return QObject::tr("Progress");
    case fabgl::UIWidgetType::List:
        return QObject::tr("List");
    case fabgl::UIWidgetType::Layout:
        return QObject::tr("Layout");
    }
    return QObject::tr("Widget");
}

[[nodiscard]] fabgl::UITheme lightUITheme() noexcept {
    fabgl::UITheme theme;
    theme.panel = {236U, 239U, 245U, 255U};
    theme.foreground = {24U, 28U, 36U, 255U};
    theme.accent = {25U, 105U, 215U, 255U};
    theme.disabled = {145U, 150U, 160U, 255U};
    return theme;
}

[[nodiscard]] bool isLightUITheme(const fabgl::UITheme& theme) noexcept {
    return theme.panel.r > 127U && theme.panel.g > 127U && theme.panel.b > 127U;
}

} // namespace

UIEditorWidget::UIEditorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("uiEditorPanel"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* hierarchyColumn = new QVBoxLayout();
    m_hierarchy = new QTreeWidget(this);
    m_hierarchy->setObjectName(QStringLiteral("uiHierarchyTree"));
    m_hierarchy->setHeaderLabel(tr("Runtime UI hierarchy"));
    hierarchyColumn->addWidget(m_hierarchy, 1);

    m_palette = new QComboBox(this);
    m_palette->setObjectName(QStringLiteral("uiWidgetPaletteCombo"));
    constexpr std::array widgetTypes{
        fabgl::UIWidgetType::Panel,    fabgl::UIWidgetType::Image,  fabgl::UIWidgetType::Text,
        fabgl::UIWidgetType::Button,   fabgl::UIWidgetType::Toggle, fabgl::UIWidgetType::Slider,
        fabgl::UIWidgetType::Progress, fabgl::UIWidgetType::List,   fabgl::UIWidgetType::Layout,
    };
    for (const auto type : widgetTypes)
        m_palette->addItem(uiWidgetTypeName(type), static_cast<int>(type));
    hierarchyColumn->addWidget(m_palette);

    auto* hierarchyButtons = new QWidget(this);
    auto* hierarchyButtonLayout = new QHBoxLayout(hierarchyButtons);
    hierarchyButtonLayout->setContentsMargins(0, 0, 0, 0);
    auto* addButton = new QPushButton(tr("Add"), hierarchyButtons);
    addButton->setObjectName(QStringLiteral("uiAddWidgetButton"));
    m_removeButton = new QPushButton(tr("Remove"), hierarchyButtons);
    m_removeButton->setObjectName(QStringLiteral("uiRemoveWidgetButton"));
    hierarchyButtonLayout->addWidget(addButton);
    hierarchyButtonLayout->addWidget(m_removeButton);
    hierarchyColumn->addWidget(hierarchyButtons);

    auto* historyButtons = new QWidget(this);
    auto* historyLayout = new QHBoxLayout(historyButtons);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    m_undoButton = new QPushButton(tr("Undo"), historyButtons);
    m_undoButton->setObjectName(QStringLiteral("uiUndoButton"));
    m_redoButton = new QPushButton(tr("Redo"), historyButtons);
    m_redoButton->setObjectName(QStringLiteral("uiRedoButton"));
    historyLayout->addWidget(m_undoButton);
    historyLayout->addWidget(m_redoButton);
    hierarchyColumn->addWidget(historyButtons);
    root->addLayout(hierarchyColumn, 1);

    auto* properties = new QGroupBox(tr("Widget properties"), this);
    auto* form = new QFormLayout(properties);
    m_parent = new QComboBox(properties);
    m_parent->setObjectName(QStringLiteral("uiParentCombo"));
    auto* reparentButton = new QPushButton(tr("Reparent"), properties);
    reparentButton->setObjectName(QStringLiteral("uiReparentButton"));
    auto* parentRow = new QWidget(properties);
    auto* parentLayout = new QHBoxLayout(parentRow);
    parentLayout->setContentsMargins(0, 0, 0, 0);
    parentLayout->addWidget(m_parent, 1);
    parentLayout->addWidget(reparentButton);
    form->addRow(tr("Parent"), parentRow);

    m_text = new QLineEdit(properties);
    m_text->setObjectName(QStringLiteral("uiWidgetTextEdit"));
    form->addRow(tr("Text"), m_text);

    const auto anchorSpin = [properties](const QString& objectName) {
        auto* result = new QDoubleSpinBox(properties);
        result->setObjectName(objectName);
        result->setRange(0.0, 1.0);
        result->setDecimals(3);
        result->setSingleStep(0.05);
        return result;
    };
    const auto offsetSpin = [properties](const QString& objectName) {
        auto* result = new QDoubleSpinBox(properties);
        result->setObjectName(objectName);
        result->setRange(-4096.0, 4096.0);
        result->setDecimals(2);
        result->setSingleStep(1.0);
        return result;
    };
    m_anchorMinX = anchorSpin(QStringLiteral("uiAnchorMinXSpin"));
    m_anchorMinY = anchorSpin(QStringLiteral("uiAnchorMinYSpin"));
    m_anchorMaxX = anchorSpin(QStringLiteral("uiAnchorMaxXSpin"));
    m_anchorMaxY = anchorSpin(QStringLiteral("uiAnchorMaxYSpin"));
    m_offsetMinX = offsetSpin(QStringLiteral("uiOffsetMinXSpin"));
    m_offsetMinY = offsetSpin(QStringLiteral("uiOffsetMinYSpin"));
    m_offsetMaxX = offsetSpin(QStringLiteral("uiOffsetMaxXSpin"));
    m_offsetMaxY = offsetSpin(QStringLiteral("uiOffsetMaxYSpin"));

    const auto pairRow = [properties](QDoubleSpinBox* first, QDoubleSpinBox* second) {
        auto* result = new QWidget(properties);
        auto* layout = new QHBoxLayout(result);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(first);
        layout->addWidget(second);
        return result;
    };
    form->addRow(tr("Anchor min X/Y"), pairRow(m_anchorMinX, m_anchorMinY));
    form->addRow(tr("Anchor max X/Y"), pairRow(m_anchorMaxX, m_anchorMaxY));
    form->addRow(tr("Offset min X/Y"), pairRow(m_offsetMinX, m_offsetMinY));
    form->addRow(tr("Offset max X/Y"), pairRow(m_offsetMaxX, m_offsetMaxY));

    m_visible = new QCheckBox(tr("Visible"), properties);
    m_visible->setObjectName(QStringLiteral("uiVisibleCheck"));
    m_enabled = new QCheckBox(tr("Enabled"), properties);
    m_enabled->setObjectName(QStringLiteral("uiEnabledCheck"));
    auto* stateRow = new QWidget(properties);
    auto* stateLayout = new QHBoxLayout(stateRow);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->addWidget(m_visible);
    stateLayout->addWidget(m_enabled);
    form->addRow(tr("State"), stateRow);

    auto* applyButton = new QPushButton(tr("Apply properties"), properties);
    applyButton->setObjectName(QStringLiteral("uiApplyPropertiesButton"));
    form->addRow({}, applyButton);

    m_themeCombo = new QComboBox(properties);
    m_themeCombo->setObjectName(QStringLiteral("uiThemeCombo"));
    m_themeCombo->addItem(tr("Dark"), 0);
    m_themeCombo->addItem(tr("Light"), 1);
    form->addRow(tr("Theme"), m_themeCombo);
    m_scaleSpin = new QDoubleSpinBox(properties);
    m_scaleSpin->setObjectName(QStringLiteral("uiScaleSpin"));
    m_scaleSpin->setRange(0.25, 4.0);
    m_scaleSpin->setDecimals(2);
    m_scaleSpin->setSingleStep(0.25);
    m_scaleSpin->setValue(1.0);
    form->addRow(tr("Scale"), m_scaleSpin);
    root->addWidget(properties, 1);

    auto* previewColumn = new QVBoxLayout();
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("uiRuntimePreview"));
    previewColumn->addWidget(m_preview, 0, Qt::AlignCenter);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("uiEditorStatus"));
    m_status->setWordWrap(true);
    previewColumn->addWidget(m_status);
    previewColumn->addStretch();
    root->addLayout(previewColumn, 1);

    connect(m_undoButton, &QPushButton::clicked, this, &UIEditorWidget::undo);
    connect(m_redoButton, &QPushButton::clicked, this, &UIEditorWidget::redo);
    connect(m_hierarchy, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (m_updatingControls)
                    return;
                m_selectedId = current == nullptr ? 0U : current->data(0, Qt::UserRole).toUInt();
                refreshParentChoices();
                syncPropertiesFromSelection();
                refreshPreview();
            });
    connect(addButton, &QPushButton::clicked, this, [this]() {
        QString error;
        const auto type = static_cast<fabgl::UIWidgetType>(m_palette->currentData().toInt());
        const auto parentId = node(m_selectedId) == nullptr ? 0U : m_selectedId;
        const auto id = addWidget(type, parentId, uiWidgetTypeName(type), error);
        if (id == 0U)
            m_status->setText(error);
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!removeWidget(m_selectedId, error))
            m_status->setText(error);
    });
    connect(reparentButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!reparentWidget(m_selectedId, m_parent->currentData().toUInt(), error))
            m_status->setText(error);
    });
    connect(applyButton, &QPushButton::clicked, this, [this]() {
        auto* target = node(m_selectedId);
        if (target == nullptr)
            return;
        const auto before = snapshot();
        target->text = m_text->text();
        target->properties.anchors.minimum = {
            static_cast<float>(m_anchorMinX->value()),
            static_cast<float>(m_anchorMinY->value()),
        };
        target->properties.anchors.maximum = {
            static_cast<float>(m_anchorMaxX->value()),
            static_cast<float>(m_anchorMaxY->value()),
        };
        target->properties.minimumOffset = {
            static_cast<float>(m_offsetMinX->value()),
            static_cast<float>(m_offsetMinY->value()),
        };
        target->properties.maximumOffset = {
            static_cast<float>(m_offsetMaxX->value()),
            static_cast<float>(m_offsetMaxY->value()),
        };
        target->properties.visible = m_visible->isChecked();
        target->properties.enabled = m_enabled->isChecked();
        QString error;
        if (!rebuildRuntime(error)) {
            restoreSnapshot(before);
            m_status->setText(error);
            return;
        }
        commitUndoSnapshot(before);
        refreshHierarchy();
        refreshParentChoices();
        syncPropertiesFromSelection();
        m_status->setText(tr("Widget properties applied."));
        emit statusMessage(m_status->text());
    });
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (m_updatingControls)
            return;
        QString error;
        const auto theme = index == 1 ? lightUITheme() : fabgl::UITheme{};
        if (!setEditorTheme(theme, error))
            m_status->setText(error);
    });
    connect(m_scaleSpin, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (m_updatingControls)
            return;
        QString error;
        if (!setEditorScale(static_cast<float>(m_scaleSpin->value()), error))
            m_status->setText(error);
    });

    Node panel;
    panel.id = m_nextId++;
    panel.type = fabgl::UIWidgetType::Panel;
    panel.text = tr("Root");
    panel.properties.anchors.maximum = {1.0F, 1.0F};
    panel.properties.maximumOffset = {0.0F, 0.0F};
    m_nodes.push_back(panel);

    Node button;
    button.id = m_nextId++;
    button.parentId = panel.id;
    button.type = fabgl::UIWidgetType::Button;
    button.text = tr("Button");
    button.properties.minimumOffset = {20.0F, 20.0F};
    button.properties.maximumOffset = {150.0F, 54.0F};
    m_nodes.push_back(button);
    m_selectedId = panel.id;

    QString error;
    if (!rebuildRuntime(error))
        m_status->setText(error);
    refreshHierarchy();
    refreshParentChoices();
    syncPropertiesFromSelection();
    m_undoHistory.clear();
    m_redoHistory.clear();
    updateHistoryActions();
}

std::uint32_t UIEditorWidget::addWidget(const fabgl::UIWidgetType type,
                                        const std::uint32_t parentId, QString text,
                                        QString& errorMessage) {
    constexpr std::size_t MaximumWidgets = 512U;
    constexpr qsizetype MaximumTextBytes = 1024;
    if (m_nodes.size() >= MaximumWidgets) {
        errorMessage = tr("The UI editor widget limit (%1) was reached.").arg(MaximumWidgets);
        return 0U;
    }
    if (parentId != 0U && node(parentId) == nullptr) {
        errorMessage = tr("The selected UI parent no longer exists.");
        return 0U;
    }
    if (text.toUtf8().size() > MaximumTextBytes) {
        errorMessage = tr("Widget text exceeds the strict 1024-byte limit.");
        return 0U;
    }

    const auto before = snapshot();
    Node created;
    created.id = m_nextId++;
    created.parentId = parentId;
    created.type = type;
    created.text = std::move(text);
    const auto siblingCount = static_cast<float>(
        std::count_if(m_nodes.begin(), m_nodes.end(), [parentId](const Node& candidate) {
            return candidate.parentId == parentId;
        }));
    created.properties.minimumOffset = {12.0F + std::fmod(siblingCount, 5.0F) * 12.0F,
                                        12.0F + std::fmod(siblingCount, 5.0F) * 10.0F};
    created.properties.maximumOffset =
        type == fabgl::UIWidgetType::Panel || type == fabgl::UIWidgetType::Layout
            ? fabgl::Vec2{created.properties.minimumOffset.x + 150.0F,
                          created.properties.minimumOffset.y + 90.0F}
            : fabgl::Vec2{created.properties.minimumOffset.x + 120.0F,
                          created.properties.minimumOffset.y + 32.0F};
    m_nodes.push_back(created);
    m_selectedId = created.id;

    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return 0U;
    }
    commitUndoSnapshot(before);
    refreshHierarchy();
    refreshParentChoices();
    syncPropertiesFromSelection();
    m_status->setText(tr("Added %1 #%2.").arg(uiWidgetTypeName(type)).arg(created.id));
    emit statusMessage(m_status->text());
    return created.id;
}

bool UIEditorWidget::removeWidget(const std::uint32_t id, QString& errorMessage) {
    const auto* removed = node(id);
    if (removed == nullptr) {
        errorMessage = tr("Select an existing widget to remove.");
        return false;
    }
    const auto fallbackParent = removed->parentId;
    const auto before = snapshot();
    std::vector<std::uint32_t> removalIds;
    removalIds.reserve(m_nodes.size());
    for (const auto& candidate : m_nodes) {
        auto cursor = candidate.id;
        for (std::size_t depth = 0U; cursor != 0U && depth <= m_nodes.size(); ++depth) {
            if (cursor == id) {
                removalIds.push_back(candidate.id);
                break;
            }
            const auto* current = node(cursor);
            cursor = current == nullptr ? 0U : current->parentId;
        }
    }
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(),
                                 [&removalIds](const Node& candidate) {
                                     return std::find(removalIds.begin(), removalIds.end(),
                                                      candidate.id) != removalIds.end();
                                 }),
                  m_nodes.end());
    m_selectedId = node(fallbackParent) != nullptr ? fallbackParent
                                                   : (m_nodes.empty() ? 0U : m_nodes.front().id);
    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return false;
    }
    commitUndoSnapshot(before);
    refreshHierarchy();
    refreshParentChoices();
    syncPropertiesFromSelection();
    m_status->setText(tr("Removed widget #%1 and its descendants.").arg(id));
    emit statusMessage(m_status->text());
    return true;
}

bool UIEditorWidget::reparentWidget(const std::uint32_t id, const std::uint32_t parentId,
                                    QString& errorMessage) {
    auto* child = node(id);
    if (child == nullptr) {
        errorMessage = tr("Select an existing widget to reparent.");
        return false;
    }
    if (parentId != 0U && node(parentId) == nullptr) {
        errorMessage = tr("The requested UI parent does not exist.");
        return false;
    }
    if (id == parentId) {
        errorMessage = tr("A widget cannot be its own parent.");
        return false;
    }
    auto cursor = parentId;
    for (std::size_t depth = 0U; cursor != 0U && depth <= m_nodes.size(); ++depth) {
        if (cursor == id) {
            errorMessage = tr("Reparenting would create a UI hierarchy cycle.");
            return false;
        }
        const auto* current = node(cursor);
        cursor = current == nullptr ? 0U : current->parentId;
    }
    if (child->parentId == parentId)
        return true;

    const auto before = snapshot();
    child->parentId = parentId;
    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return false;
    }
    commitUndoSnapshot(before);
    refreshHierarchy();
    refreshParentChoices();
    syncPropertiesFromSelection();
    m_status->setText(parentId == 0U ? tr("Widget #%1 moved to the UI root.").arg(id)
                                     : tr("Widget #%1 reparented to #%2.").arg(id).arg(parentId));
    emit statusMessage(m_status->text());
    return true;
}

bool UIEditorWidget::setWidgetText(const std::uint32_t id, QString text, QString& errorMessage) {
    constexpr qsizetype MaximumTextBytes = 1024;
    auto* target = node(id);
    if (target == nullptr) {
        errorMessage = tr("The requested UI widget does not exist.");
        return false;
    }
    if (text.toUtf8().size() > MaximumTextBytes) {
        errorMessage = tr("Widget text exceeds the strict 1024-byte limit.");
        return false;
    }
    if (target->text == text)
        return true;
    const auto before = snapshot();
    target->text = std::move(text);
    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return false;
    }
    commitUndoSnapshot(before);
    refreshHierarchy();
    syncPropertiesFromSelection();
    emit statusMessage(tr("Widget text updated."));
    return true;
}

bool UIEditorWidget::setWidgetLayout(const std::uint32_t id,
                                     const fabgl::UILayoutProperties properties,
                                     QString& errorMessage) {
    auto* target = node(id);
    if (target == nullptr) {
        errorMessage = tr("The requested UI widget does not exist.");
        return false;
    }
    const auto before = snapshot();
    target->properties = properties;
    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return false;
    }
    commitUndoSnapshot(before);
    syncPropertiesFromSelection();
    emit statusMessage(tr("Widget anchors and offsets updated."));
    return true;
}

bool UIEditorWidget::setEditorTheme(const fabgl::UITheme theme, QString& errorMessage) {
    fabgl::RuntimeUI validator;
    const auto valid = validator.setTheme(theme);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    const auto before = snapshot();
    m_theme = theme;
    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return false;
    }
    commitUndoSnapshot(before);
    syncPropertiesFromSelection();
    emit statusMessage(tr("Runtime UI theme updated."));
    return true;
}

bool UIEditorWidget::setEditorScale(const float scale, QString& errorMessage) {
    fabgl::RuntimeUI validator;
    const auto valid = validator.setScale(scale);
    if (!valid) {
        errorMessage = errorText(valid.error());
        return false;
    }
    if (m_scale == scale)
        return true;
    const auto before = snapshot();
    m_scale = scale;
    if (!rebuildRuntime(errorMessage)) {
        restoreSnapshot(before);
        return false;
    }
    commitUndoSnapshot(before);
    syncPropertiesFromSelection();
    emit statusMessage(tr("Runtime UI scale updated."));
    return true;
}

qsizetype UIEditorWidget::widgetCount() const noexcept {
    return static_cast<qsizetype>(m_nodes.size());
}

std::uint32_t UIEditorWidget::selectedWidgetId() const noexcept {
    return m_selectedId;
}

std::uint32_t UIEditorWidget::parentOf(const std::uint32_t id) const noexcept {
    const auto* target = node(id);
    return target == nullptr ? 0U : target->parentId;
}

std::uint64_t UIEditorWidget::previewChecksum() const noexcept {
    return m_previewChecksum;
}

bool UIEditorWidget::canUndo() const noexcept {
    return !m_undoHistory.empty();
}

bool UIEditorWidget::canRedo() const noexcept {
    return !m_redoHistory.empty();
}

const fabgl::RuntimeUI& UIEditorWidget::runtimeUI() const noexcept {
    return m_runtime;
}

UIEditorWidget::Snapshot UIEditorWidget::snapshot() const {
    return {m_nodes, m_nextId, m_selectedId, m_theme, m_scale};
}

void UIEditorWidget::restoreSnapshot(Snapshot value) {
    m_nodes = std::move(value.nodes);
    m_nextId = value.nextId;
    m_selectedId = value.selectedId;
    m_theme = value.theme;
    m_scale = value.scale;
    QString error;
    if (!rebuildRuntime(error))
        m_status->setText(error);
    refreshHierarchy();
    refreshParentChoices();
    syncPropertiesFromSelection();
}

void UIEditorWidget::commitUndoSnapshot(Snapshot value) {
    constexpr std::size_t MaximumHistory = 100U;
    if (m_undoHistory.size() >= MaximumHistory)
        m_undoHistory.erase(m_undoHistory.begin());
    m_undoHistory.push_back(std::move(value));
    m_redoHistory.clear();
    updateHistoryActions();
}

void UIEditorWidget::updateHistoryActions() {
    if (m_undoButton != nullptr)
        m_undoButton->setEnabled(canUndo());
    if (m_redoButton != nullptr)
        m_redoButton->setEnabled(canRedo());
}

void UIEditorWidget::undo() {
    if (m_undoHistory.empty())
        return;
    constexpr std::size_t MaximumHistory = 100U;
    if (m_redoHistory.size() >= MaximumHistory)
        m_redoHistory.erase(m_redoHistory.begin());
    m_redoHistory.push_back(snapshot());
    auto value = std::move(m_undoHistory.back());
    m_undoHistory.pop_back();
    restoreSnapshot(std::move(value));
    updateHistoryActions();
    emit statusMessage(tr("UI authoring edit undone."));
}

void UIEditorWidget::redo() {
    if (m_redoHistory.empty())
        return;
    constexpr std::size_t MaximumHistory = 100U;
    if (m_undoHistory.size() >= MaximumHistory)
        m_undoHistory.erase(m_undoHistory.begin());
    m_undoHistory.push_back(snapshot());
    auto value = std::move(m_redoHistory.back());
    m_redoHistory.pop_back();
    restoreSnapshot(std::move(value));
    updateHistoryActions();
    emit statusMessage(tr("UI authoring edit redone."));
}

bool UIEditorWidget::rebuildRuntime(QString& errorMessage) {
    fabgl::RuntimeUI candidate;
    auto configured = candidate.setTheme(m_theme);
    if (!configured) {
        errorMessage = errorText(configured.error());
        return false;
    }
    configured = candidate.setScale(m_scale);
    if (!configured) {
        errorMessage = errorText(configured.error());
        return false;
    }

    std::vector<std::pair<std::uint32_t, fabgl::UIElementId>> runtimeIds;
    runtimeIds.reserve(m_nodes.size());
    std::vector<bool> added(m_nodes.size(), false);
    while (runtimeIds.size() < m_nodes.size()) {
        bool progressed = false;
        for (std::size_t index = 0U; index < m_nodes.size(); ++index) {
            if (added[index])
                continue;
            const auto& source = m_nodes[index];
            std::optional<fabgl::UIElementId> parent;
            if (source.parentId != 0U) {
                const auto parentIterator =
                    std::find_if(runtimeIds.begin(), runtimeIds.end(), [&source](const auto& item) {
                        return item.first == source.parentId;
                    });
                if (parentIterator == runtimeIds.end())
                    continue;
                parent = parentIterator->second;
            }
            auto created = candidate.addWidget(source.type, parent, source.properties);
            if (!created) {
                errorMessage = errorText(created.error());
                return false;
            }
            auto textResult = candidate.setText(created.value(), source.text.toStdString());
            if (!textResult) {
                errorMessage = errorText(textResult.error());
                return false;
            }
            if (source.type == fabgl::UIWidgetType::Slider ||
                source.type == fabgl::UIWidgetType::Progress) {
                auto rangeResult = candidate.setRange(created.value(), 0.0F, 1.0F, 0.1F);
                if (!rangeResult) {
                    errorMessage = errorText(rangeResult.error());
                    return false;
                }
                auto valueResult = candidate.setValue(created.value(), 0.5F);
                if (!valueResult) {
                    errorMessage = errorText(valueResult.error());
                    return false;
                }
            } else if (source.type == fabgl::UIWidgetType::List) {
                auto itemsResult =
                    candidate.setItems(created.value(), {"First", "Second", "Third"});
                if (!itemsResult) {
                    errorMessage = errorText(itemsResult.error());
                    return false;
                }
            }
            runtimeIds.emplace_back(source.id, created.value());
            added[index] = true;
            progressed = true;
        }
        if (!progressed) {
            errorMessage = tr("UI hierarchy contains a cycle or a missing parent.");
            return false;
        }
    }

    auto laidOut = candidate.layout({0.0F, 0.0F, 320.0F, 180.0F});
    if (!laidOut) {
        errorMessage = errorText(laidOut.error());
        return false;
    }
    m_runtime = std::move(candidate);
    m_runtimeIds = std::move(runtimeIds);
    refreshPreview();
    return true;
}

void UIEditorWidget::refreshHierarchy() {
    if (m_hierarchy == nullptr)
        return;
    m_updatingControls = true;
    m_hierarchy->clear();
    std::vector<std::pair<std::uint32_t, QTreeWidgetItem*>> items;
    items.reserve(m_nodes.size());
    std::vector<bool> added(m_nodes.size(), false);
    while (items.size() < m_nodes.size()) {
        bool progressed = false;
        for (std::size_t index = 0U; index < m_nodes.size(); ++index) {
            if (added[index])
                continue;
            const auto& source = m_nodes[index];
            QTreeWidgetItem* parentItem = nullptr;
            if (source.parentId != 0U) {
                const auto parentIterator =
                    std::find_if(items.begin(), items.end(), [&source](const auto& item) {
                        return item.first == source.parentId;
                    });
                if (parentIterator == items.end())
                    continue;
                parentItem = parentIterator->second;
            }
            auto* created = parentItem == nullptr ? new QTreeWidgetItem(m_hierarchy)
                                                  : new QTreeWidgetItem(parentItem);
            const auto suffix =
                source.text.isEmpty() ? QString{} : QStringLiteral(" — %1").arg(source.text);
            created->setText(0, QStringLiteral("%1 #%2%3")
                                    .arg(uiWidgetTypeName(source.type))
                                    .arg(source.id)
                                    .arg(suffix));
            created->setData(0, Qt::UserRole, source.id);
            created->setExpanded(true);
            items.emplace_back(source.id, created);
            added[index] = true;
            progressed = true;
        }
        if (!progressed)
            break;
    }
    const auto selected = std::find_if(items.begin(), items.end(), [this](const auto& item) {
        return item.first == m_selectedId;
    });
    if (selected != items.end())
        m_hierarchy->setCurrentItem(selected->second);
    m_updatingControls = false;
}

void UIEditorWidget::refreshParentChoices() {
    if (m_parent == nullptr)
        return;
    const QSignalBlocker blocker(m_parent);
    const auto* selected = node(m_selectedId);
    const auto currentParent = selected == nullptr ? 0U : selected->parentId;
    m_parent->clear();
    m_parent->addItem(tr("(UI root)"), 0U);
    for (const auto& candidate : m_nodes) {
        if (candidate.id == m_selectedId)
            continue;
        m_parent->addItem(
            QStringLiteral("%1 #%2").arg(uiWidgetTypeName(candidate.type)).arg(candidate.id),
            candidate.id);
    }
    const auto index = m_parent->findData(currentParent);
    m_parent->setCurrentIndex(index < 0 ? 0 : index);
}

void UIEditorWidget::syncPropertiesFromSelection() {
    if (m_text == nullptr)
        return;
    m_updatingControls = true;
    const auto* selected = node(m_selectedId);
    const bool available = selected != nullptr;
    m_removeButton->setEnabled(available);
    for (auto* control : {static_cast<QWidget*>(m_text), static_cast<QWidget*>(m_anchorMinX),
                          static_cast<QWidget*>(m_anchorMinY), static_cast<QWidget*>(m_anchorMaxX),
                          static_cast<QWidget*>(m_anchorMaxY), static_cast<QWidget*>(m_offsetMinX),
                          static_cast<QWidget*>(m_offsetMinY), static_cast<QWidget*>(m_offsetMaxX),
                          static_cast<QWidget*>(m_offsetMaxY), static_cast<QWidget*>(m_visible),
                          static_cast<QWidget*>(m_enabled)}) {
        control->setEnabled(available);
    }
    if (selected != nullptr) {
        m_text->setText(selected->text);
        m_anchorMinX->setValue(selected->properties.anchors.minimum.x);
        m_anchorMinY->setValue(selected->properties.anchors.minimum.y);
        m_anchorMaxX->setValue(selected->properties.anchors.maximum.x);
        m_anchorMaxY->setValue(selected->properties.anchors.maximum.y);
        m_offsetMinX->setValue(selected->properties.minimumOffset.x);
        m_offsetMinY->setValue(selected->properties.minimumOffset.y);
        m_offsetMaxX->setValue(selected->properties.maximumOffset.x);
        m_offsetMaxY->setValue(selected->properties.maximumOffset.y);
        m_visible->setChecked(selected->properties.visible);
        m_enabled->setChecked(selected->properties.enabled);
    } else {
        m_text->clear();
    }
    m_themeCombo->setCurrentIndex(isLightUITheme(m_theme) ? 1 : 0);
    m_scaleSpin->setValue(m_scale);
    m_updatingControls = false;
    updateHistoryActions();
}

UIEditorWidget::Node* UIEditorWidget::node(const std::uint32_t id) noexcept {
    const auto iterator = std::find_if(m_nodes.begin(), m_nodes.end(),
                                       [id](const Node& candidate) { return candidate.id == id; });
    return iterator == m_nodes.end() ? nullptr : &*iterator;
}

const UIEditorWidget::Node* UIEditorWidget::node(const std::uint32_t id) const noexcept {
    const auto iterator = std::find_if(m_nodes.begin(), m_nodes.end(),
                                       [id](const Node& candidate) { return candidate.id == id; });
    return iterator == m_nodes.end() ? nullptr : &*iterator;
}

void UIEditorWidget::refreshPreview() {
    if (m_preview == nullptr)
        return;
    fabgl::rendering::Framebuffer framebuffer(320, 180);
    framebuffer.clear(m_theme.panel);
    auto laidOut = m_runtime.layout({0.0F, 0.0F, 320.0F, 180.0F});
    if (!laidOut) {
        m_previewChecksum = framebuffer.checksum();
        showFramebuffer(m_preview, framebuffer);
        m_status->setText(errorText(laidOut.error()));
        return;
    }

    const auto effectivelyVisible = [this](const Node& source) {
        const Node* cursor = &source;
        for (std::size_t depth = 0U; cursor != nullptr && depth <= m_nodes.size(); ++depth) {
            if (!cursor->properties.visible)
                return false;
            cursor = cursor->parentId == 0U ? nullptr : node(cursor->parentId);
        }
        return true;
    };
    for (const auto& source : m_nodes) {
        if (!effectivelyVisible(source))
            continue;
        const auto runtimeIterator =
            std::find_if(m_runtimeIds.begin(), m_runtimeIds.end(),
                         [&source](const auto& item) { return item.first == source.id; });
        if (runtimeIterator == m_runtimeIds.end())
            continue;
        const auto rectangle = m_runtime.screenRect(runtimeIterator->second);
        if (!rectangle)
            continue;
        const int left = static_cast<int>(std::lround(rectangle->x));
        const int top = static_cast<int>(std::lround(rectangle->y));
        const int width = std::max(0, static_cast<int>(std::lround(rectangle->width)));
        const int height = std::max(0, static_cast<int>(std::lround(rectangle->height)));
        if (width == 0 || height == 0)
            continue;

        fabgl::Color fill = lerpColor(m_theme.panel, m_theme.foreground, 0.12F);
        if (source.type == fabgl::UIWidgetType::Button ||
            source.type == fabgl::UIWidgetType::Toggle) {
            fill = lerpColor(m_theme.panel, m_theme.accent, 0.55F);
        } else if (source.type == fabgl::UIWidgetType::Image) {
            fill = lerpColor(m_theme.panel, m_theme.accent, 0.25F);
        } else if (source.type == fabgl::UIWidgetType::Slider ||
                   source.type == fabgl::UIWidgetType::Progress) {
            fill = lerpColor(m_theme.panel, m_theme.accent, 0.4F);
        } else if (source.type == fabgl::UIWidgetType::Layout) {
            fill = lerpColor(m_theme.panel, m_theme.foreground, 0.06F);
        }
        if (!source.properties.enabled)
            fill = lerpColor(fill, m_theme.disabled, 0.7F);
        framebuffer.fillRect(left, top, width, height, fill);
        const auto border = source.id == m_selectedId ? m_theme.accent : m_theme.foreground;
        framebuffer.drawLine(left, top, left + width - 1, top, border);
        framebuffer.drawLine(left, top, left, top + height - 1, border);
        framebuffer.drawLine(left + width - 1, top, left + width - 1, top + height - 1, border);
        framebuffer.drawLine(left, top + height - 1, left + width - 1, top + height - 1, border);

        std::uint32_t hash = 2166136261U;
        const auto bytes = source.text.toUtf8();
        for (const auto character : bytes) {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 16777619U;
        }
        const int sampleCount = std::min(12, std::max(0, width - 6));
        for (int sample = 0; sample < sampleCount; ++sample) {
            if ((hash & (1U << (sample % 24))) != 0U) {
                const int bottom = std::min(top + height - 3, top + 3 + sample % 5);
                framebuffer.drawLine(left + 3 + sample, top + 3, left + 3 + sample, bottom,
                                     m_theme.foreground);
            }
        }
    }
    m_previewChecksum = framebuffer.checksum();
    showFramebuffer(m_preview, framebuffer);
    m_status->setText(tr("%1 widgets | RuntimeUI scale %2 | deterministic checksum %3")
                          .arg(widgetCount())
                          .arg(m_scale, 0, 'f', 2)
                          .arg(QString::number(m_previewChecksum, 16)));
}

PackageManagerWidget::PackageManagerWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("packageManagerPanel"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* form = new QFormLayout();
    m_cliPath = new QLineEdit(this);
    m_cliPath->setObjectName(QStringLiteral("packageCliPathEdit"));
    m_projectPath = new QLineEdit(this);
    m_projectPath->setObjectName(QStringLiteral("packageProjectManifestEdit"));
    m_sourcePath = new QLineEdit(this);
    m_sourcePath->setObjectName(QStringLiteral("packageSourceDirectoryEdit"));
    m_packageId = new QLineEdit(this);
    m_packageId->setObjectName(QStringLiteral("packageIdEdit"));
    m_allowExecutable = new QCheckBox(tr("Reviewed executable package"), this);
    m_allowExecutable->setObjectName(QStringLiteral("packageAllowExecutableCheck"));
    form->addRow(tr("Project CLI"), m_cliPath);
    form->addRow(tr("Project manifest"), m_projectPath);
    form->addRow(tr("Package source"), m_sourcePath);
    form->addRow(tr("Package ID"), m_packageId);
    form->addRow(tr("Executable approval"), m_allowExecutable);
    root->addLayout(form);
    m_trustStatus = new QLabel(this);
    m_trustStatus->setObjectName(QStringLiteral("packageTrustStatus"));
    root->addWidget(m_trustStatus);
    auto* buttons = new QHBoxLayout();
    auto* install = new QPushButton(tr("Prepare Install"), this);
    install->setObjectName(QStringLiteral("packagePrepareInstallButton"));
    auto* list = new QPushButton(tr("Prepare List"), this);
    list->setObjectName(QStringLiteral("packagePrepareListButton"));
    auto* validate = new QPushButton(tr("Prepare Validate"), this);
    validate->setObjectName(QStringLiteral("packagePrepareValidateButton"));
    auto* remove = new QPushButton(tr("Prepare Remove"), this);
    remove->setObjectName(QStringLiteral("packagePrepareRemoveButton"));
    for (auto* button : {install, list, validate, remove})
        buttons->addWidget(button);
    root->addLayout(buttons);
    m_commandPreview = new QLabel(this);
    m_commandPreview->setObjectName(QStringLiteral("packageCommandPreview"));
    m_commandPreview->setWordWrap(true);
    root->addWidget(m_commandPreview);
    root->addStretch();

    const auto prepare = [this](const PackageCliCommand command) {
        QStringList quoted;
        for (const auto& argument : command.arguments)
            quoted.push_back(QStringLiteral("\"") + argument + QStringLiteral("\""));
        m_commandPreview->setText(command.program + QStringLiteral(" ") + quoted.join(' '));
        emit commandPrepared(command.program, command.arguments);
    };
    connect(install, &QPushButton::clicked, this, [this, prepare]() { prepare(installCommand()); });
    connect(list, &QPushButton::clicked, this, [this, prepare]() { prepare(listCommand()); });
    connect(validate, &QPushButton::clicked, this,
            [this, prepare]() { prepare(validateCommand()); });
    connect(remove, &QPushButton::clicked, this, [this, prepare]() { prepare(removeCommand()); });
    connect(m_allowExecutable, &QCheckBox::toggled, this, [this](bool) { refreshTrustStatus(); });
    refreshTrustStatus();
}

void PackageManagerWidget::setProjectCliPath(QString path) {
    m_cliPath->setText(std::move(path));
}

void PackageManagerWidget::setProjectManifestPath(QString path) {
    m_projectPath->setText(std::move(path));
}

void PackageManagerWidget::setPackageSourcePath(QString path) {
    m_sourcePath->setText(std::move(path));
}

void PackageManagerWidget::setPackageId(QString packageId) {
    m_packageId->setText(std::move(packageId));
}

void PackageManagerWidget::setProjectTrusted(const bool trusted) {
    m_projectTrusted = trusted;
    refreshTrustStatus();
}

void PackageManagerWidget::setAllowExecutablePackage(const bool allow) {
    m_allowExecutable->setChecked(allow);
    refreshTrustStatus();
}

bool PackageManagerWidget::projectTrusted() const noexcept {
    return m_projectTrusted;
}

QString PackageManagerWidget::trustStatus() const {
    return m_trustStatus->text();
}

PackageCliCommand PackageManagerWidget::installCommand() const {
    PackageCliCommand command;
    command.program = m_cliPath->text().trimmed();
    command.arguments = {QStringLiteral("package"), QStringLiteral("install"),
                         m_projectPath->text().trimmed(), m_sourcePath->text().trimmed()};
    command.executableApprovalIncluded = canInstallExecutablePackage();
    if (command.executableApprovalIncluded)
        command.arguments.push_back(QStringLiteral("--allow-executable"));
    return command;
}

PackageCliCommand PackageManagerWidget::listCommand() const {
    return {m_cliPath->text().trimmed(),
            {QStringLiteral("package"), QStringLiteral("list"), m_projectPath->text().trimmed()},
            false};
}

PackageCliCommand PackageManagerWidget::validateCommand() const {
    return {
        m_cliPath->text().trimmed(),
        {QStringLiteral("package"), QStringLiteral("validate"), m_projectPath->text().trimmed()},
        false};
}

PackageCliCommand PackageManagerWidget::removeCommand() const {
    return {m_cliPath->text().trimmed(),
            {QStringLiteral("package"), QStringLiteral("remove"), m_projectPath->text().trimmed(),
             m_packageId->text().trimmed()},
            false};
}

bool PackageManagerWidget::canInstallExecutablePackage() const noexcept {
    return m_projectTrusted && m_allowExecutable->isChecked();
}

void PackageManagerWidget::refreshTrustStatus() {
    if (!m_projectTrusted) {
        m_trustStatus->setText(
            tr("Project untrusted: executable approval is excluded from command vectors."));
    } else if (m_allowExecutable->isChecked()) {
        m_trustStatus->setText(
            tr("Trusted project: content-bound executable approval will be requested."));
    } else {
        m_trustStatus->setText(tr("Trusted project: data-only package install."));
    }
}

AudioMixerEditorWidget::AudioMixerEditorWidget(QWidget* parent)
    : QWidget(parent), m_mixer(fabgl::AudioMixerConfig{48'000U, 8U, 8U, 512U}) {
    setObjectName(QStringLiteral("audioMixerEditor"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    m_busTable = new QTableWidget(0, 4, this);
    m_busTable->setObjectName(QStringLiteral("audioMixerBusTable"));
    m_busTable->setHorizontalHeaderLabels({tr("Bus"), tr("Volume"), tr("Pan"), tr("Muted")});
    m_busTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_busTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_busTable, 1);
    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName(QStringLiteral("audioMixerStatsLabel"));
    m_statsLabel->setWordWrap(true);
    root->addWidget(m_statsLabel);

    constexpr std::array buses{
        std::pair{fabgl::AudioBusId{1U}, fabgl::AudioBusSettings{0.8F, 0.0F, false}},
        std::pair{fabgl::AudioBusId{2U}, fabgl::AudioBusSettings{1.0F, 0.0F, false}},
        std::pair{fabgl::AudioBusId{3U}, fabgl::AudioBusSettings{0.9F, 0.0F, false}},
    };
    for (const auto& [id, settings] : buses) {
        const auto created = m_mixer.createBus(id, settings);
        if (!created) {
            emit statusMessage(errorText(created.error()));
        }
    }
    refreshTable();
}

bool AudioMixerEditorWidget::setBusVolume(const fabgl::AudioBusId bus, const float volume,
                                          QString& errorMessage) {
    const auto result = m_mixer.setBusVolume(bus, volume);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTable();
    emit statusMessage(tr("Updated audio bus %1 volume.").arg(bus.value));
    return true;
}

bool AudioMixerEditorWidget::setBusPan(const fabgl::AudioBusId bus, const float pan,
                                       QString& errorMessage) {
    const auto result = m_mixer.setBusPan(bus, pan);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTable();
    emit statusMessage(tr("Updated audio bus %1 pan.").arg(bus.value));
    return true;
}

bool AudioMixerEditorWidget::setBusMuted(const fabgl::AudioBusId bus, const bool muted,
                                         QString& errorMessage) {
    const auto result = m_mixer.setBusMuted(bus, muted);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTable();
    emit statusMessage(tr("Updated audio bus %1 mute state.").arg(bus.value));
    return true;
}

const fabgl::AudioBusSettings*
AudioMixerEditorWidget::busSettings(const fabgl::AudioBusId bus) const noexcept {
    return m_mixer.busSettings(bus);
}

fabgl::AudioMixerStats AudioMixerEditorWidget::mixerStats() const noexcept {
    return m_mixer.stats();
}

bool AudioMixerEditorWidget::renderTestTone(const fabgl::AudioBusId bus, const std::size_t frames,
                                            QString& errorMessage) {
    constexpr std::size_t MaximumPreviewFrames = 480'000U;
    if (frames == 0U || frames > MaximumPreviewFrames || m_mixer.busSettings(bus) == nullptr) {
        errorMessage = tr("Audio preview bus is unknown or frame count exceeds the limit.");
        return false;
    }

    m_mixer.stopAll();
    m_testTone.resize(frames);
    constexpr double Pi = 3.14159265358979323846;
    constexpr double Frequency = 440.0;
    constexpr double SampleRate = 48'000.0;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const auto phase = (2.0 * Pi * Frequency * static_cast<double>(frame)) / SampleRate;
        m_testTone[frame] = static_cast<float>(std::sin(phase) * 0.25);
    }

    fabgl::AudioClipView clip;
    clip.interleavedSamples = m_testTone.data();
    clip.frameCount = frames;
    clip.channelCount = 1U;
    clip.sampleRate = m_mixer.outputSampleRate();
    fabgl::AudioVoiceSettings voice;
    voice.bus = bus;
    auto played = m_mixer.play(clip, voice);
    if (!played) {
        errorMessage = errorText(played.error());
        return false;
    }

    std::vector<float> mixed(frames * 2U, 0.0F);
    auto rendered = m_mixer.mixTo(mixed.data(), frames);
    if (!rendered) {
        errorMessage = errorText(rendered.error());
        m_mixer.stopAll();
        return false;
    }
    m_mixer.stopAll();

    constexpr std::uint64_t FnvOffset = 1469598103934665603ULL;
    constexpr std::uint64_t FnvPrime = 1099511628211ULL;
    m_lastMixChecksum = FnvOffset;
    m_lastNonZeroSamples = 0U;
    for (const auto sample : mixed) {
        const auto bounded = std::clamp(sample, -1.0F, 1.0F);
        const auto quantized = static_cast<std::int16_t>(std::lround(bounded * 32767.0F));
        const auto bits = static_cast<std::uint16_t>(quantized);
        if (quantized != 0) {
            ++m_lastNonZeroSamples;
        }
        m_lastMixChecksum ^= static_cast<std::uint8_t>(bits & 0xFFU);
        m_lastMixChecksum *= FnvPrime;
        m_lastMixChecksum ^= static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
        m_lastMixChecksum *= FnvPrime;
    }
    refreshTable();
    emit statusMessage(tr("Rendered %1 deterministic stereo preview frames on bus %2.")
                           .arg(static_cast<qulonglong>(frames))
                           .arg(bus.value));
    return true;
}

std::uint64_t AudioMixerEditorWidget::lastMixChecksum() const noexcept {
    return m_lastMixChecksum;
}

std::uint64_t AudioMixerEditorWidget::lastNonZeroSamples() const noexcept {
    return m_lastNonZeroSamples;
}

void AudioMixerEditorWidget::refreshTable() {
    constexpr std::array buses{fabgl::MasterAudioBus, fabgl::AudioBusId{1U}, fabgl::AudioBusId{2U},
                               fabgl::AudioBusId{3U}};
    constexpr std::array names{"Master", "Music", "SFX", "UI"};
    m_busTable->setRowCount(static_cast<int>(buses.size()));
    for (std::size_t index = 0U; index < buses.size(); ++index) {
        const auto row = static_cast<int>(index);
        const auto bus = buses[index];
        const auto* settings = m_mixer.busSettings(buses[index]);
        m_busTable->setItem(row, 0, item(QString::fromLatin1(names[index])));
        if (settings == nullptr) {
            for (int column = 1; column < 4; ++column) {
                m_busTable->setItem(row, column, item(tr("Missing")));
            }
            continue;
        }

        auto* volume = new QDoubleSpinBox(m_busTable);
        volume->setObjectName(QStringLiteral("audioBus%1VolumeSpin").arg(bus.value));
        volume->setRange(0.0, 4.0);
        volume->setDecimals(3);
        volume->setSingleStep(0.05);
        volume->setValue(settings->volume);
        connect(volume, &QDoubleSpinBox::valueChanged, this, [this, bus](const double value) {
            const auto updated = m_mixer.setBusVolume(bus, static_cast<float>(value));
            emit statusMessage(updated ? tr("Updated audio bus %1 volume.").arg(bus.value)
                                       : errorText(updated.error()));
        });
        m_busTable->setCellWidget(row, 1, volume);

        auto* pan = new QDoubleSpinBox(m_busTable);
        pan->setObjectName(QStringLiteral("audioBus%1PanSpin").arg(bus.value));
        pan->setRange(-1.0, 1.0);
        pan->setDecimals(3);
        pan->setSingleStep(0.05);
        pan->setValue(settings->pan);
        connect(pan, &QDoubleSpinBox::valueChanged, this, [this, bus](const double value) {
            const auto updated = m_mixer.setBusPan(bus, static_cast<float>(value));
            emit statusMessage(updated ? tr("Updated audio bus %1 pan.").arg(bus.value)
                                       : errorText(updated.error()));
        });
        m_busTable->setCellWidget(row, 2, pan);

        auto* muted = new QCheckBox(tr("Muted"), m_busTable);
        muted->setObjectName(QStringLiteral("audioBus%1MutedCheck").arg(bus.value));
        muted->setChecked(settings->muted);
        connect(muted, &QCheckBox::toggled, this, [this, bus](const bool checked) {
            const auto updated = m_mixer.setBusMuted(bus, checked);
            emit statusMessage(updated ? tr("Updated audio bus %1 mute state.").arg(bus.value)
                                       : errorText(updated.error()));
        });
        m_busTable->setCellWidget(row, 3, muted);
    }
    const auto stats = m_mixer.stats();
    m_statsLabel->setText(
        tr("Voices %1/%2 | started %3 | stolen %4 | mixed %5 frames | checksum %6")
            .arg(static_cast<qulonglong>(stats.activeVoices))
            .arg(static_cast<qulonglong>(stats.maximumVoices))
            .arg(static_cast<qulonglong>(stats.voicesStarted))
            .arg(static_cast<qulonglong>(stats.voicesStolen))
            .arg(static_cast<qulonglong>(stats.mixedFrames))
            .arg(QString::number(m_lastMixChecksum, 16)));
}

ProfilerTimelineWidget::ProfilerTimelineWidget(QWidget* parent)
    : QWidget(parent), m_profiler(fabgl::ProfilerConfig{512U, 64U, 16U}) {
    setObjectName(QStringLiteral("profilerTimeline"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    m_legend = new QLabel(
        tr("Measured PC = blue | Measured ESP32 = green | Estimated ESP32 = amber"), this);
    m_legend->setObjectName(QStringLiteral("profilerTimelineLegend"));
    root->addWidget(m_legend);
    m_timeline = new QTableWidget(0, 6, this);
    m_timeline->setObjectName(QStringLiteral("profilerTimelineTable"));
    m_timeline->setHorizontalHeaderLabels(
        {tr("Sequence"), tr("Metric"), tr("Value"), tr("Unit"), tr("Source"), tr("Budget")});
    m_timeline->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_timeline, 1);
    refreshTimeline();
}

bool ProfilerTimelineWidget::recordMeasuredPc(QString metric, const double value,
                                              const fabgl::ProfilerUnit unit,
                                              QString& errorMessage) {
    metric = metric.trimmed();
    auto result = m_profiler.recordMeasured(metric.toStdString(), value, unit,
                                            fabgl::ProfilerSampleSource::MeasuredPc);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTimeline();
    return true;
}

bool ProfilerTimelineWidget::recordMeasuredEsp32(QString metric, const double value,
                                                 const fabgl::ProfilerUnit unit,
                                                 QString& errorMessage) {
    metric = metric.trimmed();
    auto result = m_profiler.recordMeasured(metric.toStdString(), value, unit,
                                            fabgl::ProfilerSampleSource::MeasuredEsp32);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTimeline();
    return true;
}

bool ProfilerTimelineWidget::recordEstimatedEsp32(QString metric, const double value,
                                                  const fabgl::ProfilerUnit unit,
                                                  QString& errorMessage) {
    metric = metric.trimmed();
    auto result = m_profiler.recordEstimated(metric.toStdString(), value, unit);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTimeline();
    return true;
}

bool ProfilerTimelineWidget::setBudget(QString metric, const double maximum,
                                       const fabgl::ProfilerUnit unit, QString& errorMessage) {
    metric = metric.trimmed();
    auto result = m_profiler.setBudget(metric.toStdString(), maximum, unit);
    if (!result) {
        errorMessage = errorText(result.error());
        return false;
    }
    refreshTimeline();
    return true;
}

std::size_t ProfilerTimelineWidget::sampleCount() const noexcept {
    return m_profiler.sampleCount();
}

qsizetype ProfilerTimelineWidget::measuredPcCount() const noexcept {
    qsizetype count = 0;
    for (std::size_t index = 0U; index < m_profiler.sampleCount(); ++index) {
        const auto* sample = m_profiler.sampleAt(index);
        if (sample != nullptr && sample->source == fabgl::ProfilerSampleSource::MeasuredPc) {
            ++count;
        }
    }
    return count;
}

qsizetype ProfilerTimelineWidget::measuredEsp32Count() const noexcept {
    qsizetype count = 0;
    for (std::size_t index = 0U; index < m_profiler.sampleCount(); ++index) {
        const auto* sample = m_profiler.sampleAt(index);
        if (sample != nullptr && sample->source == fabgl::ProfilerSampleSource::MeasuredEsp32) {
            ++count;
        }
    }
    return count;
}

qsizetype ProfilerTimelineWidget::estimatedEsp32Count() const noexcept {
    qsizetype count = 0;
    for (std::size_t index = 0U; index < m_profiler.sampleCount(); ++index) {
        const auto* sample = m_profiler.sampleAt(index);
        if (sample != nullptr && sample->source == fabgl::ProfilerSampleSource::EstimatedEsp32) {
            ++count;
        }
    }
    return count;
}

fabgl::Result<fabgl::ProfilerSummary>
ProfilerTimelineWidget::summary(const QString& metric,
                                const fabgl::ProfilerSampleSource source) const {
    return m_profiler.summary(metric.trimmed().toStdString(), source);
}

void ProfilerTimelineWidget::refreshTimeline() {
    const auto unitName = [this](const fabgl::ProfilerUnit unit) {
        switch (unit) {
        case fabgl::ProfilerUnit::Milliseconds:
            return tr("ms");
        case fabgl::ProfilerUnit::Bytes:
            return tr("bytes");
        case fabgl::ProfilerUnit::Count:
            return tr("count");
        case fabgl::ProfilerUnit::Percent:
            return tr("percent");
        }
        return tr("unknown");
    };
    const auto sourceName = [this](const fabgl::ProfilerSampleSource source) {
        switch (source) {
        case fabgl::ProfilerSampleSource::MeasuredPc:
            return tr("Measured PC");
        case fabgl::ProfilerSampleSource::MeasuredEsp32:
            return tr("Measured ESP32");
        case fabgl::ProfilerSampleSource::EstimatedEsp32:
            return tr("Estimated ESP32");
        }
        return tr("Unknown");
    };
    const auto sourceColor = [](const fabgl::ProfilerSampleSource source) {
        switch (source) {
        case fabgl::ProfilerSampleSource::MeasuredPc:
            return QColor(96, 165, 250, 72);
        case fabgl::ProfilerSampleSource::MeasuredEsp32:
            return QColor(74, 222, 128, 72);
        case fabgl::ProfilerSampleSource::EstimatedEsp32:
            return QColor(251, 191, 36, 72);
        }
        return QColor(Qt::transparent);
    };

    m_timeline->setRowCount(static_cast<int>(m_profiler.sampleCount()));
    for (std::size_t index = 0U; index < m_profiler.sampleCount(); ++index) {
        const auto* sample = m_profiler.sampleAt(index);
        if (sample == nullptr) {
            continue;
        }
        const auto row = static_cast<int>(index);
        const auto* budget = m_profiler.budget(sample->metric);
        const auto exceeded =
            budget != nullptr && budget->unit == sample->unit && sample->value > budget->maximum;
        const auto budgetText = budget == nullptr
                                    ? tr("None")
                                    : tr("%1 %2%3")
                                          .arg(budget->maximum, 0, 'g', 12)
                                          .arg(unitName(budget->unit))
                                          .arg(exceeded ? tr(" (exceeded)") : QString{});
        const std::array values{QString::number(static_cast<qulonglong>(sample->sequence)),
                                QString::fromStdString(sample->metric),
                                QString::number(sample->value, 'g', 12),
                                unitName(sample->unit),
                                sourceName(sample->source),
                                budgetText};
        for (std::size_t column = 0U; column < values.size(); ++column) {
            auto* cell = item(values[column]);
            cell->setBackground(sourceColor(sample->source));
            if (exceeded && column == values.size() - 1U) {
                cell->setForeground(QColor(220, 38, 38));
            }
            m_timeline->setItem(row, static_cast<int>(column), cell);
        }
    }
    emit statusMessage(tr("Profiler timeline contains %1 bounded samples.")
                           .arg(static_cast<qulonglong>(m_profiler.sampleCount())));
}

} // namespace fgl::studio
