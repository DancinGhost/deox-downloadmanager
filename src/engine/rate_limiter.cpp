// SPDX-License-Identifier: MIT
#include "deoxdm/engine/rate_limiter.hpp"

#include <algorithm>

namespace deoxdm::engine {

void RateLimiter::set_rate(uint64_t bytes_per_second) noexcept {
    rate_.store(bytes_per_second, std::memory_order_relaxed);
    std::scoped_lock lock(mutex_);
    next_free_ = {};
    wakeup_.notify_all();
}

bool RateLimiter::acquire(uint64_t bytes, const std::stop_token& stop_token) {
    const uint64_t current_rate = rate_.load(std::memory_order_relaxed);
    if (current_rate == 0 || bytes == 0) {
        return !stop_token.stop_requested();
    }

    std::unique_lock lock(mutex_);

    // Each request books the time its bytes take at the target rate, starting
    // where the previous booking ended. Idle time is not carried over.
    const auto now = Clock::now();
    const auto cost = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(static_cast<double>(bytes) / static_cast<double>(current_rate)));
    next_free_ = std::max(next_free_, now) + cost;
    const auto ready_at = next_free_;

    // Returns early when a stop is requested or the rate is changed to unlimited.
    wakeup_.wait_until(lock, stop_token, ready_at, [this] {
        return rate_.load(std::memory_order_relaxed) == 0;
    });
    return !stop_token.stop_requested();
}

} // namespace deoxdm::engine
