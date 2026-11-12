// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/progress.hpp"

#include <vector>

using namespace deoxdm::core;

DEOXDM_TEST(progress_ema_speed) {
    ASSERT_EQ(update_ema_speed(1000.0, 2000.0, 0.5), 1500.0);
    // The first sample is taken as is.
    ASSERT_EQ(update_ema_speed(0.0, 500.0), 500.0);
}

DEOXDM_TEST(progress_calculation_values) {
    const std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, 499}, .bytes_downloaded = 500, .status = ChunkStatus::Completed},
        ChunkState{.index = 1, .range = ByteRange{500, 999}, .bytes_downloaded = 250, .status = ChunkStatus::Downloading}
    };

    const auto snap = calculate_progress(chunks, 1000, std::chrono::seconds(2), 2500.0);
    ASSERT_EQ(snap.bytes_downloaded, 750u);
    ASSERT_EQ(snap.total_bytes, 1000u);
    ASSERT_TRUE(snap.size_known);
    ASSERT_EQ(snap.completed_chunks, 1u);
    ASSERT_EQ(snap.active_workers, 1u);
    ASSERT_EQ(snap.total_chunks, 2u);
    ASSERT_EQ(snap.percentage, 75.0);
    ASSERT_EQ(snap.average_speed_bps, 375.0);
    ASSERT_EQ(snap.estimated_remaining, std::chrono::seconds(1)); // 250 bytes at 2500 B/s, rounded up
    ASSERT_FALSE(snap.is_finished());
}

DEOXDM_TEST(progress_average_speed_ignores_resumed_bytes) {
    const std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, 999}, .bytes_downloaded = 800, .status = ChunkStatus::Downloading}
    };
    // 600 bytes were already on disk; only 200 arrived during these 2 seconds.
    const auto snap = calculate_progress(chunks, 1000, std::chrono::seconds(2), 100.0, 600);
    ASSERT_EQ(snap.average_speed_bps, 100.0);
    ASSERT_EQ(snap.percentage, 80.0);
}

DEOXDM_TEST(progress_finished_condition) {
    const std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, 999}, .bytes_downloaded = 1000, .status = ChunkStatus::Completed}
    };
    const auto snap = calculate_progress(chunks, 1000, std::chrono::seconds(1), 1000.0);
    ASSERT_TRUE(snap.is_finished());
    ASSERT_EQ(snap.percentage, 100.0);
    ASSERT_EQ(snap.estimated_remaining, std::chrono::seconds(0));
}

DEOXDM_TEST(progress_unknown_size) {
    std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, kOpenEndedByte}, .bytes_downloaded = 4096, .status = ChunkStatus::Downloading}
    };
    auto snap = calculate_progress(chunks, std::nullopt, std::chrono::seconds(1), 4096.0);
    ASSERT_FALSE(snap.size_known);
    ASSERT_EQ(snap.bytes_downloaded, 4096u);
    ASSERT_EQ(snap.percentage, 0.0);
    ASSERT_EQ(snap.estimated_remaining, std::chrono::seconds(0));
    ASSERT_FALSE(snap.is_finished());

    chunks[0].status = ChunkStatus::Completed;
    snap = calculate_progress(chunks, std::nullopt, std::chrono::seconds(1), 0.0);
    ASSERT_TRUE(snap.is_finished());
}

DEOXDM_TEST(progress_empty_file) {
    const auto snap = calculate_progress({}, 0, std::chrono::seconds(0), 0.0);
    ASSERT_TRUE(snap.is_finished());
    ASSERT_EQ(snap.percentage, 100.0);
}
