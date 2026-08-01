#pragma once

#include <Arduino.h>

namespace board_profile {

constexpr char kProfileId[] = "olimex-esp32-sbc-fabgl-revb";
constexpr char kFirmwareVersion[] = "0.1.0-smoke";

constexpr gpio_num_t kVgaR1 = GPIO_NUM_22;
constexpr gpio_num_t kVgaR0 = GPIO_NUM_21;
constexpr gpio_num_t kVgaG1 = GPIO_NUM_19;
constexpr gpio_num_t kVgaG0 = GPIO_NUM_18;
constexpr gpio_num_t kVgaB1 = GPIO_NUM_5;
constexpr gpio_num_t kVgaB0 = GPIO_NUM_4;
constexpr gpio_num_t kVgaHSync = GPIO_NUM_23;
constexpr gpio_num_t kVgaVSync = GPIO_NUM_15;

constexpr gpio_num_t kKeyboardData = GPIO_NUM_32;
constexpr gpio_num_t kKeyboardClock = GPIO_NUM_33;
constexpr gpio_num_t kMouseData = GPIO_NUM_27;
constexpr gpio_num_t kMouseClock = GPIO_NUM_26;

constexpr gpio_num_t kAudioDac = GPIO_NUM_25;

constexpr int kSdMiso = 35;
constexpr int kSdMosi = 12;
constexpr int kSdClock = 14;
constexpr int kSdChipSelect = 13;
constexpr std::uint32_t kSdFrequencyHz = 20000000U;

constexpr unsigned long kSerialBaud = 115200UL;
constexpr std::uint32_t kMetricPeriodMs = 1000U;

// The FabGL preset used by the diagnostic is valid only while these remain
// the library's documented KeyboardPort0/MousePort1 defaults.
static_assert(kKeyboardClock == GPIO_NUM_33 && kKeyboardData == GPIO_NUM_32 &&
                  kMouseClock == GPIO_NUM_26 && kMouseData == GPIO_NUM_27,
              "PS/2 preset no longer matches the board profile");

} // namespace board_profile
