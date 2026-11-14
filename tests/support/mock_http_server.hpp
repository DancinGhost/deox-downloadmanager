// SPDX-License-Identifier: MIT
#pragma once

// A small HTTP/1.1 server on the loopback interface for integration tests.
// It serves one payload and can be told to misbehave in the ways real servers
// do: no HEAD, no ranges, no Content-Length, slow or dropped connections.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace deoxdm::test {

struct MockServerOptions {
    std::vector<uint8_t> payload;
    bool allow_head{true};           // false: HEAD is answered with 405
    bool support_ranges{true};       // false: Range headers are ignored (200 + full body)
    bool advertise_ranges{true};     // send "Accept-Ranges: bytes"
    bool send_content_length{true};  // false: the body ends when the connection closes
    std::string etag{"\"mock-v1\""};
    std::string content_disposition; // sent as-is when not empty
    std::string redirect_location{"/files/redirected.bin"}; // where /redirect points

    // Slow the body down: send `throttle_bytes` at a time, pausing in between.
    size_t throttle_bytes{0};
    std::chrono::milliseconds throttle_pause{0};

    // Cut the first `drop_connections` body transfers after `drop_after` bytes.
    int drop_connections{0};
    size_t drop_after{0};
};

struct RecordedRequest {
    std::string method;
    std::string path;
    std::optional<uint64_t> range_start;
    std::string raw; // request line and headers
};

// Paths:  /missing...         -> 404
//         /redirect           -> 302 to options.redirect_location
//         anything else       -> the payload
class MockHttpServer {
public:
    explicit MockHttpServer(MockServerOptions options);
    ~MockHttpServer();

    MockHttpServer(const MockHttpServer&) = delete;
    MockHttpServer& operator=(const MockHttpServer&) = delete;

    [[nodiscard]] std::string url(std::string_view path = "/file.bin") const;
    [[nodiscard]] std::vector<RecordedRequest> requests() const;
    [[nodiscard]] size_t request_count(std::string_view method) const;

    // Replaces the validator the server reports, as if the file had changed.
    void set_etag(std::string etag);

    // Lifts the throttle for all following requests.
    void stop_throttling();

private:
    void accept_loop();
    void handle_client(int fd);

    MockServerOptions options_;
    int listen_fd_{-1};
    uint16_t port_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> throttling_disabled_{false};
    std::atomic<int> drops_left_{0};
    std::thread accept_thread_;

    mutable std::mutex mutex_;
    std::vector<RecordedRequest> requests_;
    std::vector<int> client_fds_;
    std::vector<std::thread> client_threads_;
};

// Deterministic, non-repeating test data.
[[nodiscard]] std::vector<uint8_t> make_payload(size_t size);

} // namespace deoxdm::test
