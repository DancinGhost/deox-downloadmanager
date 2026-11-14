// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>

namespace deoxdm::engine {

// Caps the combined throughput of all connections of one download.
//
// Every thread reports the bytes it is about to write and is put to sleep
// until those bytes fit the configured rate. Because the budget is shared, the
// cap holds no matter how many connections are active.
class RateLimiter {
public:
    // bytes_per_second == 0 disables limiting.
    explicit RateLimiter(uint64_t bytes_per_second) noexcept
        : rate_(bytes_per_second) {}

    void set_rate(uint64_t bytes_per_second) noexcept;

    [[nodiscard]] uint64_t rate() const noexcept {
        return rate_.load(std::memory_order_relaxed);
    }

    // Blocks until `bytes` may pass. Returns false if a stop was requested
    // while waiting.
    [[nodiscard]] bool acquire(uint64_t bytes, const std::stop_token& stop_token);

private:
    using Clock = std::chrono::steady_clock;

    std::atomic<uint64_t> rate_;
    std::mutex mutex_;
    std::condition_variable_any wakeup_;
    Clock::time_point next_free_{};
};

} // namespace deoxdm::engine
