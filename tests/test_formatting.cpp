#include "test_framework.hpp"
#include "deox/core/formatting.hpp"

using namespace deox::core;

DEOX_TEST(format_bytes_scales) {
    ASSERT_EQ(format_bytes(500), "500 B");
    ASSERT_EQ(format_bytes(1024), "1.00 KB");
    ASSERT_EQ(format_bytes(1048576), "1.00 MB");
    ASSERT_EQ(format_bytes(1073741824), "1.00 GB");
}

DEOX_TEST(format_speed_scales) {
    ASSERT_EQ(format_speed(0), "0 B/s");
    ASSERT_EQ(format_speed(512), "512.0 B/s");
    ASSERT_EQ(format_speed(1048576), "1.00 MB/s");
    ASSERT_EQ(format_speed(10485760), "10.00 MB/s");
}

DEOX_TEST(format_duration_scales) {
    ASSERT_EQ(format_duration(std::chrono::seconds(0)), "--:--");
    ASSERT_EQ(format_duration(std::chrono::seconds(45)), "00m 45s");
    ASSERT_EQ(format_duration(std::chrono::seconds(125)), "02m 05s");
    ASSERT_EQ(format_duration(std::chrono::seconds(3665)), "01h 01m 05s");
}

DEOX_TEST(format_progress_bar_bounds) {
    auto b0 = format_progress_bar(0.0, 10, false);
    ASSERT_TRUE(b0.find("  0.0%") != std::string::npos);

    auto b50 = format_progress_bar(0.5, 10, false);
    ASSERT_TRUE(b50.find(" 50.0%") != std::string::npos);

    auto b100 = format_progress_bar(1.0, 10, false);
    ASSERT_TRUE(b100.find("100.0%") != std::string::npos);
}
