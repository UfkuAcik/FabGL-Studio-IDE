#include "test_harness.h"

#include <exception>
#include <iostream>

int main() {
    std::size_t passed = 0;
    std::size_t failed = 0;
    for (const auto& test : fabgl::tests::registry()) {
        try {
            test.function();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    std::cout << "Executed " << (passed + failed) << " tests: " << passed << " passed, " << failed
              << " failed.\n";
    return failed == 0 ? 0 : 1;
}
