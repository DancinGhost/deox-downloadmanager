#include "deox/core/progress.hpp"
#include <numeric>
#include <cmath>

namespace deox::core {

double update_ema_speed(double prev_ema, double instantaneous, double alpha) noexcept {
    if (prev_ema <= 0.0) {
        return instantaneous;
    }
    return (alpha * instantaneous) + ((1.0 - alpha) * prev_ema);
}

ProgressSnapshot calculate_progress(
    std::span<const ChunkState> chunks,
    uint64_t total_bytes,
    std::chrono::nanoseconds total_elapsed,
    double smoothed_speed_bps
) noexcept {
    ProgressSnapshot snap;
    snap.total_bytes = total_bytes;
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

    if (total_bytes > 0) {
        snap.percentage = (static_cast<double>(sum_downloaded) / static_cast<double>(total_bytes)) * 100.0;
        if (snap.percentage > 100.0) snap.percentage = 100.0;
    } else {
        snap.percentage = 0.0;
    }

    const double elapsed_seconds = std::chrono::duration<double>(total_elapsed).count();
    if (elapsed_seconds > 0.001) {
        snap.average_speed_bps = static_cast<double>(sum_downloaded) / elapsed_seconds;
    } else {
        snap.average_speed_bps = 0.0;
    }

    snap.current_speed_bps = (smoothed_speed_bps >= 0.0) ? smoothed_speed_bps : 0.0;

    // Calculate Estimated Time of Arrival (ETA)
    if (total_bytes > sum_downloaded && snap.current_speed_bps > 1024.0) {
        const double bytes_remaining = static_cast<double>(total_bytes - sum_downloaded);
        const double seconds_remaining = bytes_remaining / snap.current_speed_bps;
        snap.estimated_remaining = std::chrono::seconds(static_cast<int64_t>(std::ceil(seconds_remaining)));
    } else {
        snap.estimated_remaining = std::chrono::seconds(0);
    }

    return snap;
}

} // namespace deox::core
