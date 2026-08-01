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

#include "BoardProfile.h"

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
int spriteX = 16;
int spriteDirection = 1;
bool sdMounted = false;

void emitRecord(const char* status, const char* check, const char* detail) {
    Serial.printf("%s|%d|%s|%s|%s\n", kProtocol, kProtocolVersion, status, check,
                  detail == nullptr ? "" : detail);
}

void emitCheck(const char* check, const bool passed, const char* detail) {
    emitRecord(passed ? "PASS" : "FAIL", check, detail);
}

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
    canvas.drawText(4, 4, "FabGL Studio - Olimex ESP32-SBC-FabGL");
    canvas.drawText(4, 14, "VGA / 2D / input / audio / SD diagnostic");
    canvas.drawText(4, height - 26, "Move mouse, press keys, verify colors and 440 Hz tone");
    canvas.waitCompletion(false);
}

void initializeVga() {
    displayController.begin(board_profile::kVgaR1, board_profile::kVgaR0, board_profile::kVgaG1,
                            board_profile::kVgaG0, board_profile::kVgaB1, board_profile::kVgaB0,
                            board_profile::kVgaHSync, board_profile::kVgaVSync);
    displayController.setResolution(VGA_320x200_75Hz);
    const bool dimensionsValid = canvas.getWidth() == 320 && canvas.getHeight() == 200;
    emitCheck("vga_init", dimensionsValid,
              "mode=320x200@75Hz;pins=22,21,19,18,5,4,23,15;visual=manual");
    if (dimensionsValid) {
        drawStaticDiagnostics();
        emitCheck("renderer_2d", true,
                  "color-bars=8;checkerboard=8px;sprite=animated;visual=manual");
        emitRecord("MANUAL", "vga_visual", "confirm-stable-sync;color-bars;text;checkerboard");
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
    }

    auto* mouse = ps2Controller.mouse();
    if (mouse != nullptr && mouse->deltaAvailable()) {
        fabgl::MouseDelta delta{};
        const bool received = mouse->getNextDelta(&delta, 0);
        char detail[96]{};
        snprintf(detail, sizeof(detail), "dx=%d;dy=%d;wheel=%d", static_cast<int>(delta.deltaX),
                 static_cast<int>(delta.deltaY), static_cast<int>(delta.deltaZ));
        emitCheck("mouse_event", received, detail);
    }
}

void animate2dAndReportMetrics() {
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

    const std::uint32_t now = millis();
    const std::uint32_t elapsed = now - metricWindowStart;
    if (elapsed < board_profile::kMetricPeriodMs) {
        return;
    }
    const float fps = static_cast<float>(frameCount) * 1000.0F / static_cast<float>(elapsed);
    char detail[192]{};
    snprintf(detail, sizeof(detail),
             "fps=%.2f;heapFree=%lu;heapMinimum=%lu;largestBlock=%lu;dmaFree=%u;psramFree=%lu",
             static_cast<double>(fps), static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()),
             static_cast<unsigned long>(ESP.getMaxAllocHeap()),
             static_cast<unsigned int>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned long>(ESP.getFreePsram()));
    emitRecord("METRIC", "runtime", detail);

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

    initializeVga();
    initializeInput();
    initializeAudio();
    initializeSd();
    diagnoseMemoryProfile();

    metricWindowStart = millis();
    emitRecord("READY", "diagnostics", "interactive=true;upload-command=absent;sd-write=false");
}

void loop() {
    pollInput();
    animate2dAndReportMetrics();
    delay(1);
}
