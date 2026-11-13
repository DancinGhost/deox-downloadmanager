// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"

#include <array>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deoxdm::net {

// Return the number of bytes consumed; anything else aborts the transfer.
using WriteCallback = std::function<size_t(std::span<const uint8_t>)>;
// Called once per response header line, including the status line.
using HeaderCallback = std::function<void(std::string_view)>;
// Called regularly during a transfer; return false to abort it.
using ProgressCallback = std::function<bool()>;

// RAII wrapper around one libcurl easy handle. The handle stores a pointer to
// this object, so the wrapper can be neither copied nor moved.
class CurlEasy {
public:
    CurlEasy();
    ~CurlEasy();

    CurlEasy(const CurlEasy&) = delete;
    CurlEasy& operator=(const CurlEasy&) = delete;

    [[nodiscard]] bool is_valid() const noexcept {
        return handle_ != nullptr;
    }

    void set_url(const std::string& url);
    void set_range(const std::string& range_spec); // "0-499" or "500-"
    void set_nobody(bool nobody);                  // true: HEAD request
    void set_connect_timeout(uint32_t seconds);    // 0 = libcurl default
    void set_total_timeout(uint32_t seconds);      // 0 = no limit
    void set_stall_timeout(uint32_t seconds);      // abort when no data arrives for this long; 0 = never
    void set_ssl_verify(bool verify);
    void set_user_agent(const std::string& user_agent);
    void set_headers(const std::vector<std::string>& headers);
    void set_fail_on_error(bool fail);             // treat HTTP >= 400 as an error before any body is delivered

    void set_write_callback(WriteCallback cb);
    void set_header_callback(HeaderCallback cb);
    void set_progress_callback(ProgressCallback cb);

    // Runs the transfer. CURLE_WRITE_ERROR (the write callback refused data)
    // is reported as InternalError; callers that abort on purpose know why.
    [[nodiscard]] std::expected<void, core::Error> perform();

    [[nodiscard]] uint32_t response_code() const noexcept;
    [[nodiscard]] std::optional<uint64_t> content_length() const noexcept;
    [[nodiscard]] std::string effective_url() const;
    // Where a 3xx response points, resolved against the request URL; empty
    // when the last response was not a redirect. Redirects are never
    // followed automatically.
    [[nodiscard]] std::string redirect_url() const;

private:
    // Returns whether libcurl accepted the option. A failed non-optional
    // option is remembered and reported by perform().
    template <typename T>
    bool setopt(int option, T value, bool optional = false) noexcept;

    static size_t static_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept;
    static size_t static_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) noexcept;
    static int static_xferinfo_cb(void* clientp, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow) noexcept;

    void* handle_{nullptr};
    void* header_list_{nullptr};
    int setopt_error_{0};
    std::array<char, 256> error_buffer_{};
    WriteCallback write_cb_;
    HeaderCallback header_cb_;
    ProgressCallback progress_cb_;
};

} // namespace deoxdm::net
