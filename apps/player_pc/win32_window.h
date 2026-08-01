#pragma once

#include "demo.h"

#include <fabgl/rendering/framebuffer.h>

#include <string>
#include <vector>

#include <windows.h>

namespace fabgl::player {

class Win32Window final {
  public:
    Win32Window() = default;
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    [[nodiscard]] bool open(int logicalWidth, int logicalHeight, int scale,
                            const std::string& title, std::string& error);
    [[nodiscard]] bool poll(InputState& input) noexcept;
    void present(const rendering::Framebuffer& framebuffer) noexcept;

  private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam);

    HWND window_ = nullptr;
    int logicalWidth_ = 0;
    int logicalHeight_ = 0;
    std::vector<std::uint32_t> pixels_;
    BITMAPINFO bitmapInfo_{};
    bool running_ = false;
};

} // namespace fabgl::player
