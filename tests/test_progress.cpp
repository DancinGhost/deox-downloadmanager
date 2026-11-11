#include "test_framework.hpp"
#include "deox/core/progress.hpp"

using namespace deox::core;

DEOX_TEST(progress_ema_speed) {
    double speed = 1000.0;
    speed = update_ema_speed(speed, 2000.0, 0.5);
    ASSERT_EQ(speed, 1500.0);

    // Initial value
    ASSERT_EQ(update_ema_speed(0.0, 500.0), 500.0);
}

DEOX_TEST(progress_calculation_values) {
    std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, 499}, .bytes_downloaded = 500, .status = ChunkStatus::Completed},
        ChunkState{.index = 1, .range = ByteRange{500, 999}, .bytes_downloaded = 250, .status = ChunkStatus::Downloading}
    };

    const auto snap = calculate_progress(chunks, 1000, std::chrono::seconds(2), 250.0);
    ASSERT_EQ(snap.bytes_downloaded, 750);
    ASSERT_EQ(snap.total_bytes, 1000);
    ASSERT_EQ(snap.completed_chunks, 1);
    ASSERT_EQ(snap.active_workers, 1);
    ASSERT_EQ(snap.percentage, 75.0);
    ASSERT_EQ(snap.average_speed_bps, 375.0);
    ASSERT_FALSE(snap.is_finished());
}

DEOX_TEST(progress_finished_condition) {
    std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, 999}, .bytes_downloaded = 1000, .status = ChunkStatus::Completed}
    };
    const auto snap = calculate_progress(chunks, 1000, std::chrono::seconds(1), 1000.0);
    ASSERT_TRUE(snap.is_finished());
    ASSERT_EQ(snap.percentage, 100.0);
}
