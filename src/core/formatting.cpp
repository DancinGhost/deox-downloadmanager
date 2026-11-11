#include "deox/core/formatting.hpp"
#include <format>
#include <cmath>
#include <algorithm>

namespace deox::core {

std::string format_bytes(uint64_t bytes) noexcept {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    constexpr double TB = GB * 1024.0;

    const double d_bytes = static_cast<double>(bytes);

    if (d_bytes < KB) {
        return std::format("{} B", bytes);
    } else if (d_bytes < MB) {
        return std::format("{:.2f} KB", d_bytes / KB);
    } else if (d_bytes < GB) {
        return std::format("{:.2f} MB", d_bytes / MB);
    } else if (d_bytes < TB) {
        return std::format("{:.2f} GB", d_bytes / GB);
    } else {
        return std::format("{:.2f} TB", d_bytes / TB);
    }
}

std::string format_speed(double bytes_per_sec) noexcept {
    if (bytes_per_sec <= 0.0) {
        return "0 B/s";
    }

    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;

    if (bytes_per_sec < KB) {
        return std::format("{:.1f} B/s", bytes_per_sec);
    } else if (bytes_per_sec < MB) {
        return std::format("{:.2f} KB/s", bytes_per_sec / KB);
    } else if (bytes_per_sec < GB) {
        return std::format("{:.2f} MB/s", bytes_per_sec / MB);
    } else {
        return std::format("{:.2f} GB/s", bytes_per_sec / GB);
    }
}

std::string format_duration(std::chrono::seconds sec) noexcept {
    if (sec.count() <= 0) {
        return "--:--";
    }

    const auto total_secs = sec.count();
    const auto hours = total_secs / 3600;
    const auto minutes = (total_secs % 3600) / 60;
    const auto seconds = total_secs % 60;

    if (hours > 0) {
        return std::format("{:02d}h {:02d}m {:02d}s", hours, minutes, seconds);
    } else {
        return std::format("{:02d}m {:02d}s", minutes, seconds);
    }
}

std::string format_progress_bar(double ratio, size_t bar_width, bool use_unicode) noexcept {
    ratio = std::clamp(ratio, 0.0, 1.0);
    const size_t filled = static_cast<size_t>(std::round(ratio * static_cast<double>(bar_width)));
    const double percentage = ratio * 100.0;

    std::string bar;
    bar.reserve(bar_width * 4 + 16);
    bar += "[";

    if (use_unicode) {
        for (size_t i = 0; i < bar_width; ++i) {
            if (i < filled) {
                bar += "█";
            } else {
                bar += "░";
            }
        }
    } else {
        for (size_t i = 0; i < bar_width; ++i) {
            if (i < filled) {
                bar += '=';
            } else {
                bar += ' ';
            }
        }
    }

    bar += std::format("] {:5.1f}%", percentage);
    return bar;
}

} // namespace deox::core
