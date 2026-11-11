#include "deox/core/url.hpp"
#include <algorithm>
#include <charconv>

namespace deox::core {

std::string Url::full_url() const {
    std::string res = scheme + "://" + host;
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

std::string sanitize_filename(std::string_view filename) noexcept {
    if (filename.empty()) {
        return "download.bin";
    }

    // Strip directory components if any
    const auto last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string_view::npos) {
        filename = filename.substr(last_slash + 1);
    }

    std::string sanitized;
    sanitized.reserve(filename.size());

    for (const char ch : filename) {
        // Disallow dangerous characters, control characters, nulls
        if (ch >= 32 && ch <= 126 && ch != '/' && ch != '\\' && ch != ':' && 
            ch != '*' && ch != '?' && ch != '"' && ch != '<' && ch != '>' && ch != '|') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }

    // Avoid '.' or '..' or empty
    if (sanitized.empty() || sanitized == "." || sanitized == "..") {
        return "download.bin";
    }

    return sanitized;
}

std::string extract_filename_from_url(std::string_view path, std::string_view fallback) noexcept {
    // Strip query or fragment if present in raw path
    const auto q_pos = path.find_first_of("?#");
    if (q_pos != std::string_view::npos) {
        path = path.substr(0, q_pos);
    }

    // Strip trailing slashes
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
        path.remove_suffix(1);
    }

    const auto last_slash = path.find_last_of("/\\");
    std::string_view candidate = (last_slash != std::string_view::npos) ? path.substr(last_slash + 1) : path;

    if (candidate.empty()) {
        return std::string(fallback);
    }

    return sanitize_filename(candidate);
}

std::expected<Url, DeoxError> parse_url(std::string_view raw_url) noexcept {
    // Trim leading and trailing whitespace
    while (!raw_url.empty() && (raw_url.front() == ' ' || raw_url.front() == '\t')) {
        raw_url.remove_prefix(1);
    }
    while (!raw_url.empty() && (raw_url.back() == ' ' || raw_url.back() == '\t' || raw_url.back() == '\r' || raw_url.back() == '\n')) {
        raw_url.remove_suffix(1);
    }

    if (raw_url.empty()) {
        return std::unexpected(DeoxError{ErrorCode::InvalidUrl, "URL string is empty"});
    }

    Url url;

    // 1. Parse Scheme
    constexpr std::string_view scheme_sep = "://";
    const auto scheme_pos = raw_url.find(scheme_sep);
    if (scheme_pos == std::string_view::npos) {
        return std::unexpected(DeoxError{ErrorCode::InvalidUrl, "Missing protocol scheme (e.g. 'http://' or 'https://')"});
    }

    std::string scheme_str;
    scheme_str.resize(scheme_pos);
    std::transform(raw_url.begin(), raw_url.begin() + static_cast<ptrdiff_t>(scheme_pos), scheme_str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (scheme_str == "http") {
        url.scheme = "http";
        url.port = 80;
    } else if (scheme_str == "https") {
        url.scheme = "https";
        url.port = 443;
    } else {
        return std::unexpected(DeoxError{ErrorCode::InvalidUrl, "Unsupported protocol: '" + scheme_str + "'. Only HTTP and HTTPS are supported."});
    }

    std::string_view rest = raw_url.substr(scheme_pos + scheme_sep.size());
    if (rest.empty()) {
        return std::unexpected(DeoxError{ErrorCode::InvalidUrl, "Missing host in URL"});
    }

    // 2. Parse Host and optional Port
    const auto path_pos = rest.find('/');
    std::string_view host_port = (path_pos != std::string_view::npos) ? rest.substr(0, path_pos) : rest;
    std::string_view path_and_query = (path_pos != std::string_view::npos) ? rest.substr(path_pos) : "/";

    if (host_port.empty()) {
        return std::unexpected(DeoxError{ErrorCode::InvalidUrl, "Empty host name"});
    }

    const auto colon_pos = host_port.find(':');
    if (colon_pos != std::string_view::npos) {
        url.host = std::string(host_port.substr(0, colon_pos));
        std::string_view port_str = host_port.substr(colon_pos + 1);
        uint16_t parsed_port = 0;
        const auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), parsed_port);
        if (ec != std::errc{} || ptr != port_str.data() + port_str.size() || parsed_port == 0) {
            return std::unexpected(DeoxError{ErrorCode::InvalidUrl, "Invalid port number: '" + std::string(port_str) + "'"});
        }
        url.port = parsed_port;
    } else {
        url.host = std::string(host_port);
    }

    // 3. Parse Path and Query
    const auto query_pos = path_and_query.find('?');
    if (query_pos != std::string_view::npos) {
        url.path = std::string(path_and_query.substr(0, query_pos));
        url.query = std::string(path_and_query.substr(query_pos + 1));
    } else {
        url.path = std::string(path_and_query);
    }

    if (url.path.empty()) {
        url.path = "/";
    }

    url.filename = extract_filename_from_url(url.path);

    return url;
}

} // namespace deox::core
