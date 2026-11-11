#pragma once

#include <iostream>
#include <string_view>
#include <vector>
#include <functional>
#include <format>
#include <cstdlib>

namespace deox::test {

struct TestCase {
    std::string_view name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline bool register_test(std::string_view name, std::function<void()> func) {
    get_registry().push_back(TestCase{name, std::move(func)});
    return true;
}

#define DEOX_TEST(name) \
    static void test_func_##name(); \
    static const bool reg_##name = ::deox::test::register_test(#name, test_func_##name); \
    static void test_func_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << std::format("Assertion failed: {} ({}:{})\n", #cond, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << std::format("Assertion failed: {} == {} ({}:{})\n", #a, #b, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << std::format("Assertion failed: {} != {} ({}:{})\n", #a, #b, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (0)

} // namespace deox::test
