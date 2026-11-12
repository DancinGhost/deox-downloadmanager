// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"
#include <expected>
#include <string>
#include <string_view>

namespace deoxdm::core {

struct Url {
    std::string scheme;     // "http" or "https"
    std::string userinfo;   // "user:password", usually empty
    std::string host;       // "example.com" or "[::1]"
    uint16_t port{0};       // explicit port, or the scheme default
    std::string path;       // "/path/to/file", never empty
    std::string query;      // "foo=bar" (without the '?')
    std::string filename;   // decoded, sanitized file name derived from the path

    [[nodiscard]] std::string full_url() const;
};

// Parses and validates an absolute http(s) URL. Any fragment is dropped.
[[nodiscard]] std::expected<Url, Error> parse_url(std::string_view raw_url) noexcept;

// Decodes %XX escapes. Malformed escapes are kept verbatim.
[[nodiscard]] std::string percent_decode(std::string_view text) noexcept;

// Derives a safe local file name from a URL path.
[[nodiscard]] std::string extract_filename_from_url(std::string_view path, std::string_view fallback = "download.bin") noexcept;

// Reduces an untrusted name to a single safe path component: directory parts,
// control characters and characters that are illegal on common file systems
// are removed. Never returns an empty string, "." or "..".
[[nodiscard]] std::string sanitize_filename(std::string_view filename) noexcept;

// Extracts the file name from a Content-Disposition header value, preferring
// the RFC 5987 `filename*=` form. Returns an empty string when there is none.
[[nodiscard]] std::string filename_from_content_disposition(std::string_view header_value) noexcept;

// "archive.tar.gz", 2 -> "archive.2.tar.gz"; "notes", 1 -> "notes.1"
[[nodiscard]] std::string numbered_filename(std::string_view filename, uint32_t number) noexcept;

} // namespace deoxdm::core
