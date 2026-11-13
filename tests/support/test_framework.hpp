// SPDX-License-Identifier: MIT
#pragma once

// A minimal test framework: tests register themselves, a failed assertion
// throws and fails only the test it belongs to.

#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace deoxdm::test {

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

struct AssertionFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(std::string_view expression, std::string_view file, int line) {
    const auto slash = file.find_last_of('/');
    throw AssertionFailure(std::format("{} ({}:{})", expression,
        slash == std::string_view::npos ? file : file.substr(slash + 1), line));
}

// A scratch directory that is removed again when the test ends.
class TempDir {
public:
    // Portable stand-in for mkdtemp(): create_directory() fails when the
    // name exists, so a random name is retried until one is new.
    TempDir() {
        std::random_device random;
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base / std::format("deoxdm-test-{:08x}{:08x}", random(), random());
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec)) {
                std::filesystem::permissions(candidate, std::filesystem::perms::owner_all,
                                             std::filesystem::perm_options::replace, ec);
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot create a temporary directory");
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] std::filesystem::path operator/(std::string_view name) const {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

} // namespace deoxdm::test

#define DEOXDM_TEST(name) \
    static void test_func_##name(); \
    [[maybe_unused]] static const bool reg_##name = ::deoxdm::test::register_test(#name, test_func_##name); \
    static void test_func_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) ::deoxdm::test::fail("expected true: " #cond, __FILE__, __LINE__); \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) ::deoxdm::test::fail("expected false: " #cond, __FILE__, __LINE__); \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) ::deoxdm::test::fail("expected equal: " #a " == " #b, __FILE__, __LINE__); \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) ::deoxdm::test::fail("expected different: " #a " != " #b, __FILE__, __LINE__); \
    } while (0)
