#pragma once

#include "deox/core/types.hpp"
#include <string>
#include <string_view>
#include <expected>

namespace deox::core {

struct Url {
    std::string scheme;     // "http" or "https"
    std::string host;       // e.g. "example.com"
    uint16_t port{0};       // 80, 443, or custom
    std::string path;       // "/path/to/file"
    std::string query;      // "foo=bar"
    std::string filename;   // extracted & sanitized default filename

    [[nodiscard]] std::string full_url() const;
};

// Pure URL parser and validator
[[nodiscard]] std::expected<Url, DeoxError> parse_url(std::string_view raw_url) noexcept;

// Pure filename extractor from URL path
[[nodiscard]] std::string extract_filename_from_url(std::string_view path, std::string_view fallback = "download.bin") noexcept;

// Pure filename sanitizer: strips path traversal (../), slashes, nulls, control chars
[[nodiscard]] std::string sanitize_filename(std::string_view filename) noexcept;

} // namespace deox::core
