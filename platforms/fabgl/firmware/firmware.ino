/*
  FabGL Studio hardware smoke and interactive diagnostic firmware.

  Copyright (C) 2026 FabGL Studio contributors.
  SPDX-License-Identifier: GPL-3.0-or-later

  This firmware links FabGL, which is GPL-3.0-or-later. It intentionally does
  not contain flashing or self-update logic. The host build is compile-only.
*/

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <fabgl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>

#include "BoardProfile.h"
#include "ProjectRuntime.h"
#include "ProjectSaveSdAdapter.h"
#include "ProjectScriptRuntime.h"

#if defined(__has_include)
#if __has_include("ProjectPayload.h")
#include "ProjectPayload.h"
#define FABGL_STUDIO_HAS_PROJECT_PAYLOAD 1
#endif
#endif

#ifndef FABGL_STUDIO_HAS_PROJECT_PAYLOAD
#define FABGL_STUDIO_HAS_PROJECT_PAYLOAD 0
#endif

#if defined(__has_include)
#if __has_include("ProjectScriptConfig.h")
#include "ProjectScriptConfig.h"
#endif
#endif

#ifndef FABGL_STUDIO_HAS_PROJECT_SCRIPTS
#define FABGL_STUDIO_HAS_PROJECT_SCRIPTS 0
#endif

#ifndef FABGL_STUDIO_PROJECT_SCRIPT_FILE_COUNT
#define FABGL_STUDIO_PROJECT_SCRIPT_FILE_COUNT 0U
#endif

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
extern "C" bool fabglProjectGetEsp32ScriptsV1(
    fabgl_project_scripts::ModuleView* output) noexcept __attribute__((weak));
#endif

#ifndef FABGL_STUDIO_SOAK_DIAGNOSTICS
#define FABGL_STUDIO_SOAK_DIAGNOSTICS 0
#endif

namespace {

constexpr char kProtocol[] = "FABGLSTUDIO";
constexpr int kProtocolVersion = 1;

fabgl::VGAController displayController;
fabgl::PS2Controller ps2Controller;
fabgl::Canvas canvas(&displayController);
fabgl::SoundGenerator soundGenerator(FABGL_SOUNDGEN_DEFAULT_SAMPLE_RATE, board_profile::kAudioDac,
                                     fabgl::SoundGenMethod::DAC);
SPIClass sdSpi(HSPI);

std::uint32_t frameCount = 0;
std::uint32_t metricWindowStart = 0;
std::uint32_t projectUpdateCount = 0U;
std::uint32_t projectRenderCount = 0U;
int spriteX = 16;
int spriteDirection = 1;
bool sdMounted = false;
bool projectMode = false;
std::uint32_t previousProjectFrame = 0U;

void emitRecord(const char* status, const char* check, const char* detail) {
    Serial.printf("%s|%d|%s|%s|%s\n", kProtocol, kProtocolVersion, status, check,
                  detail == nullptr ? "" : detail);
}

void emitCheck(const char* check, const bool passed, const char* detail) {
    emitRecord(passed ? "PASS" : "FAIL", check, detail);
}

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
constexpr std::size_t kPackHeaderSize = 32U;
constexpr std::size_t kPackIndexEntrySize = 40U;
constexpr std::uint16_t kPackVersion = 1U;
constexpr std::uint32_t kManifestPayloadType = 0x4D414E46U;
constexpr std::uint32_t kScenePayloadType = 0x53434E45U;
constexpr std::uint32_t kAssetPayloadType = 0x41535354U;
constexpr bool kReportInactiveProjectMetrics = FABGL_STUDIO_SOAK_DIAGNOSTICS != 0;

struct PayloadInspection final {
    bool valid = false;
    bool sceneV2 = false;
    std::uint32_t manifestCount = 0;
    std::uint32_t sceneCount = 0;
    std::uint32_t assetCount = 0;
};

struct ProgramPayloadReader final {
    [[nodiscard]] std::size_t size() const noexcept {
        return fabgl_project_payload::kPayloadSize;
    }
    [[nodiscard]] std::uint8_t byte(const std::size_t offset) const noexcept {
        return static_cast<std::uint8_t>(pgm_read_byte(fabgl_project_payload::kData + offset));
    }
};

ProgramPayloadReader projectReader;
fabgl_project_runtime::RuntimeProject projectRuntime;
fabgl_project_runtime::Failure projectFailure;
fabgl_project_scripts::Runtime projectScriptRuntime;
fabgl_project_save::SdStorageAdapter projectSaveStorage(SD);
fabgl_project_save::SaveService projectSaveService(
    projectSaveStorage.callbacks(), "/fabglstudio/saves",
    fabgl_project_save::kDefaultSchemaVersion);
fabgl_project_save::Document projectSaveDocument;
// The scratch codec bytes are overwritten on every operation and do not need normal DRAM. Keep
// them in the ESP32's bounded RTC slow-memory region so VGA/audio/project state retain internal
// RAM. The 4 KiB size is asserted by the save codec and fits the 8 KiB RTC slow-memory budget.
RTC_NOINIT_ATTR std::uint8_t projectSaveBuffer[fabgl_project_save::kMaximumFileBytes];
fabgl_project_save::Error projectSaveError = fabgl_project_save::Error::None;
std::uint32_t projectSaveSequence = 1U;

#if FABGL_STUDIO_SOAK_DIAGNOSTICS
constexpr std::uint32_t kSoakStepPeriodMs = 2000U;
constexpr std::size_t kSoakAssetCacheBytes = 64U;
fabgl_project_runtime::SoakWorkload soakWorkload;
std::uint8_t soakAssetCache[kSoakAssetCacheBytes]{};
std::uint64_t soakAssetChecksum = 0U;
std::uint32_t soakLastStep = 0U;
std::uint32_t soakReloadFailures = 0U;
#endif

void sanitizePayloadText(const char* source, char* destination, const std::size_t capacity) {
    if (capacity == 0U) {
        return;
    }
    std::size_t output = 0;
    while (*source != '\0' && output + 1U < capacity) {
        const auto character = static_cast<unsigned char>(*source++);
        destination[output++] =
            character < 0x20U || character > 0x7EU || character == '|' || character == ';'
                ? '_'
                : static_cast<char>(character);
    }
    destination[output] = '\0';
}

std::uint8_t payloadByte(const std::size_t offset) {
    return static_cast<std::uint8_t>(pgm_read_byte(fabgl_project_payload::kData + offset));
}

bool payloadRangeValid(const std::size_t offset, const std::size_t size) {
    return offset <= fabgl_project_payload::kPayloadSize &&
           size <= fabgl_project_payload::kPayloadSize - offset;
}

std::uint16_t payloadU16(const std::size_t offset) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(payloadByte(offset)) |
                                      (static_cast<std::uint16_t>(payloadByte(offset + 1U)) << 8U));
}

std::uint32_t payloadU32(const std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(payloadByte(offset + shift / 8U)) << shift;
    }
    return value;
}

std::uint64_t payloadU64(const std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        value |= static_cast<std::uint64_t>(payloadByte(offset + shift / 8U)) << shift;
    }
    return value;
}

std::uint64_t calculatePayloadChecksum(const std::size_t offset, const std::size_t size) {
    constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto checksum = fnvOffset;
    for (std::size_t index = 0; index < size; ++index) {
        checksum ^= payloadByte(offset + index);
        checksum *= prime;
    }
    return checksum;
}

bool payloadStartsWith(const std::size_t offset, const std::size_t size, const char* expected) {
    std::size_t index = 0;
    while (expected[index] != '\0') {
        if (index >= size ||
            payloadByte(offset + index) != static_cast<std::uint8_t>(expected[index])) {
            return false;
        }
        ++index;
    }
    return true;
}

bool assetPathIsSafe(const std::size_t offset, const std::size_t length) {
    if (length == 0U || payloadByte(offset) == static_cast<std::uint8_t>('/') ||
        payloadByte(offset) == static_cast<std::uint8_t>('\\')) {
        return false;
    }
    std::size_t segmentStart = 0;
    for (std::size_t index = 0; index <= length; ++index) {
        const auto character =
            index == length ? static_cast<std::uint8_t>('/') : payloadByte(offset + index);
        if (index < length && (character < 0x20U || character == static_cast<std::uint8_t>('\\') ||
                               character == static_cast<std::uint8_t>(':'))) {
            return false;
        }
        if (character != static_cast<std::uint8_t>('/')) {
            continue;
        }
        const auto segmentLength = index - segmentStart;
        if (segmentLength == 0U ||
            (segmentLength == 1U && payloadByte(offset + segmentStart) == '.') ||
            (segmentLength == 2U && payloadByte(offset + segmentStart) == '.' &&
             payloadByte(offset + segmentStart + 1U) == '.')) {
            return false;
        }
        segmentStart = index + 1U;
    }
    return true;
}

bool guidAtIndexIsStrictlySorted(const std::uint32_t index) {
    const auto current = kPackHeaderSize + static_cast<std::size_t>(index) * kPackIndexEntrySize;
    bool anyNonZero = false;
    for (std::size_t byte = 0; byte < 16U; ++byte) {
        anyNonZero = anyNonZero || payloadByte(current + byte) != 0U;
    }
    if (!anyNonZero) {
        return false;
    }
    if (index == 0U) {
        return true;
    }
    const auto previous = current - kPackIndexEntrySize;
    for (std::size_t byte = 0; byte < 16U; ++byte) {
        const auto previousByte = payloadByte(previous + byte);
        const auto currentByte = payloadByte(current + byte);
        if (previousByte != currentByte) {
            return previousByte < currentByte;
        }
    }
    return false;
}

PayloadInspection inspectProjectPayload() {
    PayloadInspection result;
    if (fabgl_project_payload::kPayloadSize < kPackHeaderSize ||
        !payloadStartsWith(0U, fabgl_project_payload::kPayloadSize, "FGLP") ||
        payloadU16(4U) != kPackVersion || payloadU16(6U) != 0U) {
        return result;
    }
    const auto entryCount = payloadU32(8U);
    const auto alignment = payloadU32(12U);
    const auto entrySize = payloadU32(16U);
    const auto dataOffset = payloadU32(20U);
    const auto declaredBuildChecksum = payloadU64(24U);
    const auto indexBytes = static_cast<std::uint64_t>(entryCount) * kPackIndexEntrySize;
    if (entryCount != fabgl_project_payload::kAssetCount + 2U || alignment == 0U ||
        alignment > 4096U || (alignment & (alignment - 1U)) != 0U ||
        entrySize != kPackIndexEntrySize || dataOffset < kPackHeaderSize ||
        dataOffset > fabgl_project_payload::kPayloadSize ||
        indexBytes > static_cast<std::uint64_t>(dataOffset - kPackHeaderSize) ||
        calculatePayloadChecksum(kPackHeaderSize, fabgl_project_payload::kPayloadSize -
                                                      kPackHeaderSize) != declaredBuildChecksum ||
        declaredBuildChecksum != fabgl_project_payload::kPackBuildChecksum) {
        return result;
    }

    for (std::uint32_t index = 0; index < entryCount; ++index) {
        const auto entryOffset = kPackHeaderSize + static_cast<std::size_t>(index) * entrySize;
        const auto type = payloadU32(entryOffset + 16U);
        const auto storage = payloadByte(entryOffset + 20U);
        const auto payloadOffset = payloadU32(entryOffset + 24U);
        const auto payloadSize = payloadU32(entryOffset + 28U);
        const auto payloadChecksum = payloadU64(entryOffset + 32U);
        if (!guidAtIndexIsStrictlySorted(index) || storage > 3U || payloadOffset < dataOffset ||
            (payloadOffset & (alignment - 1U)) != 0U ||
            !payloadRangeValid(payloadOffset, payloadSize) ||
            calculatePayloadChecksum(payloadOffset, payloadSize) != payloadChecksum) {
            return PayloadInspection{};
        }
        if (type == kManifestPayloadType) {
            ++result.manifestCount;
            if (!payloadStartsWith(payloadOffset, payloadSize, "{")) {
                return PayloadInspection{};
            }
        } else if (type == kScenePayloadType) {
            ++result.sceneCount;
            result.sceneV2 = payloadStartsWith(payloadOffset, payloadSize, "fglscene 2\n");
            if (!result.sceneV2) {
                return PayloadInspection{};
            }
        } else if (type == kAssetPayloadType) {
            ++result.assetCount;
            if (payloadSize < 8U || !payloadStartsWith(payloadOffset, payloadSize, "FGLA") ||
                payloadU16(payloadOffset + 4U) != 1U) {
                return PayloadInspection{};
            }
            const auto pathLength = payloadU16(payloadOffset + 6U);
            if (pathLength > payloadSize - 8U || !assetPathIsSafe(payloadOffset + 8U, pathLength)) {
                return PayloadInspection{};
            }
        } else {
            return PayloadInspection{};
        }
    }
    result.valid = result.manifestCount == 1U && result.sceneCount == 1U && result.sceneV2 &&
                   result.assetCount == fabgl_project_payload::kAssetCount;
    return result;
}

void reportProjectPayload() {
    char projectName[64]{};
    char previewDemo[40]{};
    sanitizePayloadText(fabgl_project_payload::kProjectName, projectName, sizeof(projectName));
    sanitizePayloadText(fabgl_project_payload::kPreviewDemo, previewDemo, sizeof(previewDemo));
    const auto checksum = calculatePayloadChecksum(0U, fabgl_project_payload::kPayloadSize);
    const auto inspection = inspectProjectPayload();
    const bool valid = inspection.valid && checksum == fabgl_project_payload::kPayloadChecksum;
    char detail[256]{};
    snprintf(detail, sizeof(detail),
             "project=%s;demo=%s;entities=%lu;assets=%lu;bytes=%u;checksum=%016llx;sceneV2=%s",
             projectName, previewDemo,
             static_cast<unsigned long>(fabgl_project_payload::kEntityCount),
             static_cast<unsigned long>(fabgl_project_payload::kAssetCount),
             static_cast<unsigned int>(fabgl_project_payload::kPayloadSize),
             static_cast<unsigned long long>(checksum), inspection.sceneV2 ? "true" : "false");
    emitCheck("project_payload", valid, detail);
}

bool initializeProjectRuntime() {
    const bool loaded = fabgl_project_runtime::loadProject(
        projectReader, fabgl_project_payload::kAssetCount,
        fabgl_project_payload::kPayloadChecksum, fabgl_project_payload::kPackBuildChecksum,
        board_profile::kProfileId, projectRuntime, projectFailure);
    char detail[224]{};
    snprintf(detail, sizeof(detail),
             "loaded=%s;entities=%u;manifestAssets=%u;bindings=%u;error=%u;offset=%u;detail=%s",
             loaded ? "true" : "false",
             static_cast<unsigned int>(projectRuntime.scene.entityCount),
             static_cast<unsigned int>(projectRuntime.manifest.assetCount),
             static_cast<unsigned int>(projectRuntime.manifest.inputBindingCount),
             static_cast<unsigned int>(projectFailure.code),
             static_cast<unsigned int>(projectFailure.offset), projectFailure.detail);
    emitCheck("project_runtime", loaded, detail);
    return loaded;
}
#else
void reportProjectPayload() {
    emitRecord("MANUAL", "project_payload",
               "embedded=false;mode=diagnostic;export-with=fabgl_project_cli-export-esp32");
}

bool initializeProjectRuntime() {
    return false;
}
#endif

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
fabgl::RGB888 projectColor(const fabgl_project_runtime::Color color) {
    return fabgl::RGB888(color.red, color.green, color.blue);
}

const char* keyControlName(const fabgl::VirtualKey key) {
    switch (key) {
    case fabgl::VK_a:
    case fabgl::VK_A: return "Key.A";
    case fabgl::VK_d:
    case fabgl::VK_D: return "Key.D";
    case fabgl::VK_e:
    case fabgl::VK_E: return "Key.E";
    case fabgl::VK_r:
    case fabgl::VK_R: return "Key.R";
    case fabgl::VK_s:
    case fabgl::VK_S: return "Key.S";
    case fabgl::VK_w:
    case fabgl::VK_W: return "Key.W";
    case fabgl::VK_SPACE: return "Key.Space";
    case fabgl::VK_ESCAPE: return "Key.Escape";
    case fabgl::VK_LEFT:
    case fabgl::VK_KP_LEFT: return "Key.Left";
    case fabgl::VK_RIGHT:
    case fabgl::VK_KP_RIGHT: return "Key.Right";
    case fabgl::VK_UP:
    case fabgl::VK_KP_UP: return "Key.Up";
    case fabgl::VK_DOWN:
    case fabgl::VK_KP_DOWN: return "Key.Down";
    default: return nullptr;
    }
}

const fabgl_project_runtime::Entity* firstProjectComponent(const std::uint16_t component) {
    for (std::size_t index = 0U; index < projectRuntime.scene.entityCount; ++index) {
        const auto& entity = projectRuntime.scene.entities[index];
        if (entity.active && (entity.components & component) != 0U)
            return &entity;
    }
    return nullptr;
}

std::size_t countProjectComponents(const std::uint16_t component) {
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < projectRuntime.scene.entityCount; ++index) {
        const auto& entity = projectRuntime.scene.entities[index];
        if (entity.active && (entity.components & component) != 0U)
            ++count;
    }
    return count;
}

const char* projectRenderMode() {
    if (projectRuntime.raycastMap.valid)
        return "raycast";
    if (projectRuntime.racerTrack.valid)
        return "racer";
    return "2d";
}

void drawIndexedSprite(const fabgl_project_runtime::Entity& entity) {
    fabgl_project_runtime::IndexedImageView image;
    fabgl_project_runtime::Failure failure;
    if (!fabgl_project_runtime::inspectIndexedImage(projectReader, projectRuntime, entity.sprite,
                                                    image, failure)) {
        canvas.setBrushColor(projectColor(entity.tint));
        canvas.fillRectangle(static_cast<int>(entity.x), static_cast<int>(entity.y),
                             static_cast<int>(entity.x) + 7, static_cast<int>(entity.y) + 7);
        return;
    }
    const auto scale = std::max(1, std::min(8, static_cast<int>(
        std::lround(std::max(std::fabs(entity.scaleX), std::fabs(entity.scaleY))))));
    auto runOffset = image.runsOffset;
    std::uint8_t runRemaining = 0U;
    std::uint8_t paletteIndex = 0U;
    for (std::uint32_t pixel = 0U; pixel < image.pixelCount; ++pixel) {
        if (runRemaining == 0U) {
            runRemaining = projectReader.byte(runOffset++);
            paletteIndex = projectReader.byte(runOffset++);
        }
        --runRemaining;
        if (paletteIndex == image.transparentIndex)
            continue;
        const auto palette = image.paletteOffset + static_cast<std::uint32_t>(paletteIndex) * 4U;
        const auto red = static_cast<std::uint8_t>(
            static_cast<unsigned>(projectReader.byte(palette)) * entity.tint.red / 255U);
        const auto green = static_cast<std::uint8_t>(
            static_cast<unsigned>(projectReader.byte(palette + 1U)) * entity.tint.green / 255U);
        const auto blue = static_cast<std::uint8_t>(
            static_cast<unsigned>(projectReader.byte(palette + 2U)) * entity.tint.blue / 255U);
        const auto x = static_cast<int>(pixel % image.width);
        const auto y = static_cast<int>(pixel / image.width);
        canvas.setBrushColor(fabgl::RGB888(red, green, blue));
        canvas.fillRectangle(static_cast<int>(entity.x) + x * scale,
                             static_cast<int>(entity.y) + y * scale,
                             static_cast<int>(entity.x) + (x + 1) * scale - 1,
                             static_cast<int>(entity.y) + (y + 1) * scale - 1);
    }
}

void renderProject2d() {
    canvas.setBrushColor(fabgl::RGB888(18, 24, 34));
    canvas.clear();
    for (std::size_t index = 0U; index < projectRuntime.scene.entityCount; ++index) {
        const auto& entity = projectRuntime.scene.entities[index];
        if (!entity.active)
            continue;
        if ((entity.components & fabgl_project_runtime::Component::Sprite) != 0U &&
            !entity.sprite.isNil()) {
            drawIndexedSprite(entity);
        } else if ((entity.components & fabgl_project_runtime::Component::Character) != 0U) {
            canvas.setBrushColor(fabgl::BrightYellow);
            canvas.fillRectangle(static_cast<int>(entity.x), static_cast<int>(entity.y) - 12,
                                 static_cast<int>(entity.x) + 9, static_cast<int>(entity.y));
        }
    }
}

void renderProjectRaycast() {
    const auto& map = projectRuntime.raycastMap;
    const auto* camera = firstProjectComponent(fabgl_project_runtime::Component::FirstPerson);
    if (!map.valid || camera == nullptr) {
        renderProject2d();
        return;
    }
    const auto width = canvas.getWidth();
    const auto height = canvas.getHeight();
    canvas.setBrushColor(fabgl::RGB888(48, 62, 86));
    canvas.fillRectangle(0, 0, width - 1, height / 2);
    canvas.setBrushColor(fabgl::RGB888(36, 31, 29));
    canvas.fillRectangle(0, height / 2, width - 1, height - 1);
    constexpr float fieldOfView = 1.0471975512F;
    for (int column = 0; column < width; column += 2) {
        const auto rayAngle = camera->rotationZ - fieldOfView * 0.5F +
                              fieldOfView * static_cast<float>(column) /
                                  static_cast<float>(std::max(1, width - 1));
        const auto rayX = std::cos(rayAngle);
        const auto rayY = std::sin(rayAngle);
        float distance = 0.04F;
        std::uint8_t cell = 0U;
        for (int step = 0; step < 640 && cell == 0U; ++step) {
            const auto x = static_cast<int>(camera->x + rayX * distance);
            const auto y = static_cast<int>(camera->y + rayY * distance);
            if (x < 0 || y < 0 || x >= map.width || y >= map.height) {
                cell = 1U;
                break;
            }
            cell = map.cells[static_cast<std::size_t>(y * map.width + x)];
            distance += 0.04F;
        }
        distance *= std::max(0.2F, std::cos(rayAngle - camera->rotationZ));
        const auto wallHeight = std::min(height, static_cast<int>(height / std::max(0.12F, distance)));
        const auto top = (height - wallHeight) / 2;
        const auto paletteIndex = std::min<std::size_t>(cell, map.paletteCount - 1U);
        canvas.setBrushColor(projectColor(map.palette[paletteIndex]));
        canvas.fillRectangle(column, top, std::min(width - 1, column + 1), top + wallHeight);
    }
}

void renderProjectRacer() {
    const auto& track = projectRuntime.racerTrack;
    const auto* vehicle = firstProjectComponent(fabgl_project_runtime::Component::Vehicle);
    if (!track.valid || vehicle == nullptr) {
        renderProject2d();
        return;
    }
    const auto width = canvas.getWidth();
    const auto height = canvas.getHeight();
    canvas.setBrushColor(fabgl::RGB888(55, 104, 158));
    canvas.fillRectangle(0, 0, width - 1, height / 2);
    canvas.setBrushColor(projectColor(track.segments[0].grass));
    canvas.fillRectangle(0, height / 2, width - 1, height - 1);
    float curveOffset = -vehicle->x * 34.0F;
    const auto baseSegment = static_cast<std::size_t>(vehicle->z / track.segmentLength) %
                             track.segmentCount;
    constexpr int slices = 34;
    for (int slice = slices - 1; slice >= 0; --slice) {
        const auto& segment = track.segments[(baseSegment + static_cast<std::size_t>(slice)) %
                                             track.segmentCount];
        curveOffset += segment.curve * static_cast<float>(slice + 1) * 0.05F;
        const auto nearY = height - 1 - slice * (height / 2) / slices;
        const auto farY = height - 1 - (slice + 1) * (height / 2) / slices;
        const auto nearHalf = static_cast<int>((width * 0.48F * segment.width) /
                                               (1.0F + slice * 0.12F));
        const auto farHalf = static_cast<int>((width * 0.48F * segment.width) /
                                              (1.0F + (slice + 1) * 0.12F));
        const auto center = width / 2 + static_cast<int>(curveOffset);
        canvas.setBrushColor(projectColor(segment.road));
        canvas.fillRectangle(std::max(0, center - nearHalf), farY,
                             std::min(width - 1, center + nearHalf), nearY);
        canvas.setPenColor(projectColor(segment.rumble));
        canvas.drawLine(center - farHalf, farY, center - nearHalf, nearY);
        canvas.drawLine(center + farHalf, farY, center + nearHalf, nearY);
    }
    canvas.setBrushColor(fabgl::BrightRed);
    canvas.fillRectangle(width / 2 - 8, height - 22, width / 2 + 8, height - 5);
}

void renderProjectFrame() {
    if (projectRuntime.raycastMap.valid)
        renderProjectRaycast();
    else if (projectRuntime.racerTrack.valid)
        renderProjectRacer();
    else
        renderProject2d();
    canvas.selectFont(&fabgl::FONT_8x8);
    canvas.setPenColor(fabgl::BrightWhite);
    canvas.drawTextFmt(3, 3, "%.30s", projectRuntime.manifest.name.value);
    canvas.waitCompletion(false);
    ++projectRenderCount;
}
#endif

void drawStaticDiagnostics() {
    const int width = canvas.getWidth();
    const int height = canvas.getHeight();
    constexpr fabgl::Color colors[] = {
        fabgl::BrightRed,     fabgl::BrightGreen,  fabgl::BrightBlue,  fabgl::BrightCyan,
        fabgl::BrightMagenta, fabgl::BrightYellow, fabgl::BrightWhite, fabgl::BrightBlack,
    };
    constexpr int colorCount = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    const int barWidth = width / colorCount;

    canvas.setBrushColor(fabgl::Black);
    canvas.clear();
    for (int index = 0; index < colorCount; ++index) {
        canvas.setBrushColor(colors[index]);
        const int right = index == colorCount - 1 ? width - 1 : (index + 1) * barWidth - 1;
        canvas.fillRectangle(index * barWidth, 24, right, 63);
    }

    for (int y = 72; y < height - 38; y += 8) {
        for (int x = 0; x < width; x += 8) {
            canvas.setBrushColor(((x + y) / 8) % 2 == 0 ? fabgl::Blue : fabgl::BrightBlue);
            canvas.fillRectangle(x, y, x + 7, y + 7);
        }
    }

    canvas.selectFont(&fabgl::FONT_8x8);
    canvas.setPenColor(fabgl::BrightWhite);
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    char projectName[40]{};
    char previewDemo[24]{};
    sanitizePayloadText(fabgl_project_payload::kProjectName, projectName, sizeof(projectName));
    sanitizePayloadText(fabgl_project_payload::kPreviewDemo, previewDemo, sizeof(previewDemo));
    canvas.drawTextFmt(4, 4, "Project: %.28s", projectName);
    canvas.drawTextFmt(4, 14, "Demo: %.14s  entities: %lu", previewDemo,
                       static_cast<unsigned long>(fabgl_project_payload::kEntityCount));
#else
    canvas.drawText(4, 4, "FabGL Studio - Olimex ESP32-SBC-FabGL");
    canvas.drawText(4, 14, "VGA / 2D / input / audio / SD diagnostic");
#endif
    canvas.drawText(4, height - 26, "Move mouse, press keys, verify colors and 440 Hz tone");
    canvas.waitCompletion(false);
}

void initializeVga() {
    displayController.begin(board_profile::kVgaR1, board_profile::kVgaR0, board_profile::kVgaG1,
                            board_profile::kVgaG0, board_profile::kVgaB1, board_profile::kVgaB0,
                            board_profile::kVgaHSync, board_profile::kVgaVSync);
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    // The bounded project parser intentionally keeps all manifest, scene, input and asset
    // tables in internal RAM. Reserve a deterministic eight scanline margin for FabGL's DMA
    // pools instead of allowing its allocator to silently choose a smaller viewport.
    constexpr int expectedViewportHeight = 192;
    displayController.setResolution(VGA_320x200_75Hz, 320, expectedViewportHeight);
#else
    constexpr int expectedViewportHeight = 200;
    displayController.setResolution(VGA_320x200_75Hz);
#endif
    const bool dimensionsValid =
        canvas.getWidth() == 320 && canvas.getHeight() == expectedViewportHeight;
    char vgaDetail[144]{};
    snprintf(vgaDetail, sizeof(vgaDetail),
             "signal=320x200@75Hz;viewport=%dx%d;pins=22,21,19,18,5,4,23,15;visual=manual",
             canvas.getWidth(), canvas.getHeight());
    emitCheck("vga_init", dimensionsValid, vgaDetail);
    if (dimensionsValid) {
        if (projectMode) {
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
            renderProjectFrame();
            char projectDetail[224]{};
            snprintf(projectDetail, sizeof(projectDetail),
                     "mode=%s;entities=%u;sprites=%u;characters=%u;vehicles=%u;"
                     "raycastMaps=%u;firstPerson=%u;trackSegments=%u;renders=%lu;visual=manual",
                     projectRenderMode(),
                     static_cast<unsigned int>(projectRuntime.scene.entityCount),
                     static_cast<unsigned int>(countProjectComponents(
                         fabgl_project_runtime::Component::Sprite)),
                     static_cast<unsigned int>(countProjectComponents(
                         fabgl_project_runtime::Component::Character)),
                     static_cast<unsigned int>(countProjectComponents(
                         fabgl_project_runtime::Component::Vehicle)),
                     static_cast<unsigned int>(countProjectComponents(
                         fabgl_project_runtime::Component::RaycastMap)),
                     static_cast<unsigned int>(countProjectComponents(
                         fabgl_project_runtime::Component::FirstPerson)),
                     static_cast<unsigned int>(projectRuntime.racerTrack.segmentCount),
                     static_cast<unsigned long>(projectRenderCount));
            emitCheck("project_renderer", projectRenderCount > 0U, projectDetail);
            emitCheck("renderer_2d", true,
                      "source=embedded-scene;fallback=diagnostics;visual=manual");
#endif
        } else {
            drawStaticDiagnostics();
            emitCheck("renderer_2d", true,
                      "color-bars=8;checkerboard=8px;sprite=animated;visual=manual");
            emitRecord("MANUAL", "vga_visual", "confirm-stable-sync;color-bars;text;checkerboard");
        }
    }
}

void initializeInput() {
    // KeyboardPort0/MousePort1 expands to clock/data 33/32 and 26/27. The
    // static_assert in BoardProfile.h makes that board dependency explicit.
    ps2Controller.begin(fabgl::PS2Preset::KeyboardPort0_MousePort1,
                        fabgl::KbdMode::CreateVirtualKeysQueue);
    delay(250);
    const bool keyboardAvailable =
        ps2Controller.keyboard() != nullptr && ps2Controller.keyboard()->isKeyboardAvailable();
    const bool mouseAvailable =
        ps2Controller.mouse() != nullptr && ps2Controller.mouse()->isMouseAvailable();
    emitCheck("keyboard_detect", keyboardAvailable, "data=32;clock=33;press-key-for-event");
    emitCheck("mouse_detect", mouseAvailable, "data=27;clock=26;move-mouse-for-event");
}

void initializeAudio() {
    soundGenerator.playSound(fabgl::SquareWaveformGenerator(), 440, 300, 64);
    emitCheck("audio_pipeline", soundGenerator.playing(),
              "gpio=25;method=dac;tone=440Hz;duration=300ms;audible=manual");
    emitRecord("MANUAL", "audio_output", "confirm-tone-on-3.5mm-output-or-board-audio-path");
}

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
bool initializeProjectScripts() {
    fabgl_project_scripts::GetModuleFunction getModule = nullptr;
    if (fabglProjectGetEsp32ScriptsV1 != nullptr)
        getModule = &fabglProjectGetEsp32ScriptsV1;
    const bool initialized = projectScriptRuntime.initialize(
        getModule, projectRuntime, FABGL_STUDIO_HAS_PROJECT_SCRIPTS != 0);
    char detail[160]{};
    snprintf(detail, sizeof(detail),
             "required=%s;files=%u;scripts=%u;error=%u;abi=%u",
             FABGL_STUDIO_HAS_PROJECT_SCRIPTS != 0 ? "true" : "false",
             static_cast<unsigned int>(FABGL_STUDIO_PROJECT_SCRIPT_FILE_COUNT),
             static_cast<unsigned int>(projectScriptRuntime.count()),
             static_cast<unsigned int>(projectScriptRuntime.error()),
             static_cast<unsigned int>(fabgl_project_scripts::kAbiVersion));
    emitCheck("project_scripts", initialized, detail);
    return initialized;
}

bool saveProjectSlot(void*, fabgl_project_runtime::RuntimeProject& runtime, const char* slot,
                     const fabgl_project_runtime::Guid* player,
                     fabgl_project_save::Document* gameplayState) noexcept {
    if (!sdMounted) {
        projectSaveError = fabgl_project_save::Error::StorageUnavailable;
        emitRecord("FAIL", "project_save", "reason=sd-not-mounted;write=false");
        return false;
    }
    auto& document = gameplayState == nullptr ? projectSaveDocument : *gameplayState;
    // The no-document convenience call captures runtime state only. An explicit caller-owned
    // document retains its bounded primitive/scene/player fields across runtime capture.
    if (gameplayState == nullptr) {
        document.primitiveCount = 0U;
        document.sceneFieldCount = 0U;
        document.playerFieldCount = 0U;
    }
    const auto captured = fabgl_project_save::captureRuntime(
        runtime, player, document, fabgl_project_save::kDefaultSchemaVersion,
        projectSaveSequence);
    if (!captured) {
        projectSaveError = captured.error;
        emitRecord("FAIL", "project_save", "reason=capture-failed;write=false");
        return false;
    }
    const auto saved = projectSaveService.save(slot, document, projectSaveBuffer,
                                               sizeof(projectSaveBuffer));
    projectSaveError = saved.error;
    char detail[144]{};
    snprintf(detail, sizeof(detail), "slot=%.24s;bytes=%u;sequence=%lu;error=%u;explicit=true",
             slot == nullptr ? "(null)" : slot, static_cast<unsigned int>(saved.bytes),
             static_cast<unsigned long>(projectSaveSequence),
             static_cast<unsigned int>(saved.error));
    emitRecord(saved ? "PASS" : "FAIL", "project_save", detail);
    if (saved)
        ++projectSaveSequence;
    return saved.ok();
}

bool loadProjectSlot(void*, fabgl_project_runtime::RuntimeProject& runtime, const char* slot,
                     const fabgl_project_runtime::Guid* player,
                     fabgl_project_save::Document* gameplayState) noexcept {
    if (!sdMounted) {
        projectSaveError = fabgl_project_save::Error::StorageUnavailable;
        emitRecord("FAIL", "project_load", "reason=sd-not-mounted;read=false");
        return false;
    }
    auto& document = gameplayState == nullptr ? projectSaveDocument : *gameplayState;
    fabgl_project_save::LoadInfo info;
    const auto loaded = projectSaveService.load(slot, document, projectSaveBuffer,
                                                sizeof(projectSaveBuffer), &info);
    if (!loaded) {
        projectSaveError = loaded.error;
        char detail[112]{};
        snprintf(detail, sizeof(detail), "slot=%.24s;error=%u",
                 slot == nullptr ? "(null)" : slot,
                 static_cast<unsigned int>(loaded.error));
        emitRecord("FAIL", "project_load", detail);
        return false;
    }
    const auto restored = fabgl_project_save::restoreRuntime(document, runtime, player);
    projectSaveError = restored.error;
    char detail[160]{};
    snprintf(detail, sizeof(detail),
             "slot=%.24s;bytes=%u;sequence=%lu;backup=%s;error=%u;explicit=true",
             slot == nullptr ? "(null)" : slot, static_cast<unsigned int>(loaded.bytes),
             static_cast<unsigned long>(document.sequence),
             info.recoveredFromBackup ? "true" : "false",
             static_cast<unsigned int>(restored.error));
    emitRecord(restored ? "PASS" : "FAIL", "project_load", detail);
    if (restored)
        projectSaveSequence = std::max(projectSaveSequence, document.sequence + 1U);
    return restored.ok();
}

std::uint8_t projectPersistenceError(const void*) noexcept {
    return static_cast<std::uint8_t>(projectSaveError);
}

void bindProjectPersistence() noexcept {
    // A non-null stable context is used even when the card is absent. This exposes a precise
    // StorageUnavailable error to gameplay without performing any boot-time read or write.
    projectRuntime.bindPersistence(&projectSaveService, &saveProjectSlot, &loadProjectSlot,
                                   &projectPersistenceError);
}
#endif

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD && FABGL_STUDIO_SOAK_DIAGNOSTICS
bool reloadProjectForSoak() {
    projectRuntime.~RuntimeProject();
    new (&projectRuntime) fabgl_project_runtime::RuntimeProject();
    projectFailure = {};
    const bool loaded = fabgl_project_runtime::loadProject(
        projectReader, fabgl_project_payload::kAssetCount,
        fabgl_project_payload::kPayloadChecksum, fabgl_project_payload::kBuildChecksum,
        board_profile::kProfileId, projectRuntime, projectFailure);
    if (loaded)
        bindProjectPersistence();
    if (!loaded)
        ++soakReloadFailures;
    return loaded;
}

bool updateSoakAssetCache(const bool makeResident) {
    if (!makeResident) {
        std::memset(soakAssetCache, 0, sizeof(soakAssetCache));
        soakAssetChecksum = 0U;
        return true;
    }
    if (projectRuntime.payload.assetCount == 0U)
        return false;
    const auto& asset = projectRuntime.payload.assets[0];
    const auto count = std::min<std::size_t>(asset.contentSize, sizeof(soakAssetCache));
    if (count == 0U || asset.contentOffset > projectReader.size() ||
        count > projectReader.size() - asset.contentOffset)
        return false;
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    soakAssetChecksum = offset;
    for (std::size_t index = 0U; index < count; ++index) {
        soakAssetCache[index] = projectReader.byte(asset.contentOffset + index);
        soakAssetChecksum ^= soakAssetCache[index];
        soakAssetChecksum *= prime;
    }
    if (count < sizeof(soakAssetCache))
        std::memset(soakAssetCache + count, 0, sizeof(soakAssetCache) - count);
    return true;
}

void advanceSoakWorkload() {
    const auto now = millis();
    if (now - soakLastStep < kSoakStepPeriodMs)
        return;
    soakLastStep = now;

    const bool assetAvailable = projectRuntime.payload.assetCount != 0U;
    const auto step = soakWorkload.advance(assetAvailable);
    bool sceneReady = true;
    if (step.projectSceneActive) {
        sceneReady = reloadProjectForSoak();
        projectMode = sceneReady;
        if (sceneReady)
            renderProjectFrame();
    } else {
        projectMode = false;
        drawStaticDiagnostics();
    }
    const bool assetReady = updateSoakAssetCache(step.assetResident);
    soundGenerator.playSound(fabgl::SquareWaveformGenerator(), 440, 120, 48);

    if (!sceneReady || !assetReady || !soakWorkload.invariantHolds()) {
        char failure[192]{};
        snprintf(failure, sizeof(failure),
                 "iteration=%lu;scene=%s;asset=%s;invariant=%s;runtimeCode=%u",
                 static_cast<unsigned long>(soakWorkload.iterations),
                 sceneReady ? "ok" : "failed", assetReady ? "ok" : "failed",
                 soakWorkload.invariantHolds() ? "ok" : "failed",
                 static_cast<unsigned int>(projectFailure.code));
        emitRecord("FAIL", "soak_workload", failure);
    }
}

void emitSoakMetric() {
    char detail[320]{};
    snprintf(detail, sizeof(detail),
             "iterations=%lu;sceneTransitions=%lu;assetLoads=%lu;assetUnloads=%lu;"
             "audioPlays=%lu;entityCreates=%lu;entityDestroys=%lu;liveEntities=%u;"
             "reloadFailures=%lu;assetChecksum=%016llx",
             static_cast<unsigned long>(soakWorkload.iterations),
             static_cast<unsigned long>(soakWorkload.sceneTransitions),
             static_cast<unsigned long>(soakWorkload.assetLoads),
             static_cast<unsigned long>(soakWorkload.assetUnloads),
             static_cast<unsigned long>(soakWorkload.audioPlays),
             static_cast<unsigned long>(soakWorkload.entityCreates),
             static_cast<unsigned long>(soakWorkload.entityDestroys),
             static_cast<unsigned int>(soakWorkload.liveEntities),
             static_cast<unsigned long>(soakReloadFailures),
             static_cast<unsigned long long>(soakAssetChecksum));
    emitRecord("METRIC", "soak", detail);
}
#endif

void initializeSd() {
    // Do not use FabGL FileBrowser defaults (16/17/14/13). This board is wired
    // to HSPI MISO/MOSI/CLK/CS = 35/12/14/13.
    sdSpi.begin(board_profile::kSdClock, board_profile::kSdMiso, board_profile::kSdMosi,
                board_profile::kSdChipSelect);
    sdMounted = SD.begin(board_profile::kSdChipSelect, sdSpi, board_profile::kSdFrequencyHz);
    const bool cardPresent = sdMounted && SD.cardType() != CARD_NONE;
    if (cardPresent) {
        char detail[128]{};
        const std::uint64_t capacityMiB = SD.cardSize() / (1024ULL * 1024ULL);
        snprintf(detail, sizeof(detail),
                 "bus=HSPI;miso=35;mosi=12;clock=14;cs=13;capacityMiB=%llu;write=not-tested",
                 static_cast<unsigned long long>(capacityMiB));
        emitCheck("sd_mount", true, detail);
    } else {
        emitCheck("sd_mount", false,
                  "bus=HSPI;miso=35;mosi=12;clock=14;cs=13;reason=no-card-or-mount-failed");
    }
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    bindProjectPersistence();
#endif
}

void diagnoseMemoryProfile() {
#if defined(BOARD_HAS_PSRAM)
    const bool psramReady = psramFound() && ESP.getPsramSize() > 0U;
    char detail[128]{};
    snprintf(detail, sizeof(detail), "profile=enabled;size=%lu;free=%lu",
             static_cast<unsigned long>(ESP.getPsramSize()),
             static_cast<unsigned long>(ESP.getFreePsram()));
    emitCheck("psram", psramReady, detail);
#else
    emitCheck("psram_profile", true, "profile=disabled;official-reference-setting=true");
    emitRecord("MANUAL", "psram_hardware", "rebuild-with-EnablePsram-for-physical-8MiB-test");
#endif

    char heapDetail[160]{};
    snprintf(heapDetail, sizeof(heapDetail),
             "heapSize=%lu;free=%lu;minimumFree=%lu;largestBlock=%lu;dmaFree=%u",
             static_cast<unsigned long>(ESP.getHeapSize()),
             static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()),
             static_cast<unsigned long>(ESP.getMaxAllocHeap()),
             static_cast<unsigned int>(heap_caps_get_free_size(MALLOC_CAP_DMA)));
    emitCheck("memory", ESP.getFreeHeap() > 32U * 1024U, heapDetail);
}

void pollInput() {
    auto* keyboard = ps2Controller.keyboard();
    if (keyboard != nullptr && keyboard->virtualKeyAvailable() > 0) {
        bool keyDown = false;
        const fabgl::VirtualKey key = keyboard->getNextVirtualKey(&keyDown, 0);
        char detail[80]{};
        snprintf(detail, sizeof(detail), "key=%d;state=%s", static_cast<int>(key),
                 keyDown ? "down" : "up");
        emitCheck("keyboard_event", key != fabgl::VK_NONE, detail);
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
        if (projectMode) {
            const auto* control = keyControlName(key);
            if (control != nullptr)
                projectRuntime.setControl(control, keyDown ? 1.0F : 0.0F);
        }
#endif
    }

    auto* mouse = ps2Controller.mouse();
    if (mouse != nullptr && mouse->deltaAvailable()) {
        fabgl::MouseDelta delta{};
        const bool received = mouse->getNextDelta(&delta, 0);
        char detail[96]{};
        snprintf(detail, sizeof(detail), "dx=%d;dy=%d;wheel=%d", static_cast<int>(delta.deltaX),
                 static_cast<int>(delta.deltaY), static_cast<int>(delta.deltaZ));
        emitCheck("mouse_event", received, detail);
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
        if (projectMode && received) {
            projectRuntime.setControl("Mouse.X", std::max(-1.0F, std::min(1.0F,
                static_cast<float>(delta.deltaX) / 32.0F)), true);
            projectRuntime.setControl("Mouse.Y", std::max(-1.0F, std::min(1.0F,
                static_cast<float>(delta.deltaY) / 32.0F)), true);
            projectRuntime.setControl("Mouse.Left", delta.buttons.left ? 1.0F : 0.0F);
            projectRuntime.setControl("Mouse.Middle", delta.buttons.middle ? 1.0F : 0.0F);
            projectRuntime.setControl("Mouse.Right", delta.buttons.right ? 1.0F : 0.0F);
        }
#endif
    }
}

void animate2dAndReportMetrics() {
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD && FABGL_STUDIO_SOAK_DIAGNOSTICS
    advanceSoakWorkload();
#endif
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    if (projectMode) {
        const auto now = millis();
        const auto elapsed = previousProjectFrame == 0U ? 16U : now - previousProjectFrame;
        previousProjectFrame = now;
        const auto deltaSeconds =
            static_cast<float>(std::min<std::uint32_t>(elapsed, 100U)) / 1000.0F;
        projectScriptRuntime.update(projectRuntime, deltaSeconds);
        projectRuntime.update(deltaSeconds);
        ++projectUpdateCount;
        renderProjectFrame();
        ++frameCount;
    } else
#endif
    {
    constexpr int spriteY = 148;
    constexpr int spriteSize = 12;
    canvas.setBrushColor(fabgl::Black);
    canvas.fillRectangle(spriteX, spriteY, spriteX + spriteSize, spriteY + spriteSize);
    spriteX += spriteDirection * 3;
    if (spriteX <= 2 || spriteX + spriteSize >= canvas.getWidth() - 2) {
        spriteDirection = -spriteDirection;
    }
    canvas.setBrushColor(fabgl::BrightYellow);
    canvas.fillRectangle(spriteX, spriteY, spriteX + spriteSize, spriteY + spriteSize);
    canvas.waitCompletion(false);
    ++frameCount;
    }

    const std::uint32_t now = millis();
    const std::uint32_t elapsed = now - metricWindowStart;
    if (elapsed < board_profile::kMetricPeriodMs) {
        return;
    }
    const float fps = static_cast<float>(frameCount) * 1000.0F / static_cast<float>(elapsed);
    char detail[224]{};
    snprintf(detail, sizeof(detail),
             "fps=%.2f;heapFree=%lu;heapMinimum=%lu;largestBlock=%lu;dmaFree=%u;psramFree=%lu;"
             "sdReadBytes=%llu",
             static_cast<double>(fps), static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()),
             static_cast<unsigned long>(ESP.getMaxAllocHeap()),
             static_cast<unsigned int>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned long>(ESP.getFreePsram()),
             static_cast<unsigned long long>(projectSaveStorage.bytesRead()));
    emitRecord("METRIC", "runtime", detail);

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD && FABGL_STUDIO_SOAK_DIAGNOSTICS
    emitSoakMetric();
#endif

#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    if (projectMode || kReportInactiveProjectMetrics) {
        const auto* vehicle = firstProjectComponent(fabgl_project_runtime::Component::Vehicle);
        char projectDetail[224]{};
        snprintf(projectDetail, sizeof(projectDetail),
                 "mode=%s;updates=%lu;renders=%lu;entities=%u;assets=%u;trackSegments=%u;"
                 "vehicleSpeed=%.2f;vehicleDistance=%.2f;sceneActive=%s;scripts=%u;scriptUpdates=%lu",
                 projectRenderMode(), static_cast<unsigned long>(projectUpdateCount),
                 static_cast<unsigned long>(projectRenderCount),
                 static_cast<unsigned int>(projectRuntime.scene.entityCount),
                 static_cast<unsigned int>(projectRuntime.manifest.assetCount),
                 static_cast<unsigned int>(projectRuntime.racerTrack.segmentCount),
                 static_cast<double>(vehicle == nullptr ? 0.0F : vehicle->vehicleSpeed),
                 static_cast<double>(vehicle == nullptr ? 0.0F : vehicle->z),
                 projectMode ? "true" : "false",
                 static_cast<unsigned int>(projectScriptRuntime.count()),
                 static_cast<unsigned long>(projectScriptRuntime.updateCount()));
        emitRecord("METRIC", "project_runtime", projectDetail);
    }
#endif

    canvas.setBrushColor(fabgl::Black);
    canvas.fillRectangle(0, canvas.getHeight() - 12, canvas.getWidth() - 1, canvas.getHeight() - 1);
    canvas.setPenColor(fabgl::BrightGreen);
    canvas.drawTextFmt(4, canvas.getHeight() - 10, "FPS %.1f  heap %lu  min %lu", fps,
                       static_cast<unsigned long>(ESP.getFreeHeap()),
                       static_cast<unsigned long>(ESP.getMinFreeHeap()));
    canvas.waitCompletion(false);
    frameCount = 0;
    metricWindowStart = now;
}

} // namespace

void setup() {
    Serial.begin(board_profile::kSerialBaud);
    delay(400);
    Serial.printf("%s|%d|BOOT|firmware|profile=%s;version=%s;core=%d.%d.%d\n", kProtocol,
                  kProtocolVersion, board_profile::kProfileId, board_profile::kFirmwareVersion,
                  ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
    emitCheck("serial", true, "baud=115200;format=pipe-v1");
    reportProjectPayload();
    projectMode = initializeProjectRuntime();
    // Mount and bind persistence before script Start callbacks. Mounting is read-only from the
    // save system's perspective; only an explicit gameplay saveSlot() call stages a write.
    initializeSd();
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    if (projectMode && !initializeProjectScripts())
        projectMode = false;
#endif

    initializeVga();
    initializeInput();
    initializeAudio();
    diagnoseMemoryProfile();

    metricWindowStart = millis();
    emitRecord("READY", "diagnostics",
               "interactive=true;upload-command=absent;sd-write=false");
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD
    if (projectMode) {
        char detail[144]{};
        snprintf(detail, sizeof(detail),
                 "interactive=true;source=embedded;scriptRuntime=%s;scripts=%u",
                 projectScriptRuntime.count() == 0U ? "none" : "portable-v1",
                 static_cast<unsigned int>(projectScriptRuntime.count()));
        emitRecord("READY", "project_runtime", detail);
    }
#endif
#if FABGL_STUDIO_HAS_PROJECT_PAYLOAD && FABGL_STUDIO_SOAK_DIAGNOSTICS
    soakLastStep = millis();
    emitCheck("soak_workload", soakWorkload.invariantHolds(),
              "enabled=true;periodMs=2000;scene=true;asset=true;audio=true;entityPool=16");
#endif
}

void loop() {
    pollInput();
    animate2dAndReportMetrics();
    delay(1);
}
