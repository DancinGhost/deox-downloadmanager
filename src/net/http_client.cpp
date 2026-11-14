// SPDX-License-Identifier: MIT
#include "deoxdm/net/http_client.hpp"
#include "deoxdm/core/http_headers.hpp"
#include "deoxdm/core/url.hpp"
#include "curl_easy.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace deoxdm::net {

namespace {

using core::Error;
using core::ErrorCode;

constexpr uint32_t kMaxRedirects = 10;

constexpr bool is_redirect(uint32_t status) noexcept {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool is_strong_etag(std::string_view etag) noexcept {
    return !etag.empty() && !etag.starts_with("W/") && !etag.starts_with("w/");
}

void apply_common_options(CurlEasy& curl, const core::DownloadConfig& config) {
    curl.set_connect_timeout(config.timeout_seconds);
    curl.set_ssl_verify(!config.allow_insecure_tls);
    curl.set_fail_on_error(true);
    if (!config.user_agent.empty()) {
        curl.set_user_agent(config.user_agent);
    }
}

// Runs the request for config.url and follows redirects by hand.
//
// libcurl's own redirect handling sends every custom header to whichever host
// a redirect points at (it only holds back Authorization and Cookie), and lets
// an https download continue over plain http. Here the custom headers only go
// to the origin the user named, an https URL never leads to an http one, and a
// public host cannot redirect to a loopback or private-network address.
//
// The write callback sees the bodies of redirect responses too; it has to
// pass them over (see is_redirect()).
std::expected<void, Error> perform_following_redirects(
    CurlEasy& curl,
    const core::DownloadConfig& config,
    std::string_view protocol_header = {}
) {
    auto origin = core::parse_url(config.url);
    if (!origin) {
        return std::unexpected(origin.error());
    }

    std::string url = config.url;
    bool at_origin = true;

    for (uint32_t redirects = 0;; ++redirects) {
        curl.set_url(url);
        if (!config.headers.empty() || !protocol_header.empty()) {
            std::vector<std::string> headers;
            if (at_origin) {
                headers = config.headers;
            }
            if (!protocol_header.empty()) {
                headers.emplace_back(protocol_header);
            }
            curl.set_headers(headers);
        }

        if (auto result = curl.perform(); !result) {
            return result;
        }

        const uint32_t status = curl.response_code();
        std::string next = curl.redirect_url();
        if (!is_redirect(status) || next.empty()) {
            return {};
        }
        if (redirects >= kMaxRedirects) {
            return std::unexpected(Error{ErrorCode::HttpError,
                std::format("Too many redirects (more than {})", kMaxRedirects), status});
        }

        auto target = core::parse_url(next);
        if (!target) {
            return std::unexpected(Error{ErrorCode::InvalidUrl,
                std::format("Server redirected to an unusable address: {}", target.error().message), status});
        }
        if (origin->scheme == "https" && target->scheme != "https") {
            return std::unexpected(Error{ErrorCode::InvalidUrl,
                "Refusing to follow a redirect from https to unencrypted http", status});
        }
        if (!core::is_loopback_or_private_host(origin->host) && core::is_loopback_or_private_host(target->host)) {
            return std::unexpected(Error{ErrorCode::InvalidUrl,
                "Refusing to follow a redirect from a public host to a loopback or private address", status});
        }

        at_origin = core::same_origin(*origin, *target);
        url = std::move(next);
    }
}

struct HeaderProbe {
    core::ResponseHeaders headers;
    std::string effective_url;
};

// Performs a HEAD request, or a GET for the first byte only, and returns the
// headers of the final response.
std::expected<HeaderProbe, Error> fetch_headers(
    const core::DownloadConfig& config,
    bool ranged_get,
    const std::stop_token& stop_token
) {
    CurlEasy curl;
    apply_common_options(curl, config);
    curl.set_total_timeout(config.timeout_seconds);

    HeaderProbe probe;
    bool body_refused = false;

    curl.set_header_callback([&probe](std::string_view line) {
        probe.headers.feed(line);
    });
    curl.set_progress_callback([&stop_token] {
        return !stop_token.stop_requested();
    });

    if (ranged_get) {
        curl.set_range("0-0");
        curl.set_write_callback([&](std::span<const uint8_t> data) -> size_t {
            if (is_redirect(probe.headers.status)) {
                return data.size(); // the body of a redirect; the next request follows it
            }
            if (probe.headers.status == 206) {
                return data.size(); // the single requested byte
            }
            // The server is sending the whole file. The headers are all that
            // is needed, so stop here.
            body_refused = true;
            return 0;
        });
    } else {
        curl.set_nobody(true);
    }

    const auto result = perform_following_redirects(curl, config);
    if (stop_token.stop_requested()) {
        return std::unexpected(Error{ErrorCode::Cancelled, "Cancelled while contacting the server"});
    }
    if (!result && !body_refused) {
        return std::unexpected(result.error());
    }

    probe.effective_url = curl.effective_url();
    return probe;
}

} // namespace

std::expected<core::RemoteFileInfo, Error> HttpClient::probe(
    const core::DownloadConfig& config,
    std::stop_token stop_token
) {
    auto head = fetch_headers(config, false, stop_token);
    if (!head && head.error().code != ErrorCode::HttpError) {
        return std::unexpected(head.error()); // unreachable host, TLS failure, cancellation...
    }

    core::RemoteFileInfo info;
    std::string content_disposition;

    if (head) {
        info.http_status = head->headers.status;
        info.content_length = head->headers.content_length;
        info.accepts_ranges = head->headers.accepts_ranges;
        info.etag = head->headers.etag;
        info.last_modified = head->headers.last_modified;
        info.effective_url = head->effective_url;
        content_disposition = head->headers.content_disposition;
    }

    // HEAD is sometimes refused (405, 403 on signed URLs) or incomplete: many
    // servers honour ranges without advertising Accept-Ranges. Requesting the
    // first byte settles both questions with what the real download will see.
    const bool empty_file = head && info.content_length == uint64_t{0};
    const bool head_is_conclusive = head && info.content_length && info.accepts_ranges;

    if (!head_is_conclusive && !empty_file) {
        auto get = fetch_headers(config, true, stop_token);
        if (!get) {
            if (!head || get.error().code == ErrorCode::Cancelled) {
                return std::unexpected(get.error());
            }
            // HEAD worked but a ranged GET did not: download in one piece.
            info.accepts_ranges = false;
        } else {
            const auto& headers = get->headers;
            info.http_status = headers.status;
            info.effective_url = get->effective_url;

            if (headers.status == 206 && headers.content_range && headers.content_range->first_byte == 0) {
                info.accepts_ranges = true;
                if (headers.content_range->total_size) {
                    info.content_length = headers.content_range->total_size;
                }
            } else {
                info.accepts_ranges = false;
                if (headers.content_length) {
                    info.content_length = headers.content_length;
                }
            }

            if (!headers.etag.empty()) info.etag = headers.etag;
            if (!headers.last_modified.empty()) info.last_modified = headers.last_modified;
            if (!headers.content_disposition.empty()) content_disposition = headers.content_disposition;
        }
    }

    // Splitting needs a known size.
    if (!info.content_length) {
        info.accepts_ranges = false;
    }

    info.suggested_filename = core::filename_from_content_disposition(content_disposition);
    if (info.suggested_filename.empty()) {
        if (const auto final_url = core::parse_url(info.effective_url)) {
            info.suggested_filename = final_url->filename;
        } else if (const auto original_url = core::parse_url(config.url)) {
            info.suggested_filename = original_url->filename;
        }
    }

    return info;
}

std::expected<void, Error> HttpClient::download_chunk(
    const core::DownloadConfig& config,
    const ChunkRequest& request,
    const ChunkDataSink& sink,
    std::stop_token stop_token
) {
    if (stop_token.stop_requested()) {
        return std::unexpected(Error{ErrorCode::Cancelled, "Download cancelled"});
    }

    const bool bounded = !request.range.is_open_ended();
    const uint64_t chunk_length = request.range.length();

    if (bounded && request.already_downloaded >= chunk_length) {
        return {};
    }
    if (!request.send_range && request.already_downloaded != 0) {
        return std::unexpected(Error{ErrorCode::InternalError, "A download without range support must start from the beginning"});
    }

    const uint64_t fetch_start = request.range.start_byte + request.already_downloaded;
    const uint64_t expected_bytes = bounded ? (chunk_length - request.already_downloaded) : 0;
    const uint64_t max_bytes = request.max_bytes > 0 ? request.max_bytes : config.max_file_size;
    // A full "200 OK" body is only acceptable when the whole file was wanted anyway.
    const bool full_body_ok = request.whole_file && request.already_downloaded == 0;

    thread_local CurlEasy curl;
    curl.reset();
    apply_common_options(curl, config);
    curl.set_stall_timeout(config.timeout_seconds);

    std::string if_range_header;
    if (request.send_range) {
        curl.set_range(bounded
            ? std::format("{}-{}", fetch_start, request.range.end_byte)
            : std::format("{}-", fetch_start));
        if (is_strong_etag(request.expected_etag)) {
            if_range_header = std::format("If-Range: {}", request.expected_etag);
        } else if (request.expected_etag.empty() && !request.expected_last_modified.empty()) {
            if_range_header = std::format("If-Range: {}", request.expected_last_modified);
        }
    }

    core::ResponseHeaders headers;
    std::optional<Error> failure; // set by the write callback when it aborts
    bool response_checked = false;
    uint64_t received = 0;

    curl.set_header_callback([&headers](std::string_view line) {
        headers.feed(line);
    });

    curl.set_progress_callback([&stop_token] {
        return !stop_token.stop_requested();
    });

    curl.set_write_callback([&](std::span<const uint8_t> data) -> size_t {
        if (stop_token.stop_requested()) {
            failure = Error{ErrorCode::Cancelled, "Download cancelled"};
            return 0;
        }

        if (is_redirect(headers.status)) {
            return data.size(); // the body of a redirect; the next request follows it
        }

        if (!response_checked) {
            response_checked = true;
            const uint32_t status = headers.status;

            if (!request.expected_etag.empty() && !headers.etag.empty() &&
                headers.etag != request.expected_etag) {
                failure = Error{ErrorCode::InvalidArgument,
                    "Remote file changed during download (ETag differs)", status};
                return 0;
            }
            if (request.expected_etag.empty() && !request.expected_last_modified.empty() &&
                !headers.last_modified.empty() && headers.last_modified != request.expected_last_modified) {
                failure = Error{ErrorCode::InvalidArgument,
                    "Remote file changed during download (Last-Modified differs)", status};
                return 0;
            }

            if (status == 206) {
                if (!headers.content_range || headers.content_range->first_byte != fetch_start ||
                    (bounded && headers.content_range->last_byte != request.range.end_byte)) {
                    failure = Error{ErrorCode::RangeNotSupported, "Server answered with a different byte range than requested", status};
                    return 0;
                }
                if (request.expected_total_size && headers.content_range->total_size &&
                    *headers.content_range->total_size != *request.expected_total_size) {
                    failure = Error{ErrorCode::InvalidArgument,
                        "Remote file size changed during download", status};
                    return 0;
                }
            } else if (status == 200) {
                if (!full_body_ok) {
                    failure = Error{ErrorCode::RangeNotSupported, "Server ignored the range request and sent the whole file", status};
                    return 0;
                }
            } else {
                failure = Error{ErrorCode::HttpError, std::format("Unexpected HTTP status {}", status), status};
                return 0;
            }

            if (max_bytes > 0 && headers.content_length && fetch_start + *headers.content_length > max_bytes) {
                failure = Error{ErrorCode::InvalidArgument,
                    std::format("Remote file size ({} bytes) exceeds maximum allowed size ({} bytes)",
                                fetch_start + *headers.content_length, max_bytes),
                    status};
                return 0;
            }
        }

        if (max_bytes > 0 && (fetch_start + received >= max_bytes || data.size() > max_bytes - (fetch_start + received))) {
            failure = Error{ErrorCode::InvalidArgument,
                std::format("Download exceeded maximum allowed file size ({} bytes)", max_bytes)};
            return 0;
        }

        if (bounded && data.size() > expected_bytes - received) {
            failure = Error{ErrorCode::NetworkError, "Server sent more data than requested"};
            return 0;
        }

        if (auto written = sink(fetch_start + received, data); !written) {
            failure = std::move(written.error());
            return 0;
        }

        received += data.size();
        return data.size();
    });

    const auto result = perform_following_redirects(curl, config, if_range_header);

    if (failure) {
        return std::unexpected(std::move(*failure));
    }
    if (stop_token.stop_requested()) {
        return std::unexpected(Error{ErrorCode::Cancelled, "Download cancelled"});
    }
    if (!result) {
        return std::unexpected(result.error());
    }
    if (bounded && received != expected_bytes) {
        return std::unexpected(Error{
            ErrorCode::NetworkError,
            std::format("Connection closed early: received {} of {} bytes", received, expected_bytes)
        });
    }

    return {};
}

} // namespace deoxdm::net
