#include "deox/shell/network_client.hpp"
#include "deox/core/url.hpp"
#include <charconv>
#include <algorithm>
#include <format>

namespace deox::shell {

namespace {

std::string_view trim_sv(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string extract_disposition_filename(std::string_view val) noexcept {
    constexpr std::string_view key = "filename=";
    const auto pos = val.find(key);
    if (pos == std::string_view::npos) return "";

    std::string_view fn = val.substr(pos + key.size());
    fn = trim_sv(fn);
    if (fn.starts_with('"')) {
        fn.remove_prefix(1);
        const auto quote_end = fn.find('"');
        if (quote_end != std::string_view::npos) {
            fn = fn.substr(0, quote_end);
        }
    } else {
        const auto semi = fn.find(';');
        if (semi != std::string_view::npos) {
            fn = fn.substr(0, semi);
        }
    }

    return core::sanitize_filename(fn);
}

} // namespace

std::expected<core::RemoteFileInfo, core::DeoxError> NetworkClient::probe(
    const std::string& url,
    const core::DownloadConfig& config
) noexcept {
    CurlEasy curl;
    if (!curl.is_valid()) {
        return std::unexpected(core::DeoxError{core::ErrorCode::NetworkError, "Failed to initialize Curl"});
    }

    (void)curl.set_url(url);
    (void)curl.set_nobody(true); // HTTP HEAD
    (void)curl.set_follow_redirects(true);
    (void)curl.set_timeout(config.timeout_seconds);
    (void)curl.set_ssl_verify(!config.allow_insecure_tls);

    core::RemoteFileInfo info;

    curl.set_header_callback([&info](std::string_view line) {
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) return;

        const std::string_view key = trim_sv(line.substr(0, colon));
        const std::string_view val = trim_sv(line.substr(colon + 1));

        if (iequals(key, "Content-Length")) {
            uint64_t len = 0;
            const auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), len);
            if (ec == std::errc{} && len > 0) {
                info.content_length = len;
            }
        } else if (iequals(key, "Accept-Ranges")) {
            if (val.find("bytes") != std::string_view::npos) {
                info.accepts_ranges = true;
            }
        } else if (iequals(key, "ETag")) {
            info.etag = std::string(val);
        } else if (iequals(key, "Last-Modified")) {
            info.last_modified = std::string(val);
        } else if (iequals(key, "Content-Disposition")) {
            const std::string fn = extract_disposition_filename(val);
            if (!fn.empty()) {
                info.suggested_filename = fn;
            }
        }
    });

    auto res = curl.perform();
    info.http_status = curl.get_response_code();

    // If HEAD method is rejected (e.g. 405 Method Not Allowed), retry with Range GET 0-0
    if (!res && (info.http_status == 405 || info.http_status == 501)) {
        curl.reset();
        (void)curl.set_url(url);
        (void)curl.set_range_str("0-0");
        (void)curl.set_follow_redirects(true);
        (void)curl.set_timeout(config.timeout_seconds);
        (void)curl.set_ssl_verify(!config.allow_insecure_tls);

        curl.set_header_callback([&info](std::string_view line) {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos) return;

            const std::string_view key = trim_sv(line.substr(0, colon));
            const std::string_view val = trim_sv(line.substr(colon + 1));

            if (iequals(key, "Content-Range")) {
                info.accepts_ranges = true;
                const auto slash = val.find('/');
                if (slash != std::string_view::npos) {
                    std::string_view total_str = trim_sv(val.substr(slash + 1));
                    uint64_t total = 0;
                    const auto [ptr, ec] = std::from_chars(total_str.data(), total_str.data() + total_str.size(), total);
                    if (ec == std::errc{}) {
                        info.content_length = total;
                    }
                }
            } else if (iequals(key, "ETag")) {
                info.etag = std::string(val);
            }
        });

        // Drain write data (we only requested 1 byte anyway)
        curl.set_write_callback([](std::span<const uint8_t> data) {
            return data.size();
        });

        res = curl.perform();
        info.http_status = curl.get_response_code();
    }

    if (!res) {
        return std::unexpected(res.error());
    }

    if (info.content_length == 0) {
        const int64_t cl = curl.get_content_length();
        if (cl > 0) {
            info.content_length = static_cast<uint64_t>(cl);
        }
    }

    if (info.suggested_filename.empty()) {
        const std::string eff_url = curl.get_effective_url();
        if (!eff_url.empty()) {
            if (auto parsed = core::parse_url(eff_url)) {
                info.suggested_filename = parsed->filename;
            }
        }
    }

    return info;
}

std::expected<void, core::DeoxError> NetworkClient::download_chunk(
    const std::string& url,
    uint32_t chunk_idx,
    const core::ByteRange& range,
    uint64_t already_downloaded,
    ChunkDataSink data_sink,
    ChunkProgressNotifier progress_notifier,
    std::stop_token stop_token,
    const core::DownloadConfig& config
) noexcept {
    if (stop_token.stop_requested()) {
        return std::unexpected(core::DeoxError{core::ErrorCode::Cancelled, "Download cancelled before chunk start"});
    }

    const uint64_t chunk_len = range.length();
    if (already_downloaded >= chunk_len) {
        return {}; // Chunk is already fully downloaded
    }

    CurlEasy curl;
    if (!curl.is_valid()) {
        return std::unexpected(core::DeoxError{core::ErrorCode::NetworkError, "Failed to initialize Curl"});
    }

    (void)curl.set_url(url);
    (void)curl.set_follow_redirects(true);
    (void)curl.set_timeout(config.timeout_seconds);
    (void)curl.set_ssl_verify(!config.allow_insecure_tls);

    // Compute resumed byte range
    const uint64_t fetch_start = range.start_byte + already_downloaded;
    const core::ByteRange fetch_range{fetch_start, range.end_byte};
    (void)curl.set_range(fetch_range);

    uint64_t current_offset = fetch_start;
    std::optional<core::DeoxError> sink_error;

    curl.set_write_callback([&](std::span<const uint8_t> data) -> size_t {
        if (stop_token.stop_requested()) {
            return 0; // Aborts curl transfer
        }

        const auto write_res = data_sink(current_offset, data);
        if (!write_res) {
            sink_error = write_res.error();
            return 0; // Aborts curl transfer
        }

        current_offset += data.size();
        if (progress_notifier) {
            progress_notifier(chunk_idx, data.size());
        }

        return data.size();
    });

    curl.set_progress_callback([&stop_token](uint64_t, uint64_t) -> bool {
        return !stop_token.stop_requested();
    });

    auto res = curl.perform();

    if (sink_error) {
        return std::unexpected(*sink_error);
    }

    if (stop_token.stop_requested()) {
        return std::unexpected(core::DeoxError{core::ErrorCode::Cancelled, "Download cancelled"});
    }

    if (!res) {
        return std::unexpected(res.error());
    }

    return {};
}

} // namespace deox::shell
