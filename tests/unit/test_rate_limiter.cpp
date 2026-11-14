// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/engine/rate_limiter.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using deoxdm::engine::RateLimiter;
using Clock = std::chrono::steady_clock;

DEOXDM_TEST(rate_limiter_unlimited_never_waits) {
    RateLimiter limiter(0);
    const auto start = Clock::now();
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(limiter.acquire(1'000'000, {}));
    }
    ASSERT_TRUE(Clock::now() - start < std::chrono::milliseconds(200));
}

DEOXDM_TEST(rate_limiter_enforces_rate_across_threads) {
    // 4 threads push 10 KB each through a 100 KB/s limiter: 40 KB take 0.4 s.
    RateLimiter limiter(100 * 1024);
    const auto start = Clock::now();
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&limiter] {
                for (int i = 0; i < 10; ++i) {
                    (void)limiter.acquire(1024, {});
                }
            });
        }
    }
    const auto elapsed = Clock::now() - start;
    ASSERT_TRUE(elapsed >= std::chrono::milliseconds(350));
    ASSERT_TRUE(elapsed < std::chrono::milliseconds(3000));
}

DEOXDM_TEST(rate_limiter_wait_is_interruptible) {
    RateLimiter limiter(10); // 10 bytes/second: the request below would take 100 s
    std::stop_source stop;
    std::atomic<bool> result{true};

    const auto start = Clock::now();
    std::jthread waiter([&] { result = limiter.acquire(1000, stop.get_token()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.request_stop();
    waiter.join();

    ASSERT_FALSE(result.load());
    ASSERT_TRUE(Clock::now() - start < std::chrono::seconds(5));
}

DEOXDM_TEST(rate_limiter_dynamic_set_rate_wakes_waiter) {
    RateLimiter limiter(10); // 10 bytes/second: would wait 100 s
    std::atomic<bool> result{false};

    const auto start = Clock::now();
    std::jthread waiter([&] { result = limiter.acquire(1000, {}); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    limiter.set_rate(0); // switch to unlimited
    waiter.join();

    ASSERT_TRUE(result.load());
    ASSERT_EQ(limiter.rate(), 0u);
    ASSERT_TRUE(Clock::now() - start < std::chrono::seconds(5));
}
