#include "win32_window.h"

#include <algorithm>

namespace fabgl::player {

namespace {

constexpr wchar_t WindowClassName[] = L"FabGLStudioPlayerWindow";

[[nodiscard]] bool keyDown(int key) noexcept {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

} // namespace

Win32Window::~Win32Window() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
}

bool Win32Window::open(int logicalWidth, int logicalHeight, int scale, const std::string& title,
                       std::string& error) {
    logicalWidth_ = logicalWidth;
    logicalHeight_ = logicalHeight;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Win32Window::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "RegisterClassExW failed";
        return false;
    }

    RECT rectangle{0, 0, logicalWidth * scale, logicalHeight * scale};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    const auto wideTitleLength = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::wstring wideTitle(static_cast<std::size_t>(std::max(1, wideTitleLength)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle.data(), wideTitleLength);
    window_ = CreateWindowExW(0, WindowClassName, wideTitle.c_str(), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
                              rectangle.bottom - rectangle.top, nullptr, nullptr, instance, this);
    if (window_ == nullptr) {
        error = "CreateWindowExW failed";
        return false;
    }
    pixels_.resize(static_cast<std::size_t>(logicalWidth) *
                   static_cast<std::size_t>(logicalHeight));
    bitmapInfo_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo_.bmiHeader.biWidth = logicalWidth;
    bitmapInfo_.bmiHeader.biHeight = -logicalHeight;
    bitmapInfo_.bmiHeader.biPlanes = 1;
    bitmapInfo_.bmiHeader.biBitCount = 32;
    bitmapInfo_.bmiHeader.biCompression = BI_RGB;
    ShowWindow(window_, SW_SHOW);
    running_ = true;
    return true;
}

bool Win32Window::poll(InputState& input) noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            running_ = false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    input.left = keyDown(VK_LEFT) || keyDown('A');
    input.right = keyDown(VK_RIGHT) || keyDown('D');
    input.forward = keyDown(VK_UP) || keyDown('W');
    input.backward = keyDown(VK_DOWN) || keyDown('S');
    input.action = keyDown(VK_SPACE);
    input.quit = keyDown(VK_ESCAPE) || !running_;
    return running_ && !input.quit;
}

void Win32Window::present(const rendering::Framebuffer& framebuffer) noexcept {
    if (window_ == nullptr || framebuffer.width() != logicalWidth_ ||
        framebuffer.height() != logicalHeight_) {
        return;
    }
    const auto& source = framebuffer.pixels();
    for (std::size_t index = 0; index < source.size(); ++index) {
        pixels_[index] = static_cast<std::uint32_t>(source[index].b) |
                         (static_cast<std::uint32_t>(source[index].g) << 8U) |
                         (static_cast<std::uint32_t>(source[index].r) << 16U);
    }
    RECT client{};
    GetClientRect(window_, &client);
    const auto device = GetDC(window_);
    if (device != nullptr) {
        StretchDIBits(device, 0, 0, client.right, client.bottom, 0, 0, logicalWidth_,
                      logicalHeight_, pixels_.data(), &bitmapInfo_, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(window_, device);
    }
}

LRESULT CALLBACK Win32Window::windowProcedure(HWND window, UINT message, WPARAM wparam,
                                              LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CLOSE) {
        if (self != nullptr) {
            self->running_ = false;
        }
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (self != nullptr) {
            self->window_ = nullptr;
            self->running_ = false;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace fabgl::player
