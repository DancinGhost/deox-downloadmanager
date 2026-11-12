// SPDX-License-Identifier: MIT
#include "deoxdm/core/progress.hpp"
#include <algorithm>
#include <cmath>

namespace deoxdm::core {

double update_ema_speed(double prev_ema, double instantaneous, double alpha) noexcept {
    if (prev_ema <= 0.0) {
        return instantaneous;
    }
    return (alpha * instantaneous) + ((1.0 - alpha) * prev_ema);
}

ProgressSnapshot calculate_progress(
    std::span<const ChunkState> chunks,
    std::optional<uint64_t> total_bytes_opt,
    std::chrono::nanoseconds total_elapsed,
    double smoothed_speed_bps,
    uint64_t bytes_at_start
) noexcept {
    ProgressSnapshot snap;
    const uint64_t total_bytes = total_bytes_opt.value_or(0);
    snap.total_bytes = total_bytes;
    snap.size_known = total_bytes_opt.has_value();
    snap.total_chunks = static_cast<uint32_t>(chunks.size());

    uint64_t sum_downloaded = 0;
    uint32_t completed = 0;
    uint32_t active = 0;

    for (const auto& chunk : chunks) {
        sum_downloaded += chunk.bytes_downloaded;
        if (chunk.status == ChunkStatus::Completed || chunk.is_complete()) {
            ++completed;
        } else if (chunk.status == ChunkStatus::Downloading) {
            ++active;
        }
    }

    snap.bytes_downloaded = sum_downloaded;
    snap.completed_chunks = completed;
    snap.active_workers = active;

    if (snap.size_known) {
        snap.percentage = (total_bytes > 0)
            ? std::min(100.0, static_cast<double>(sum_downloaded) / static_cast<double>(total_bytes) * 100.0)
            : 100.0;
    }

    const double elapsed_seconds = std::chrono::duration<double>(total_elapsed).count();
    if (elapsed_seconds > 0.001) {
        const uint64_t this_run = (sum_downloaded > bytes_at_start) ? (sum_downloaded - bytes_at_start) : 0;
        snap.average_speed_bps = static_cast<double>(this_run) / elapsed_seconds;
    }

    snap.current_speed_bps = (smoothed_speed_bps >= 0.0) ? smoothed_speed_bps : 0.0;

    // Remaining time; left at zero ("unknown") while the speed is too low to be meaningful
    if (total_bytes > sum_downloaded && snap.current_speed_bps > 1024.0) {
        const double bytes_remaining = static_cast<double>(total_bytes - sum_downloaded);
        const double seconds_remaining = bytes_remaining / snap.current_speed_bps;
        snap.estimated_remaining = std::chrono::seconds(static_cast<int64_t>(std::ceil(seconds_remaining)));
    }

    return snap;
}

} // namespace deoxdm::core
