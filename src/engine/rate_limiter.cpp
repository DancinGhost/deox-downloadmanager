// SPDX-License-Identifier: MIT
#include "deoxdm/engine/rate_limiter.hpp"

#include <algorithm>

namespace deoxdm::engine {

bool RateLimiter::acquire(uint64_t bytes, const std::stop_token& stop_token) {
    if (rate_ == 0 || bytes == 0) {
        return !stop_token.stop_requested();
    }

    std::unique_lock lock(mutex_);

    // Each request books the time its bytes take at the target rate, starting
    // where the previous booking ended. Idle time is not carried over.
    const auto now = Clock::now();
    const auto cost = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(static_cast<double>(bytes) / static_cast<double>(rate_)));
    next_free_ = std::max(next_free_, now) + cost;
    const auto ready_at = next_free_;

    // Returns early only when a stop is requested.
    wakeup_.wait_until(lock, stop_token, ready_at, [] { return false; });
    return !stop_token.stop_requested();
}

} // namespace deoxdm::engine
