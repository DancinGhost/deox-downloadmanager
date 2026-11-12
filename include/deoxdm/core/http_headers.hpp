// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deoxdm::core {

struct ContentRange {
    uint64_t first_byte{0};
    uint64_t last_byte{0};
    std::optional<uint64_t> total_size; // nullopt for "bytes 0-99/*"
};

// Parses a Content-Range value such as "bytes 0-499/1234".
[[nodiscard]] std::optional<ContentRange> parse_content_range(std::string_view value) noexcept;

// The response headers the download engine cares about, accumulated line by line.
struct ResponseHeaders {
    uint32_t status{0};
    std::optional<uint64_t> content_length;
    std::optional<ContentRange> content_range;
    bool accepts_ranges{false};
    std::string etag;
    std::string last_modified;
    std::string content_disposition;

    // Feeds one raw header line. A status line ("HTTP/1.1 200 OK") starts a
    // new response and clears everything collected so far, so that after a
    // chain of redirects only the final response's headers remain.
    void feed(std::string_view line) noexcept;
};

} // namespace deoxdm::core
