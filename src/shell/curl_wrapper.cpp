#include "deox/shell/curl_wrapper.hpp"
#include "deox/version.hpp"
#include <format>

namespace deox::shell {

CurlEasy::CurlEasy() noexcept {
    handle_ = abi::curl_easy_init();
    if (handle_) {
        // Standard sane defaults
        abi::curl_easy_setopt(handle_, abi::CURLOPT_NOSIGNAL, 1L);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_CONNECTTIMEOUT, 15L);
        const std::string default_ua = std::format("{}/{}", deox::PROJECT_NAME, deox::VERSION_STRING);
        (void)set_user_agent(default_ua);
    }
}

CurlEasy::~CurlEasy() noexcept {
    if (handle_) {
        abi::curl_easy_cleanup(handle_);
        handle_ = nullptr;
    }
}

CurlEasy::CurlEasy(CurlEasy&& other) noexcept
    : handle_(other.handle_),
      write_cb_(std::move(other.write_cb_)),
      header_cb_(std::move(other.header_cb_)),
      progress_cb_(std::move(other.progress_cb_)) {
    other.handle_ = nullptr;
}

CurlEasy& CurlEasy::operator=(CurlEasy&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            abi::curl_easy_cleanup(handle_);
        }
        handle_ = other.handle_;
        write_cb_ = std::move(other.write_cb_);
        header_cb_ = std::move(other.header_cb_);
        progress_cb_ = std::move(other.progress_cb_);
        other.handle_ = nullptr;
    }
    return *this;
}

void CurlEasy::reset() noexcept {
    if (handle_) {
        abi::curl_easy_reset(handle_);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_NOSIGNAL, 1L);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_CONNECTTIMEOUT, 15L);
    }
    write_cb_ = nullptr;
    header_cb_ = nullptr;
    progress_cb_ = nullptr;
}

core::DeoxError CurlEasy::set_url(const std::string& url) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    const int rc = abi::curl_easy_setopt(handle_, abi::CURLOPT_URL, url.c_str());
    if (rc != abi::CURLE_OK) {
        return core::DeoxError{core::ErrorCode::NetworkError, abi::curl_easy_strerror(rc)};
    }
    return {};
}

core::DeoxError CurlEasy::set_range(const core::ByteRange& range) {
    return set_range_str(std::format("{}-{}", range.start_byte, range.end_byte));
}

core::DeoxError CurlEasy::set_range_str(const std::string& range_str) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    const int rc = abi::curl_easy_setopt(handle_, abi::CURLOPT_RANGE, range_str.c_str());
    if (rc != abi::CURLE_OK) {
        return core::DeoxError{core::ErrorCode::NetworkError, abi::curl_easy_strerror(rc)};
    }
    return {};
}

core::DeoxError CurlEasy::set_nobody(bool nobody) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    const int rc = abi::curl_easy_setopt(handle_, abi::CURLOPT_NOBODY, nobody ? 1L : 0L);
    if (rc != abi::CURLE_OK) {
        return core::DeoxError{core::ErrorCode::NetworkError, abi::curl_easy_strerror(rc)};
    }
    return {};
}

core::DeoxError CurlEasy::set_timeout(uint32_t seconds) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    const int rc = abi::curl_easy_setopt(handle_, abi::CURLOPT_TIMEOUT, static_cast<long>(seconds));
    if (rc != abi::CURLE_OK) {
        return core::DeoxError{core::ErrorCode::NetworkError, abi::curl_easy_strerror(rc)};
    }
    return {};
}

core::DeoxError CurlEasy::set_follow_redirects(bool follow, uint32_t max_redirects) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    abi::curl_easy_setopt(handle_, abi::CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);
    abi::curl_easy_setopt(handle_, abi::CURLOPT_MAXREDIRS, static_cast<long>(max_redirects));
    return {};
}

core::DeoxError CurlEasy::set_ssl_verify(bool verify) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    abi::curl_easy_setopt(handle_, abi::CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    abi::curl_easy_setopt(handle_, abi::CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
    return {};
}

core::DeoxError CurlEasy::set_user_agent(const std::string& user_agent) {
    if (!handle_) return core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"};
    abi::curl_easy_setopt(handle_, abi::CURLOPT_USERAGENT, user_agent.c_str());
    return {};
}

size_t CurlEasy::static_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept {
    auto* self = static_cast<CurlEasy*>(userdata);
    const size_t bytes = size * nmemb;
    if (self && self->write_cb_) {
        return self->write_cb_(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(ptr), bytes));
    }
    return bytes;
}

size_t CurlEasy::static_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) noexcept {
    auto* self = static_cast<CurlEasy*>(userdata);
    const size_t bytes = size * nitems;
    if (self && self->header_cb_) {
        self->header_cb_(std::string_view(buffer, bytes));
    }
    return bytes;
}

int CurlEasy::static_xferinfo_cb(void* clientp, int64_t dltotal, int64_t dlnow, int64_t, int64_t) noexcept {
    auto* self = static_cast<CurlEasy*>(clientp);
    if (self && self->progress_cb_) {
        const bool keep_running = self->progress_cb_(
            static_cast<uint64_t>(dltotal > 0 ? dltotal : 0),
            static_cast<uint64_t>(dlnow > 0 ? dlnow : 0)
        );
        if (!keep_running) {
            return 1; // Aborts curl transfer with CURLE_ABORTED_BY_CALLBACK
        }
    }
    return 0;
}

void CurlEasy::set_write_callback(WriteCallback cb) {
    write_cb_ = std::move(cb);
    if (write_cb_) {
        abi::curl_easy_setopt(handle_, abi::CURLOPT_WRITEFUNCTION, &CurlEasy::static_write_cb);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_WRITEDATA, this);
    } else {
        abi::curl_easy_setopt(handle_, abi::CURLOPT_WRITEFUNCTION, nullptr);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_WRITEDATA, nullptr);
    }
}

void CurlEasy::set_header_callback(HeaderCallback cb) {
    header_cb_ = std::move(cb);
    if (header_cb_) {
        abi::curl_easy_setopt(handle_, abi::CURLOPT_HEADERFUNCTION, &CurlEasy::static_header_cb);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_HEADERDATA, this);
    } else {
        abi::curl_easy_setopt(handle_, abi::CURLOPT_HEADERFUNCTION, nullptr);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_HEADERDATA, nullptr);
    }
}

void CurlEasy::set_progress_callback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
    if (progress_cb_) {
        abi::curl_easy_setopt(handle_, abi::CURLOPT_NOPROGRESS, 0L);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_XFERINFOFUNCTION, &CurlEasy::static_xferinfo_cb);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_XFERINFODATA, this);
    } else {
        abi::curl_easy_setopt(handle_, abi::CURLOPT_NOPROGRESS, 1L);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_XFERINFOFUNCTION, nullptr);
        abi::curl_easy_setopt(handle_, abi::CURLOPT_XFERINFODATA, nullptr);
    }
}

std::expected<void, core::DeoxError> CurlEasy::perform() {
    if (!handle_) {
        return std::unexpected(core::DeoxError{core::ErrorCode::NetworkError, "Curl handle not initialized"});
    }

    const int rc = abi::curl_easy_perform(handle_);
    const uint32_t status_code = get_response_code();

    if (rc == abi::CURLE_ABORTED_BY_CALLBACK) {
        return std::unexpected(core::DeoxError{core::ErrorCode::Cancelled, "Download operation aborted", status_code});
    }

    if (rc != abi::CURLE_OK) {
        return std::unexpected(core::DeoxError{
            core::ErrorCode::NetworkError,
            std::format("Network transfer failed: {} (code {})", abi::curl_easy_strerror(rc), rc),
            status_code
        });
    }

    if (status_code >= 400) {
        return std::unexpected(core::DeoxError{
            core::ErrorCode::HttpError,
            std::format("HTTP error response received: status code {}", status_code),
            status_code
        });
    }

    return {};
}

uint32_t CurlEasy::get_response_code() const noexcept {
    if (!handle_) return 0;
    long code = 0;
    abi::curl_easy_getinfo(handle_, abi::CURLINFO_RESPONSE_CODE, &code);
    return static_cast<uint32_t>(code);
}

int64_t CurlEasy::get_content_length() const noexcept {
    if (!handle_) return -1;
    int64_t cl = -1;
    abi::curl_easy_getinfo(handle_, abi::CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    return cl;
}

std::string CurlEasy::get_effective_url() const {
    if (!handle_) return "";
    char* eff_url = nullptr;
    abi::curl_easy_getinfo(handle_, abi::CURLINFO_EFFECTIVE_URL, &eff_url);
    return eff_url ? std::string(eff_url) : "";
}

} // namespace deox::shell
