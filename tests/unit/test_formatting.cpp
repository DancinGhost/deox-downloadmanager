// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/formatting.hpp"

using namespace deoxdm::core;

DEOXDM_TEST(format_bytes_scales) {
    ASSERT_EQ(format_bytes(0), "0 B");
    ASSERT_EQ(format_bytes(500), "500 B");
    ASSERT_EQ(format_bytes(1024), "1.00 KB");
    ASSERT_EQ(format_bytes(1048576), "1.00 MB");
    ASSERT_EQ(format_bytes(1073741824), "1.00 GB");
    ASSERT_EQ(format_bytes(1099511627776ull * 3), "3.00 TB");
}

DEOXDM_TEST(format_speed_scales) {
    ASSERT_EQ(format_speed(0), "0 B/s");
    ASSERT_EQ(format_speed(-5), "0 B/s");
    ASSERT_EQ(format_speed(512), "512.0 B/s");
    ASSERT_EQ(format_speed(1048576), "1.00 MB/s");
    ASSERT_EQ(format_speed(10485760), "10.00 MB/s");
}

DEOXDM_TEST(format_duration_scales) {
    ASSERT_EQ(format_duration(std::chrono::seconds(0)), "--:--");
    ASSERT_EQ(format_duration(std::chrono::seconds(45)), "00m 45s");
    ASSERT_EQ(format_duration(std::chrono::seconds(125)), "02m 05s");
    ASSERT_EQ(format_duration(std::chrono::seconds(3665)), "01h 01m 05s");
}

DEOXDM_TEST(format_progress_bar_bounds) {
    ASSERT_EQ(format_progress_bar(0.0, 10, false), "[          ]   0.0%");
    ASSERT_EQ(format_progress_bar(0.5, 10, false), "[=====     ]  50.0%");
    ASSERT_EQ(format_progress_bar(1.0, 10, false), "[==========] 100.0%");
    // Out-of-range ratios are clamped.
    ASSERT_EQ(format_progress_bar(7.0, 4, false), "[====] 100.0%");
    ASSERT_EQ(format_progress_bar(-1.0, 4, false), "[    ]   0.0%");
}

DEOXDM_TEST(parse_byte_size_units) {
    ASSERT_EQ(parse_byte_size("0"), 0u);
    ASSERT_EQ(parse_byte_size("4096"), 4096u);
    ASSERT_EQ(parse_byte_size("500k"), 500u * 1024);
    ASSERT_EQ(parse_byte_size("500K"), 500u * 1024);
    ASSERT_EQ(parse_byte_size("2M"), 2u * 1024 * 1024);
    ASSERT_EQ(parse_byte_size("1.5m"), 1572864u);
    ASSERT_EQ(parse_byte_size("1G"), 1073741824u);
    ASSERT_EQ(parse_byte_size(" 10MiB "), 10u * 1024 * 1024);
    ASSERT_EQ(parse_byte_size("3kb"), 3072u);

    ASSERT_FALSE(parse_byte_size("").has_value());
    ASSERT_FALSE(parse_byte_size("fast").has_value());
    ASSERT_FALSE(parse_byte_size("-5k").has_value());
    ASSERT_FALSE(parse_byte_size("5x").has_value());
    ASSERT_FALSE(parse_byte_size("1e3").has_value());
    ASSERT_FALSE(parse_byte_size("99999999999999999999G").has_value());
}
