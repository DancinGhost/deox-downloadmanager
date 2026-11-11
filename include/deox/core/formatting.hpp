#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace deox::core {

// Pure function to format byte counts into human-readable strings (e.g. "14.25 MB")
[[nodiscard]] std::string format_bytes(uint64_t bytes) noexcept;

// Pure function to format transfer rate into human-readable strings (e.g. "25.40 MB/s")
[[nodiscard]] std::string format_speed(double bytes_per_sec) noexcept;

// Pure function to format duration into human-readable string (e.g. "01m 23s")
[[nodiscard]] std::string format_duration(std::chrono::seconds sec) noexcept;

// Pure function to render a progress bar with percentage
[[nodiscard]] std::string format_progress_bar(double ratio, size_t bar_width = 30, bool use_unicode = true) noexcept;

} // namespace deox::core
