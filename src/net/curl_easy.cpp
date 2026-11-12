// SPDX-License-Identifier: MIT
#include "curl_easy.hpp"
#include "curl_abi.hpp"
#include "deoxdm/version.hpp"

#include <cstdlib>
#include <format>
#include <mutex>

namespace deoxdm::net {

namespace {

// libcurl wants one global initialization before any other thread uses it.
void ensure_global_init() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (abi::curl_global_init(abi::CURL_GLOBAL_DEFAULT) == abi::CURLE_OK) {
            std::atexit(abi::curl_global_cleanup);
        }
    });
}

core::ErrorCode classify(abi::CURLcode rc) noexcept {
    switch (rc) {
        case abi::CURLE_UNSUPPORTED_PROTOCOL:
        case abi::CURLE_URL_MALFORMAT:
            return core::ErrorCode::InvalidUrl;
        case abi::CURLE_HTTP_RETURNED_ERROR:
            return core::ErrorCode::HttpError;
        case abi::CURLE_WRITE_ERROR:
            return core::ErrorCode::InternalError;
        case abi::CURLE_RANGE_ERROR:
            return core::ErrorCode::RangeNotSupported;
        case abi::CURLE_ABORTED_BY_CALLBACK:
            return core::ErrorCode::Cancelled;
        case abi::CURLE_PEER_FAILED_VERIFICATION:
        case abi::CURLE_SSL_CACERT_BADFILE:
            return core::ErrorCode::TlsError;
        default:
            return core::ErrorCode::NetworkError;
    }
}

} // namespace

CurlEasy::CurlEasy() {
    ensure_global_init();
    handle_ = abi::curl_easy_init();
    if (!handle_) {
        return;
    }

    setopt(abi::CURLOPT_NOSIGNAL, 1L); // required for multi-threaded use
    setopt(abi::CURLOPT_ERRORBUFFER, error_buffer_.data());
    setopt(abi::CURLOPT_BUFFERSIZE, 128L * 1024L);
    // Never let a redirect leave HTTP(S). Older libcurl versions lack these
    // options; their defaults are acceptable.
    setopt(abi::CURLOPT_PROTOCOLS_STR, "http,https", true);
    setopt(abi::CURLOPT_REDIR_PROTOCOLS_STR, "http,https", true);
    set_user_agent(std::format("{}/{}", USER_AGENT_PRODUCT, VERSION_STRING));
}

CurlEasy::~CurlEasy() {
    if (handle_) {
        abi::curl_easy_cleanup(handle_);
    }
    if (header_list_) {
        abi::curl_slist_free_all(static_cast<abi::curl_slist*>(header_list_));
    }
}

template <typename T>
void CurlEasy::setopt(int option, T value, bool optional) noexcept {
    if (!handle_) return;
    const abi::CURLcode rc = abi::curl_easy_setopt(handle_, option, value);
    if (rc != abi::CURLE_OK && !optional && setopt_error_ == 0) {
        setopt_error_ = rc;
    }
}

void CurlEasy::set_url(const std::string& url) {
    setopt(abi::CURLOPT_URL, url.c_str());
}

void CurlEasy::set_range(const std::string& range_spec) {
    setopt(abi::CURLOPT_RANGE, range_spec.c_str());
}

void CurlEasy::set_nobody(bool nobody) {
    setopt(abi::CURLOPT_NOBODY, nobody ? 1L : 0L);
}

void CurlEasy::set_follow_redirects(uint32_t max_redirects) {
    setopt(abi::CURLOPT_FOLLOWLOCATION, max_redirects > 0 ? 1L : 0L);
    setopt(abi::CURLOPT_MAXREDIRS, static_cast<long>(max_redirects));
}

void CurlEasy::set_connect_timeout(uint32_t seconds) {
    setopt(abi::CURLOPT_CONNECTTIMEOUT, static_cast<long>(seconds));
}

void CurlEasy::set_total_timeout(uint32_t seconds) {
    setopt(abi::CURLOPT_TIMEOUT, static_cast<long>(seconds));
}

void CurlEasy::set_stall_timeout(uint32_t seconds) {
    // "Slower than 1 byte/second for N seconds" means the transfer is stuck.
    setopt(abi::CURLOPT_LOW_SPEED_LIMIT, seconds > 0 ? 1L : 0L);
    setopt(abi::CURLOPT_LOW_SPEED_TIME, static_cast<long>(seconds));
}

void CurlEasy::set_ssl_verify(bool verify) {
    setopt(abi::CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    setopt(abi::CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
}

void CurlEasy::set_user_agent(const std::string& user_agent) {
    setopt(abi::CURLOPT_USERAGENT, user_agent.c_str());
}

void CurlEasy::set_headers(const std::vector<std::string>& headers) {
    abi::curl_slist* list = nullptr;
    for (const auto& header : headers) {
        if (abi::curl_slist* appended = abi::curl_slist_append(list, header.c_str())) {
            list = appended;
        }
    }
    setopt(abi::CURLOPT_HTTPHEADER, list);
    if (header_list_) {
        abi::curl_slist_free_all(static_cast<abi::curl_slist*>(header_list_));
    }
    header_list_ = list;
}

void CurlEasy::set_fail_on_error(bool fail) {
    setopt(abi::CURLOPT_FAILONERROR, fail ? 1L : 0L);
}

size_t CurlEasy::static_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept {
    auto* self = static_cast<CurlEasy*>(userdata);
    const size_t bytes = size * nmemb;
    if (bytes == 0) {
        return 0;
    }
    try {
        return self->write_cb_(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(ptr), bytes));
    } catch (...) {
        return 0; // a short count makes libcurl abort with CURLE_WRITE_ERROR
    }
}

size_t CurlEasy::static_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) noexcept {
    auto* self = static_cast<CurlEasy*>(userdata);
    const size_t bytes = size * nitems;
    try {
        self->header_cb_(std::string_view(buffer, bytes));
    } catch (...) {
        return 0;
    }
    return bytes;
}

int CurlEasy::static_xferinfo_cb(void* clientp, int64_t, int64_t, int64_t, int64_t) noexcept {
    auto* self = static_cast<CurlEasy*>(clientp);
    try {
        return self->progress_cb_() ? 0 : 1; // non-zero aborts with CURLE_ABORTED_BY_CALLBACK
    } catch (...) {
        return 1;
    }
}

void CurlEasy::set_write_callback(WriteCallback cb) {
    write_cb_ = std::move(cb);
    setopt(abi::CURLOPT_WRITEFUNCTION, &CurlEasy::static_write_cb);
    setopt(abi::CURLOPT_WRITEDATA, static_cast<void*>(this));
}

void CurlEasy::set_header_callback(HeaderCallback cb) {
    header_cb_ = std::move(cb);
    setopt(abi::CURLOPT_HEADERFUNCTION, &CurlEasy::static_header_cb);
    setopt(abi::CURLOPT_HEADERDATA, static_cast<void*>(this));
}

void CurlEasy::set_progress_callback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
    setopt(abi::CURLOPT_XFERINFOFUNCTION, &CurlEasy::static_xferinfo_cb);
    setopt(abi::CURLOPT_XFERINFODATA, static_cast<void*>(this));
    setopt(abi::CURLOPT_NOPROGRESS, 0L);
}

std::expected<void, core::Error> CurlEasy::perform() {
    if (!handle_) {
        return std::unexpected(core::Error{core::ErrorCode::NetworkError, "Failed to initialize libcurl"});
    }
    if (setopt_error_ != 0) {
        return std::unexpected(core::Error{
            core::ErrorCode::InternalError,
            std::format("Failed to configure the request: {}", abi::curl_easy_strerror(setopt_error_))
        });
    }

    error_buffer_[0] = '\0';
    const abi::CURLcode rc = abi::curl_easy_perform(handle_);
    if (rc == abi::CURLE_OK) {
        return {};
    }

    const uint32_t status = response_code();
    const core::ErrorCode code = classify(rc);

    std::string message;
    if (code == core::ErrorCode::Cancelled) {
        message = "Transfer cancelled";
    } else if (code == core::ErrorCode::HttpError) {
        message = std::format("Server responded with HTTP {}", status);
    } else {
        const std::string_view detail = error_buffer_[0] != '\0'
            ? std::string_view(error_buffer_.data())
            : std::string_view(abi::curl_easy_strerror(rc));
        message = std::string(detail);
    }

    return std::unexpected(core::Error{code, std::move(message), status});
}

uint32_t CurlEasy::response_code() const noexcept {
    if (!handle_) return 0;
    long code = 0;
    abi::curl_easy_getinfo(handle_, abi::CURLINFO_RESPONSE_CODE, &code);
    return code > 0 ? static_cast<uint32_t>(code) : 0;
}

std::optional<uint64_t> CurlEasy::content_length() const noexcept {
    if (!handle_) return std::nullopt;
    abi::curl_off_t length = -1;
    abi::curl_easy_getinfo(handle_, abi::CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    if (length < 0) return std::nullopt;
    return static_cast<uint64_t>(length);
}

std::string CurlEasy::effective_url() const {
    if (!handle_) return {};
    char* url = nullptr;
    abi::curl_easy_getinfo(handle_, abi::CURLINFO_EFFECTIVE_URL, &url);
    return url ? std::string(url) : std::string{};
}

} // namespace deoxdm::net
