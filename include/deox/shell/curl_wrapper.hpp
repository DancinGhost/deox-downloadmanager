#pragma once

#include "deox/core/types.hpp"
#include "deox/shell/curl_abi.hpp"
#include <span>
#include <string>
#include <string_view>
#include <functional>
#include <stop_token>
#include <expected>

namespace deox::shell {

using WriteCallback = std::function<size_t(std::span<const uint8_t>)>;
using HeaderCallback = std::function<void(std::string_view)>;
using ProgressCallback = std::function<bool(uint64_t dltotal, uint64_t dlnow)>; // return false to abort

class CurlEasy {
public:
    CurlEasy() noexcept;
    ~CurlEasy() noexcept;

    // Move-only semantics (RAII)
    CurlEasy(const CurlEasy&) = delete;
    CurlEasy& operator=(const CurlEasy&) = delete;
    CurlEasy(CurlEasy&& other) noexcept;
    CurlEasy& operator=(CurlEasy&& other) noexcept;

    [[nodiscard]] bool is_valid() const noexcept {
        return handle_ != nullptr;
    }

    void reset() noexcept;

    core::DeoxError set_url(const std::string& url);
    core::DeoxError set_range(const core::ByteRange& range);
    core::DeoxError set_range_str(const std::string& range_str);
    core::DeoxError set_nobody(bool nobody);
    core::DeoxError set_timeout(uint32_t seconds);
    core::DeoxError set_follow_redirects(bool follow, uint32_t max_redirects = 10);
    core::DeoxError set_ssl_verify(bool verify);
    core::DeoxError set_user_agent(const std::string& user_agent);

    void set_write_callback(WriteCallback cb);
    void set_header_callback(HeaderCallback cb);
    void set_progress_callback(ProgressCallback cb);

    [[nodiscard]] std::expected<void, core::DeoxError> perform();

    [[nodiscard]] uint32_t get_response_code() const noexcept;
    [[nodiscard]] int64_t get_content_length() const noexcept;
    [[nodiscard]] std::string get_effective_url() const;

private:
    static size_t static_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept;
    static size_t static_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) noexcept;
    static int static_xferinfo_cb(void* clientp, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow) noexcept;

    abi::CURL* handle_{nullptr};
    WriteCallback write_cb_;
    HeaderCallback header_cb_;
    ProgressCallback progress_cb_;
};

} // namespace deox::shell
