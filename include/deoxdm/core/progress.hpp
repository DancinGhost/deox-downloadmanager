// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"
#include <chrono>
#include <optional>
#include <span>

namespace deoxdm::core {

struct ProgressSnapshot {
    uint64_t bytes_downloaded{0};
    uint64_t total_bytes{0};       // 0 when size_known is false
    bool size_known{true};
    double percentage{0.0};        // 0 when size_known is false
    double current_speed_bps{0.0}; // bytes per second
    double average_speed_bps{0.0}; // bytes per second over total time
    std::chrono::seconds estimated_remaining{0};
    uint32_t active_workers{0};
    uint32_t completed_chunks{0};
    uint32_t total_chunks{0};

    [[nodiscard]] constexpr bool is_finished() const noexcept {
        if (!size_known) return total_chunks > 0 && completed_chunks == total_chunks;
        return bytes_downloaded >= total_bytes;
    }
};

// One step of an exponential moving average over download speed samples
[[nodiscard]] double update_ema_speed(double prev_ema, double instantaneous, double alpha = 0.25) noexcept;

// Aggregates per-chunk state into overall progress.
// `total_bytes` is nullopt when the size of the download is unknown.
// `bytes_at_start` is what was already on disk when this run began (after a
// resume), so that the average speed only counts bytes transferred in this run.
[[nodiscard]] ProgressSnapshot calculate_progress(
    std::span<const ChunkState> chunks,
    std::optional<uint64_t> total_bytes,
    std::chrono::nanoseconds total_elapsed,
    double smoothed_speed_bps,
    uint64_t bytes_at_start = 0
) noexcept;

} // namespace deoxdm::core
