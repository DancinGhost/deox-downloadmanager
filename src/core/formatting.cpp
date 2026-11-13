// SPDX-License-Identifier: MIT
#include "deoxdm/core/formatting.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace deoxdm::core {

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

std::optional<uint64_t> parse_byte_size(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);

    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed);
    if (ec != std::errc{} || !std::isfinite(value) || value < 0.0) {
        return std::nullopt;
    }

    std::string suffix(ptr, text.data() + text.size());
    std::ranges::transform(suffix, suffix.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });

    double multiplier = 1.0;
    if (suffix.empty() || suffix == "b") {
        multiplier = 1.0;
    } else if (suffix == "k" || suffix == "kb" || suffix == "kib") {
        multiplier = 1024.0;
    } else if (suffix == "m" || suffix == "mb" || suffix == "mib") {
        multiplier = 1024.0 * 1024.0;
    } else if (suffix == "g" || suffix == "gb" || suffix == "gib") {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    } else {
        return std::nullopt;
    }

    const double bytes = std::floor(value * multiplier);
    if (bytes >= 9.0e18) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(bytes);
}

size_t unsafe_character_length(std::string_view text, size_t pos) noexcept {
    if (pos >= text.size()) {
        return 0;
    }
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    const size_t left = text.size() - pos;
    const unsigned char b0 = byte(pos);

    // C0 controls and DEL: ESC starts terminal escape sequences.
    if (b0 < 0x20 || b0 == 0x7F) {
        return 1;
    }
    // C1 controls U+0080..U+009F (C2 80..C2 9F); U+009B is a one-character CSI.
    if (b0 == 0xC2 && left >= 2 && byte(pos + 1) >= 0x80 && byte(pos + 1) <= 0x9F) {
        return 2;
    }
    // U+061C ARABIC LETTER MARK (D8 9C)
    if (b0 == 0xD8 && left >= 2 && byte(pos + 1) == 0x9C) {
        return 2;
    }
    if (b0 == 0xE2 && left >= 3) {
        const unsigned char b1 = byte(pos + 1);
        const unsigned char b2 = byte(pos + 2);
        // U+200E/U+200F LRM/RLM (E2 80 8E..8F), U+202A..U+202E embeddings and
        // overrides (E2 80 AA..AE): these make "evil<U+202E>fdp.exe" display as
        // "evilexe.pdf".
        if (b1 == 0x80 && (b2 == 0x8E || b2 == 0x8F || (b2 >= 0xAA && b2 <= 0xAE))) {
            return 3;
        }
        // U+2066..U+2069 isolates (E2 81 A6..A9)
        if (b1 == 0x81 && b2 >= 0xA6 && b2 <= 0xA9) {
            return 3;
        }
    }
    return 0;
}

std::string printable_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        if (const size_t unsafe = unsafe_character_length(text, pos); unsafe > 0) {
            out.push_back('?');
            pos += unsafe;
        } else {
            out.push_back(text[pos]);
            ++pos;
        }
    }
    return out;
}

} // namespace deoxdm::core
