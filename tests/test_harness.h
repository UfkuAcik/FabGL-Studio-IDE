#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fabgl::tests {

struct TestCase final {
    std::string name;
    std::function<void()> function;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

class Registrar final {
  public:
    Registrar(std::string name, std::function<void()> function) {
        registry().push_back({std::move(name), std::move(function)});
    }
};

class AssertionFailure final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::ostringstream message;
    message << file << ':' << line << ": check failed: " << expression;
    throw AssertionFailure(message.str());
}

inline void checkNear(float actual, float expected, float epsilon, const char* file, int line) {
    if (std::fabs(actual - expected) <= epsilon)
        return;
    std::ostringstream message;
    message << file << ':' << line << ": expected " << expected << ", got " << actual
            << " (epsilon " << epsilon << ')';
    throw AssertionFailure(message.str());
}

} // namespace fabgl::tests

#define FGL_TEST(name)                                                                             \
    static void name();                                                                            \
    static ::fabgl::tests::Registrar name##_registrar(#name, &name);                               \
    static void name()

#define FGL_CHECK(...)                                                                             \
    ::fabgl::tests::check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)

#define FGL_CHECK_NEAR(actual, expected, epsilon)                                                  \
    ::fabgl::tests::checkNear(static_cast<float>(actual), static_cast<float>(expected), epsilon,   \
                              __FILE__, __LINE__)
