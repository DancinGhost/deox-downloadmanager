// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deoxdm::core {

// "14.25 MB" (binary units)
[[nodiscard]] std::string format_bytes(uint64_t bytes) noexcept;

// "25.40 MB/s"
[[nodiscard]] std::string format_speed(double bytes_per_sec) noexcept;

// "01m 23s", "02h 05m 09s"; "--:--" for a zero or negative duration
[[nodiscard]] std::string format_duration(std::chrono::seconds sec) noexcept;

// "[████████░░░░]  66.7%"
[[nodiscard]] std::string format_progress_bar(double ratio, size_t bar_width = 30, bool use_unicode = true) noexcept;

// Parses a size such as "4096", "500k", "1.5M" or "2G" (binary multiples,
// optional trailing "B"/"iB") into bytes.
[[nodiscard]] std::optional<uint64_t> parse_byte_size(std::string_view text) noexcept;

// Length in bytes of a character at `pos` that must not reach a file name or
// a terminal as it is: C0/C1 control characters (terminal escape sequences)
// and Unicode bidirectional controls (names that display differently from
// what they are). Returns 0 for anything else. `text` is UTF-8.
[[nodiscard]] size_t unsafe_character_length(std::string_view text, size_t pos) noexcept;

// `text` with every character unsafe_character_length() flags replaced by
// '?', for printing text that came from a server or a file to a terminal.
[[nodiscard]] std::string printable_text(std::string_view text);

} // namespace deoxdm::core
