#pragma once

#include "deox/core/types.hpp"
#include <span>
#include <chrono>

namespace deox::core {

struct ProgressSnapshot {
    uint64_t bytes_downloaded{0};
    uint64_t total_bytes{0};
    double percentage{0.0};
    double current_speed_bps{0.0}; // bytes per second
    double average_speed_bps{0.0}; // bytes per second over total time
    std::chrono::seconds estimated_remaining{0};
    uint32_t active_workers{0};
    uint32_t completed_chunks{0};
    uint32_t total_chunks{0};

    [[nodiscard]] constexpr bool is_finished() const noexcept {
        return total_bytes > 0 && bytes_downloaded >= total_bytes;
    }
};

// Pure function to update Exponential Moving Average of download speed
[[nodiscard]] double update_ema_speed(double prev_ema, double instantaneous, double alpha = 0.25) noexcept;

// Pure function to calculate progress metrics
[[nodiscard]] ProgressSnapshot calculate_progress(
    std::span<const ChunkState> chunks,
    uint64_t total_bytes,
    std::chrono::nanoseconds total_elapsed,
    double smoothed_speed_bps
) noexcept;

} // namespace deox::core
