#include "test_framework.hpp"
#include <iostream>
#include <format>

int main() {
    auto& tests = deox::test::get_registry();
    std::cout << std::format("[deox-tests] Running {} unit and integration tests...\n", tests.size());

    size_t passed = 0;
    for (const auto& t : tests) {
        std::cout << std::format("  • Running test: {:<35} ... ", t.name);
        std::cout.flush();
        t.func();
        std::cout << "\033[1;32mPASSED\033[0m\n";
        ++passed;
    }

    std::cout << std::format("\n\033[1;32m[deox-tests] All {} tests passed successfully!\033[0m\n", passed);
    return 0;
}
