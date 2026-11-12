// SPDX-License-Identifier: MIT
#include "deoxdm/core/http_headers.hpp"

#include <charconv>

namespace deoxdm::core {

namespace {

constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

constexpr std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

template <typename T>
std::optional<T> parse_uint(std::string_view sv) noexcept {
    T value{};
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec == std::errc{} && ptr == sv.data() + sv.size() && !sv.empty()) {
        return value;
    }
    return std::nullopt;
}

} // namespace

std::optional<ContentRange> parse_content_range(std::string_view value) noexcept {
    value = trim(value);
    constexpr std::string_view unit = "bytes";
    if (value.size() <= unit.size() || !iequals(value.substr(0, unit.size()), unit)) {
        return std::nullopt;
    }
    value = trim(value.substr(unit.size()));

    const auto slash = value.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view range = trim(value.substr(0, slash));
    const std::string_view total = trim(value.substr(slash + 1));

    const auto dash = range.find('-');
    if (dash == std::string_view::npos) {
        return std::nullopt; // "bytes */1234" carries no range
    }

    const auto first = parse_uint<uint64_t>(trim(range.substr(0, dash)));
    const auto last = parse_uint<uint64_t>(trim(range.substr(dash + 1)));
    if (!first || !last || *last < *first) {
        return std::nullopt;
    }

    ContentRange result{*first, *last, std::nullopt};
    if (total != "*") {
        result.total_size = parse_uint<uint64_t>(total);
        if (!result.total_size || *result.total_size <= *last) {
            return std::nullopt;
        }
    }
    return result;
}

void ResponseHeaders::feed(std::string_view line) noexcept {
    line = trim(line);

    if (line.starts_with("HTTP/")) {
        *this = ResponseHeaders{};
        const auto space = line.find(' ');
        if (space != std::string_view::npos) {
            const std::string_view rest = trim(line.substr(space + 1));
            status = parse_uint<uint32_t>(rest.substr(0, rest.find(' '))).value_or(0);
        }
        return;
    }

    const auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        return;
    }
    const std::string_view name = trim(line.substr(0, colon));
    const std::string_view value = trim(line.substr(colon + 1));

    if (iequals(name, "Content-Length")) {
        content_length = parse_uint<uint64_t>(value);
    } else if (iequals(name, "Content-Range")) {
        content_range = parse_content_range(value);
    } else if (iequals(name, "Accept-Ranges")) {
        accepts_ranges = iequals(value, "bytes");
    } else if (iequals(name, "ETag")) {
        etag = std::string(value);
    } else if (iequals(name, "Last-Modified")) {
        last_modified = std::string(value);
    } else if (iequals(name, "Content-Disposition")) {
        content_disposition = std::string(value);
    }
}

} // namespace deoxdm::core
