#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// shellapi.h depends on Windows declarations in older MinGW SDKs.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace fabgl::tools {

inline std::vector<std::string> utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
    static_cast<void>(argc);
    static_cast<void>(argv);
    int wideCount = 0;
    auto** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wideArguments == nullptr) {
        throw std::runtime_error("CommandLineToArgvW failed");
    }
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(wideCount));
    for (auto index = 0; index < wideCount; ++index) {
        const auto length =
            WideCharToMultiByte(CP_UTF8, 0, wideArguments[index], -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0) {
            LocalFree(wideArguments);
            throw std::runtime_error("command line contains invalid Unicode");
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArguments[index], -1, value.data(), length, nullptr,
                            nullptr);
        value.pop_back();
        values.push_back(std::move(value));
    }
    LocalFree(wideArguments);
    return values;
#else
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(argc));
    for (auto index = 0; index < argc; ++index) {
        values.emplace_back(argv[index]);
    }
    return values;
#endif
}

inline std::vector<char*> mutableArgumentPointers(std::vector<std::string>& values) {
    std::vector<char*> pointers;
    pointers.reserve(values.size());
    for (auto& value : values) {
        pointers.push_back(value.data());
    }
    return pointers;
}

} // namespace fabgl::tools
