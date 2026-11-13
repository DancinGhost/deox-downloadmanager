// SPDX-License-Identifier: MIT
#include "deoxdm/core/url.hpp"
#include "deoxdm/core/formatting.hpp"

#include <algorithm>
#include <charconv>

namespace deoxdm::core {

namespace {

constexpr std::string_view kFallbackName = "download.bin";
constexpr size_t kMaxFilenameBytes = 200; // leaves room for ".deoxdm.tmp" and ".N" suffixes under NAME_MAX

constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

bool starts_with_icase(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (ascii_lower(text[i]) != ascii_lower(prefix[i])) return false;
    }
    return true;
}

// Position of the extension that should stay at the end of a numbered name.
size_t extension_start(std::string_view name) noexcept {
    const size_t last_dot = name.rfind('.');
    if (last_dot == std::string_view::npos || last_dot == 0) {
        return name.size();
    }
    // Keep compound archive extensions such as ".tar.gz" together.
    const std::string_view stem = name.substr(0, last_dot);
    constexpr std::string_view tar = ".tar";
    if (stem.size() > tar.size() && starts_with_icase(stem.substr(stem.size() - tar.size()), tar)) {
        return stem.size() - tar.size();
    }
    return last_dot;
}

// CON, PRN, AUX, NUL, COM1-COM9 and LPT1-LPT9, with or without an extension.
bool is_windows_device_name(std::string_view name) noexcept {
    std::string_view stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.remove_suffix(1);
    const auto is = [&](std::string_view device) {
        return stem.size() == device.size() && starts_with_icase(stem, device);
    };
    if (is("con") || is("prn") || is("aux") || is("nul")) {
        return true;
    }
    return stem.size() == 4 && (starts_with_icase(stem, "com") || starts_with_icase(stem, "lpt")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

} // namespace

std::string Url::full_url() const {
    std::string res = scheme + "://";
    if (!userinfo.empty()) {
        res += userinfo + "@";
    }
    res += host;
    const bool default_port = (scheme == "http" && port == 80) || (scheme == "https" && port == 443);
    if (port != 0 && !default_port) {
        res += ":" + std::to_string(port);
    }
    if (path.empty() || path.front() != '/') {
        res += '/';
    }
    res += path;
    if (!query.empty()) {
        res += '?' + query;
    }
    return res;
}

std::string percent_decode(std::string_view text) noexcept {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_value(text[i + 1]);
            const int lo = hex_value(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string sanitize_filename(std::string_view filename) noexcept {
    // Keep only the last path component, whichever separator was used.
    const auto last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string_view::npos) {
        filename = filename.substr(last_slash + 1);
    }

    std::string sanitized;
    sanitized.reserve(filename.size());

    for (size_t pos = 0; pos < filename.size();) {
        // Control characters, and bidirectional controls that would make the
        // name display differently from what it is.
        if (const size_t unsafe = unsafe_character_length(filename, pos); unsafe > 0) {
            sanitized.push_back('_');
            pos += unsafe;
            continue;
        }
        const char ch = filename[pos++];
        const bool reserved = ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
                              ch == '<' || ch == '>' || ch == '|';
        // Other bytes >= 0x80 are parts of UTF-8 sequences and are kept as they are.
        sanitized.push_back(reserved ? '_' : ch);
    }

    // Leading/trailing spaces and trailing dots are trouble on several file
    // systems. Leading dots would let a server create hidden files such as
    // ".bashrc" or ".htaccess".
    while (!sanitized.empty() && (sanitized.front() == ' ' || sanitized.front() == '.')) sanitized.erase(sanitized.begin());
    while (!sanitized.empty() && (sanitized.back() == ' ' || sanitized.back() == '.')) sanitized.pop_back();

    if (sanitized.size() > kMaxFilenameBytes) {
        // Shorten the stem and keep the extension, without splitting a UTF-8 sequence.
        const size_t ext_pos = extension_start(sanitized);
        std::string ext = sanitized.substr(ext_pos);
        if (ext.size() > 32) ext.clear();
        size_t keep = kMaxFilenameBytes - ext.size();
        while (keep > 0 && (static_cast<unsigned char>(sanitized[keep]) & 0xC0) == 0x80) --keep;
        sanitized = sanitized.substr(0, keep) + ext;
    }

    if (sanitized.empty()) {
        return std::string(kFallbackName);
    }

    // On Windows "CON", "nul.txt" and the like name devices, not files.
    if (is_windows_device_name(sanitized)) {
        sanitized.insert(sanitized.begin(), '_');
    }

    return sanitized;
}

std::string redact_credentials(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string(url);
    }
    const size_t authority_start = scheme_end + 3;
    const size_t authority_end = std::min(url.find_first_of("/?#", authority_start), url.size());
    const auto at = url.substr(authority_start, authority_end - authority_start).rfind('@');
    if (at == std::string_view::npos) {
        return std::string(url);
    }
    std::string out(url.substr(0, authority_start));
    out += "***@";
    out += url.substr(authority_start + at + 1);
    return out;
}

std::string extract_filename_from_url(std::string_view path, std::string_view fallback) noexcept {
    const auto q_pos = path.find_first_of("?#");
    if (q_pos != std::string_view::npos) {
        path = path.substr(0, q_pos);
    }

    while (!path.empty() && path.back() == '/') {
        path.remove_suffix(1);
    }

    const auto last_slash = path.rfind('/');
    const std::string_view candidate = (last_slash != std::string_view::npos) ? path.substr(last_slash + 1) : path;

    if (candidate.empty()) {
        return std::string(fallback);
    }

    // Decode first so that an encoded separator ("%2F") cannot survive sanitizing.
    return sanitize_filename(percent_decode(candidate));
}

std::string filename_from_content_disposition(std::string_view header_value) noexcept {
    std::string plain;
    std::string extended;

    // Split on ';' outside of quoted strings.
    size_t pos = 0;
    while (pos <= header_value.size()) {
        size_t end = pos;
        bool quoted = false;
        while (end < header_value.size() && (quoted || header_value[end] != ';')) {
            if (header_value[end] == '"') quoted = !quoted;
            if (header_value[end] == '\\' && quoted && end + 1 < header_value.size()) ++end;
            ++end;
        }
        const std::string_view param = trim(header_value.substr(pos, end - pos));
        pos = end + 1;

        const auto eq = param.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view name = trim(param.substr(0, eq));
        std::string_view value = trim(param.substr(eq + 1));

        if (name.size() == 9 && starts_with_icase(name, "filename*")) {
            // RFC 5987: charset'language'percent-encoded-value
            const auto first = value.find('\'');
            const auto second = (first == std::string_view::npos) ? first : value.find('\'', first + 1);
            if (second != std::string_view::npos) {
                extended = percent_decode(value.substr(second + 1));
            }
        } else if (name.size() == 8 && starts_with_icase(name, "filename")) {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
                plain.clear();
                for (size_t i = 0; i < value.size(); ++i) {
                    if (value[i] == '\\' && i + 1 < value.size()) ++i;
                    plain.push_back(value[i]);
                }
            } else {
                plain = std::string(value);
            }
        }
    }

    const std::string& chosen = !extended.empty() ? extended : plain;
    if (chosen.empty()) {
        return {};
    }
    return sanitize_filename(chosen);
}

std::string numbered_filename(std::string_view filename, uint32_t number) noexcept {
    const size_t ext_pos = extension_start(filename);
    std::string out(filename.substr(0, ext_pos));
    out += '.';
    out += std::to_string(number);
    out += filename.substr(ext_pos);
    return out;
}

std::expected<Url, Error> parse_url(std::string_view raw_url) noexcept {
    raw_url = trim(raw_url);

    if (raw_url.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidUrl, "URL is empty"});
    }

    for (const char ch : raw_url) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte <= 32 || byte == 127) {
            return std::unexpected(Error{ErrorCode::InvalidUrl, "URL contains whitespace or control characters"});
        }
    }

    Url url;

    // 1. Scheme
    constexpr std::string_view scheme_sep = "://";
    const auto scheme_pos = raw_url.find(scheme_sep);
    if (scheme_pos == std::string_view::npos || scheme_pos == 0) {
        return std::unexpected(Error{ErrorCode::InvalidUrl, "Missing scheme: the URL must start with http:// or https://"});
    }

    std::string scheme_str(raw_url.substr(0, scheme_pos));
    std::ranges::transform(scheme_str, scheme_str.begin(), ascii_lower);

    if (scheme_str == "http") {
        url.port = 80;
    } else if (scheme_str == "https") {
        url.port = 443;
    } else {
        return std::unexpected(Error{ErrorCode::InvalidUrl, "Unsupported scheme '" + scheme_str + "': only http and https are supported"});
    }
    url.scheme = std::move(scheme_str);

    // 2. Authority: [userinfo@]host[:port], ended by the first '/', '?' or '#'
    std::string_view rest = raw_url.substr(scheme_pos + scheme_sep.size());
    const auto authority_end = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, authority_end);
    rest = (authority_end != std::string_view::npos) ? rest.substr(authority_end) : std::string_view{};

    const auto at_pos = authority.rfind('@');
    if (at_pos != std::string_view::npos) {
        url.userinfo = std::string(authority.substr(0, at_pos));
        authority = authority.substr(at_pos + 1);
    }

    std::string_view host = authority;
    std::string_view port_str;
    bool has_port = false;

    if (!authority.empty() && authority.front() == '[') {
        // IPv6 literal
        const auto bracket = authority.find(']');
        if (bracket == std::string_view::npos) {
            return std::unexpected(Error{ErrorCode::InvalidUrl, "Unterminated IPv6 address in URL"});
        }
        host = authority.substr(0, bracket + 1);
        const std::string_view after = authority.substr(bracket + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return std::unexpected(Error{ErrorCode::InvalidUrl, "Unexpected characters after IPv6 address in URL"});
            }
            port_str = after.substr(1);
            has_port = true;
        }
    } else {
        const auto colon_pos = authority.rfind(':');
        if (colon_pos != std::string_view::npos) {
            host = authority.substr(0, colon_pos);
            port_str = authority.substr(colon_pos + 1);
            has_port = true;
        }
    }

    if (host.empty() || host == "[]") {
        return std::unexpected(Error{ErrorCode::InvalidUrl, "URL has no host name"});
    }
    url.host = std::string(host);

    if (has_port && !port_str.empty()) {
        uint16_t parsed_port = 0;
        const auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), parsed_port);
        if (ec != std::errc{} || ptr != port_str.data() + port_str.size() || parsed_port == 0) {
            return std::unexpected(Error{ErrorCode::InvalidUrl, "Invalid port number '" + std::string(port_str) + "'"});
        }
        url.port = parsed_port;
    }

    // 3. Path, query; the fragment never reaches the server
    const auto fragment_pos = rest.find('#');
    if (fragment_pos != std::string_view::npos) {
        rest = rest.substr(0, fragment_pos);
    }

    const auto query_pos = rest.find('?');
    if (query_pos != std::string_view::npos) {
        url.path = std::string(rest.substr(0, query_pos));
        url.query = std::string(rest.substr(query_pos + 1));
    } else {
        url.path = std::string(rest);
    }

    if (url.path.empty()) {
        url.path = "/";
    }

    url.filename = extract_filename_from_url(url.path);

    return url;
}

} // namespace deoxdm::core
