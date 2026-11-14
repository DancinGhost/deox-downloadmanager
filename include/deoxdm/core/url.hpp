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

// Whether two parsed URLs share the same scheme, host (case-insensitive) and port.
[[nodiscard]] bool same_origin(const Url& a, const Url& b) noexcept;

// Whether `host` names a loopback, link-local, or private-network address
// (e.g. localhost, 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16,
// 169.254.0.0/16, [::1], [fe80::]/10, [fc00::]/7).
[[nodiscard]] bool is_loopback_or_private_host(std::string_view host) noexcept;

// Decodes %XX escapes. Malformed escapes are kept verbatim.
[[nodiscard]] std::string percent_decode(std::string_view text) noexcept;

// Derives a safe local file name from a URL path.
[[nodiscard]] std::string extract_filename_from_url(std::string_view path, std::string_view fallback = "download.bin") noexcept;

// Reduces an untrusted name to a single safe path component: directory parts
// and leading dots are removed; control characters, bidirectional controls and
// characters that are illegal on common file systems are replaced; Windows
// device names ("CON", "nul.txt") get a '_' prefix. Never returns an empty
// string, a hidden (dot) file name, "." or "..".
[[nodiscard]] std::string sanitize_filename(std::string_view filename) noexcept;

// `url` with the user name and password, if any, replaced by "***", for
// showing it to a user or writing it to a log.
[[nodiscard]] std::string redact_credentials(std::string_view url);

// Extracts the file name from a Content-Disposition header value, preferring
// the RFC 5987 `filename*=` form. Returns an empty string when there is none.
[[nodiscard]] std::string filename_from_content_disposition(std::string_view header_value) noexcept;

// "archive.tar.gz", 2 -> "archive.2.tar.gz"; "notes", 1 -> "notes.1"
[[nodiscard]] std::string numbered_filename(std::string_view filename, uint32_t number) noexcept;

} // namespace deoxdm::core
