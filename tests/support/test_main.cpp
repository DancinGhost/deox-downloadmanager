// SPDX-License-Identifier: MIT
#include "test_framework.hpp"

#include <exception>
#include <iostream>
#include <string_view>

// Usage: deox_tests [FILTER]   runs the tests whose name contains FILTER
//        deox_tests --list     prints the test names
int main(int argc, char* argv[]) {
    const auto& tests = deoxdm::test::get_registry();
    const std::string_view filter = (argc > 1) ? argv[1] : "";

    if (filter == "--list") {
        for (const auto& test : tests) {
            std::cout << test.name << "\n";
        }
        return 0;
    }

    size_t run = 0;
    size_t failed = 0;

    for (const auto& test : tests) {
        if (test.name.find(filter) == std::string_view::npos) {
            continue;
        }
        ++run;
        try {
            test.func();
            std::cout << "  ok    " << test.name << std::endl;
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  FAIL  " << test.name << "\n          " << e.what() << std::endl;
        }
    }

    if (run == 0) {
        std::cout << "No tests match '" << filter << "'\n";
        return 1;
    }

    std::cout << "\n" << (run - failed) << " of " << run << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
