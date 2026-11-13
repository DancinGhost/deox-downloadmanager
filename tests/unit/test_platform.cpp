// SPDX-License-Identifier: MIT
// Tests of the operating-system independent contracts of deoxdm_platform.
// They run against whichever backend the build selected.
#include "test_framework.hpp"
#include "deoxdm/platform/file.hpp"
#include "deoxdm/platform/interrupt.hpp"
#include "deoxdm/platform/path.hpp"

#include <array>
#include <csignal>
#include <fstream>
#include <iterator>
#include <string>

using namespace deoxdm::platform;
using deoxdm::test::TempDir;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

DEOXDM_TEST(platform_utf8_path_round_trip) {
    // "Über café – 下载.bin" in UTF-8
    const std::string name = "\xC3\x9C" "ber caf\xC3\xA9 \xE2\x80\x93 \xE4\xB8\x8B\xE8\xBD\xBD.bin";
    const auto path = path_from_utf8(name);
    ASSERT_EQ(path_to_utf8(path), name);
    ASSERT_EQ(path_to_utf8(path.extension()), std::string(".bin"));
}

DEOXDM_TEST(platform_utf8_file_name_on_disk) {
    const TempDir dir;
    const std::string name = "r\xC3\xA9sum\xC3\xA9-\xE6\x96\x87\xE4\xBB\xB6.txt"; // "résumé-文件.txt"
    const auto path = dir.path() / path_from_utf8(name);

    ASSERT_TRUE(write_file_atomically(path, "hello").has_value());
    ASSERT_EQ(read_file(path), std::string("hello"));

    // The directory listing gives back the same UTF-8 name.
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
        found = found || path_to_utf8(entry.path().filename()) == name;
    }
    ASSERT_TRUE(found);
}

DEOXDM_TEST(platform_atomic_write_replaces_and_cleans_up) {
    const TempDir dir;
    const auto path = dir / "state.txt";

    ASSERT_TRUE(write_file_atomically(path, "first").has_value());
    ASSERT_TRUE(write_file_atomically(path, "second, longer").has_value());
    ASSERT_EQ(read_file(path), std::string("second, longer"));
    ASSERT_FALSE(std::filesystem::exists(atomic_temp_path(path)));
}

DEOXDM_TEST(platform_atomic_write_removes_stale_temp_file) {
    const TempDir dir;
    const auto path = dir / "state.txt";
    {
        std::ofstream leftover(atomic_temp_path(path), std::ios::binary);
        leftover << "left over from a crash";
    }
    ASSERT_TRUE(write_file_atomically(path, "fresh").has_value());
    ASSERT_EQ(read_file(path), std::string("fresh"));
    ASSERT_FALSE(std::filesystem::exists(atomic_temp_path(path)));
}

DEOXDM_TEST(platform_atomic_write_reports_missing_directory) {
    const TempDir dir;
    const auto result = write_file_atomically(dir / "missing" / "state.txt", "x");
    ASSERT_FALSE(result.has_value());
}

DEOXDM_TEST(platform_positional_io) {
    const TempDir dir;
    auto file = File::open(dir / "data.bin", OpenMode::CreateNew);
    ASSERT_TRUE(file.has_value());

    const std::array<uint8_t, 3> tail{'x', 'y', 'z'};
    const std::array<uint8_t, 3> head{'a', 'b', 'c'};
    ASSERT_TRUE(file->write_at(5, tail).has_value());
    ASSERT_TRUE(file->write_at(0, head).has_value());
    ASSERT_EQ(*file->size(), 8u);

    std::array<uint8_t, 3> buffer{};
    ASSERT_EQ(*file->read_at(5, buffer), 3u);
    ASSERT_TRUE(buffer == tail);
    ASSERT_EQ(*file->read_at(8, buffer), 0u); // end of file

    // CreateNew refuses an existing name.
    ASSERT_FALSE(File::open(dir / "data.bin", OpenMode::CreateNew).has_value());
}

#ifndef _WIN32
// Windows delivers console events on a separate thread and only for a real
// console, so this check is POSIX only.
DEOXDM_TEST(platform_interrupt_sets_flag) {
    install_interrupt_handler();
    ASSERT_FALSE(interrupt_requested());
    std::raise(SIGTERM);
    ASSERT_TRUE(interrupt_requested());
    // Leave the test process with default signal dispositions.
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGHUP, SIG_DFL);
}
#endif
