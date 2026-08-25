#include "win32_window.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

#include <xinput.h>

namespace fabgl::player {

namespace {

constexpr wchar_t WindowClassName[] = L"FabGLStudioPlayerWindow";

[[nodiscard]] bool keyDown(int key) noexcept {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

void setButton(InputState& input, const std::string& name, const bool down) {
    input.controls[name] = down ? 1.0F : 0.0F;
}

[[nodiscard]] float normalizedThumb(const SHORT value, const SHORT deadZone) noexcept {
    const auto magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= static_cast<int>(deadZone))
        return 0.0F;
    const auto range = 32767 - static_cast<int>(deadZone);
    const auto normalized =
        static_cast<float>(magnitude - static_cast<int>(deadZone)) / static_cast<float>(range);
    return std::copysign(std::clamp(normalized, 0.0F, 1.0F), static_cast<float>(value));
}

[[nodiscard]] float normalizedTrigger(const BYTE value) noexcept {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        return 0.0F;
    return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
           static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

void clearGamepadControls(InputState& input) {
    for (const auto* name :
         {"Gamepad.A", "Gamepad.B", "Gamepad.X", "Gamepad.Y", "Gamepad.LeftShoulder",
          "Gamepad.RightShoulder", "Gamepad.Back", "Gamepad.Start", "Gamepad.LeftStick",
          "Gamepad.RightStick", "Gamepad.DPadUp", "Gamepad.DPadDown", "Gamepad.DPadLeft",
          "Gamepad.DPadRight"}) {
        input.controls[name] = 0.0F;
    }
    for (const auto* name : {"Gamepad.LeftX", "Gamepad.LeftY", "Gamepad.RightX", "Gamepad.RightY",
                             "Gamepad.LeftTrigger", "Gamepad.RightTrigger"}) {
        input.controls[name] = 0.0F;
    }
}

} // namespace

Win32Window::~Win32Window() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
    if (xinputModule_ != nullptr) {
        FreeLibrary(xinputModule_);
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
    for (const auto* library : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        xinputModule_ = LoadLibraryW(library);
        if (xinputModule_ == nullptr)
            continue;
        xinputGetState_ = GetProcAddress(xinputModule_, "XInputGetState");
        if (xinputGetState_ != nullptr)
            break;
        FreeLibrary(xinputModule_);
        xinputModule_ = nullptr;
    }
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
    input.controls.clear();
    for (int key = 'A'; key <= 'Z'; ++key) {
        setButton(input, "Key." + std::string(1U, static_cast<char>(key)), keyDown(key));
    }
    for (int key = '0'; key <= '9'; ++key) {
        setButton(input, "Key." + std::string(1U, static_cast<char>(key)), keyDown(key));
    }
    const std::pair<const char*, int> specialKeys[]{{"Key.Left", VK_LEFT},
                                                    {"Key.Right", VK_RIGHT},
                                                    {"Key.Up", VK_UP},
                                                    {"Key.Down", VK_DOWN},
                                                    {"Key.Space", VK_SPACE},
                                                    {"Key.Enter", VK_RETURN},
                                                    {"Key.Escape", VK_ESCAPE},
                                                    {"Key.Tab", VK_TAB},
                                                    {"Key.Backspace", VK_BACK},
                                                    {"Key.LeftShift", VK_LSHIFT},
                                                    {"Key.RightShift", VK_RSHIFT},
                                                    {"Key.LeftControl", VK_LCONTROL},
                                                    {"Key.RightControl", VK_RCONTROL},
                                                    {"Key.LeftAlt", VK_LMENU},
                                                    {"Key.RightAlt", VK_RMENU}};
    for (const auto& [name, key] : specialKeys)
        setButton(input, name, keyDown(key));

    setButton(input, "Mouse.Left", keyDown(VK_LBUTTON));
    setButton(input, "Mouse.Right", keyDown(VK_RBUTTON));
    setButton(input, "Mouse.Middle", keyDown(VK_MBUTTON));
    POINT cursor{};
    if (GetCursorPos(&cursor) != FALSE && ScreenToClient(window_, &cursor) != FALSE) {
        RECT client{};
        GetClientRect(window_, &client);
        const auto width = std::max(1L, client.right - client.left);
        const auto height = std::max(1L, client.bottom - client.top);
        input.controls["Mouse.X"] = std::clamp(
            2.0F * static_cast<float>(cursor.x) / static_cast<float>(width) - 1.0F, -1.0F, 1.0F);
        input.controls["Mouse.Y"] = std::clamp(
            2.0F * static_cast<float>(cursor.y) / static_cast<float>(height) - 1.0F, -1.0F, 1.0F);
        input.controls["Mouse.DeltaX"] =
            hasPreviousCursor_
                ? std::clamp(2.0F * static_cast<float>(cursor.x - previousCursor_.x) /
                                 static_cast<float>(width),
                             -1.0F, 1.0F)
                : 0.0F;
        input.controls["Mouse.DeltaY"] =
            hasPreviousCursor_
                ? std::clamp(2.0F * static_cast<float>(cursor.y - previousCursor_.y) /
                                 static_cast<float>(height),
                             -1.0F, 1.0F)
                : 0.0F;
        previousCursor_ = cursor;
        hasPreviousCursor_ = true;
    }
    input.controls["Mouse.Wheel"] =
        std::clamp(static_cast<float>(wheelDelta_) / static_cast<float>(WHEEL_DELTA), -1.0F, 1.0F);
    wheelDelta_ = 0;

    clearGamepadControls(input);
    if (xinputGetState_ != nullptr) {
        using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        const auto getState = std::bit_cast<GetState>(xinputGetState_);
        XINPUT_STATE state{};
        if (getState(0U, &state) == ERROR_SUCCESS) {
            const auto buttons = state.Gamepad.wButtons;
            const std::pair<const char*, WORD> gamepadButtons[]{
                {"Gamepad.A", XINPUT_GAMEPAD_A},
                {"Gamepad.B", XINPUT_GAMEPAD_B},
                {"Gamepad.X", XINPUT_GAMEPAD_X},
                {"Gamepad.Y", XINPUT_GAMEPAD_Y},
                {"Gamepad.LeftShoulder", XINPUT_GAMEPAD_LEFT_SHOULDER},
                {"Gamepad.RightShoulder", XINPUT_GAMEPAD_RIGHT_SHOULDER},
                {"Gamepad.Back", XINPUT_GAMEPAD_BACK},
                {"Gamepad.Start", XINPUT_GAMEPAD_START},
                {"Gamepad.LeftStick", XINPUT_GAMEPAD_LEFT_THUMB},
                {"Gamepad.RightStick", XINPUT_GAMEPAD_RIGHT_THUMB},
                {"Gamepad.DPadUp", XINPUT_GAMEPAD_DPAD_UP},
                {"Gamepad.DPadDown", XINPUT_GAMEPAD_DPAD_DOWN},
                {"Gamepad.DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT},
                {"Gamepad.DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT},
            };
            for (const auto& [name, mask] : gamepadButtons)
                setButton(input, name, (buttons & mask) != 0U);
            input.controls["Gamepad.LeftX"] =
                normalizedThumb(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.controls["Gamepad.LeftY"] =
                normalizedThumb(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.controls["Gamepad.RightX"] =
                normalizedThumb(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.controls["Gamepad.RightY"] =
                normalizedThumb(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.controls["Gamepad.LeftTrigger"] = normalizedTrigger(state.Gamepad.bLeftTrigger);
            input.controls["Gamepad.RightTrigger"] = normalizedTrigger(state.Gamepad.bRightTrigger);
        }
    }

    input.left = keyDown(VK_LEFT) || keyDown('A') || input.controls["Gamepad.LeftX"] < -0.5F ||
                 input.controls["Gamepad.DPadLeft"] > 0.5F;
    input.right = keyDown(VK_RIGHT) || keyDown('D') || input.controls["Gamepad.LeftX"] > 0.5F ||
                  input.controls["Gamepad.DPadRight"] > 0.5F;
    input.forward = keyDown(VK_UP) || keyDown('W') || input.controls["Gamepad.LeftY"] > 0.5F ||
                    input.controls["Gamepad.DPadUp"] > 0.5F;
    input.backward = keyDown(VK_DOWN) || keyDown('S') || input.controls["Gamepad.LeftY"] < -0.5F ||
                     input.controls["Gamepad.DPadDown"] > 0.5F;
    input.action = keyDown(VK_SPACE) || keyDown(VK_LBUTTON) || input.controls["Gamepad.A"] > 0.5F;
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
    if (message == WM_MOUSEWHEEL && self != nullptr) {
        self->wheelDelta_ += GET_WHEEL_DELTA_WPARAM(wparam);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace fabgl::player
