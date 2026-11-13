// SPDX-License-Identifier: MIT
#pragma once

// A small HTTP/1.1 server on the loopback interface for integration tests.
// It serves one payload and can be told to misbehave in the ways real servers
// do: no HEAD, no ranges, no Content-Length, slow or dropped connections.

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
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
    explicit MockHttpServer(MockServerOptions options)
        : options_(std::move(options)), drops_left_(options_.drop_connections) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int enable = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // let the kernel pick a free port

        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 64);

        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        accept_thread_ = std::thread([this] { accept_loop(); });
    }

    ~MockHttpServer() {
        stopping_ = true;
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        accept_thread_.join();

        std::vector<std::thread> clients;
        {
            std::scoped_lock lock(mutex_);
            for (const int fd : client_fds_) {
                ::shutdown(fd, SHUT_RDWR);
            }
            clients.swap(client_threads_);
        }
        for (auto& client : clients) {
            client.join();
        }
    }

    MockHttpServer(const MockHttpServer&) = delete;
    MockHttpServer& operator=(const MockHttpServer&) = delete;

    [[nodiscard]] std::string url(std::string_view path = "/file.bin") const {
        return std::format("http://127.0.0.1:{}{}", port_, path);
    }

    [[nodiscard]] std::vector<RecordedRequest> requests() const {
        std::scoped_lock lock(mutex_);
        return requests_;
    }

    [[nodiscard]] size_t request_count(std::string_view method) const {
        std::scoped_lock lock(mutex_);
        size_t count = 0;
        for (const auto& request : requests_) {
            if (request.method == method) ++count;
        }
        return count;
    }

    // Replaces the validator the server reports, as if the file had changed.
    void set_etag(std::string etag) {
        std::scoped_lock lock(mutex_);
        options_.etag = std::move(etag);
    }

    // Lifts the throttle for all following requests.
    void stop_throttling() {
        throttling_disabled_ = true;
    }

private:
    void accept_loop() {
        while (!stopping_) {
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                return;
            }
            std::scoped_lock lock(mutex_);
            if (stopping_) {
                ::close(client_fd);
                return;
            }
            client_fds_.push_back(client_fd);
            client_threads_.emplace_back([this, client_fd] {
                handle_client(client_fd);
                std::scoped_lock done_lock(mutex_);
                std::erase(client_fds_, client_fd);
                ::close(client_fd);
            });
        }
    }

    static bool send_all(int fd, const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t sent = 0;
        while (sent < len) {
            const ssize_t n = ::send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    static bool send_all(int fd, std::string_view text) {
        return send_all(fd, text.data(), text.size());
    }

    static std::string read_request(int fd) {
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            request.append(buffer, static_cast<size_t>(n));
        }
        return request;
    }

    void handle_client(int fd) {
        const std::string raw = read_request(fd);
        if (raw.empty()) {
            return;
        }
        const std::string_view request = raw;

        RecordedRequest record;
        record.raw = raw;
        const auto method_end = request.find(' ');
        const auto path_end = request.find(' ', method_end + 1);
        if (method_end == std::string_view::npos || path_end == std::string_view::npos) {
            return;
        }
        record.method = std::string(request.substr(0, method_end));
        record.path = std::string(request.substr(method_end + 1, path_end - method_end - 1));

        std::optional<uint64_t> range_start;
        std::optional<uint64_t> range_end;
        constexpr std::string_view range_prefix = "Range: bytes=";
        if (const auto pos = request.find(range_prefix); pos != std::string_view::npos) {
            std::string_view spec = request.substr(pos + range_prefix.size());
            spec = spec.substr(0, spec.find("\r\n"));
            const auto dash = spec.find('-');
            if (dash != std::string_view::npos) {
                uint64_t value = 0;
                if (std::from_chars(spec.data(), spec.data() + dash, value).ec == std::errc{}) {
                    range_start = value;
                }
                const std::string_view end_text = spec.substr(dash + 1);
                if (!end_text.empty() &&
                    std::from_chars(end_text.data(), end_text.data() + end_text.size(), value).ec == std::errc{}) {
                    range_end = value;
                }
            }
        }
        record.range_start = range_start;

        std::string etag;
        {
            std::scoped_lock lock(mutex_);
            requests_.push_back(record);
            etag = options_.etag;
        }

        const bool is_head = record.method == "HEAD";
        if (!is_head && record.method != "GET") {
            send_all(fd, "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        if (is_head && !options_.allow_head) {
            send_all(fd, "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        if (record.path.starts_with("/missing")) {
            const std::string_view body = is_head ? "" : "<html>not found</html>";
            send_all(fd, std::format("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 22\r\nConnection: close\r\n\r\n{}", body));
            return;
        }
        if (record.path == "/redirect") {
            const std::string_view body = is_head ? "" : "moved";
            send_all(fd, std::format("HTTP/1.1 302 Found\r\nLocation: {}\r\nContent-Length: 5\r\nConnection: close\r\n\r\n{}",
                                     options_.redirect_location, body));
            return;
        }

        const uint64_t size = options_.payload.size();
        const bool ranged = options_.support_ranges && range_start.has_value();
        uint64_t first = 0;
        uint64_t last = size > 0 ? size - 1 : 0;

        if (ranged) {
            if (*range_start >= size) {
                send_all(fd, std::format("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */{}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", size));
                return;
            }
            first = *range_start;
            if (range_end && *range_end < last) last = *range_end;
        }
        const uint64_t body_length = size > 0 ? last - first + 1 : 0;

        std::string header = ranged
            ? std::format("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes {}-{}/{}\r\n", first, last, size)
            : std::string("HTTP/1.1 200 OK\r\n");
        header += "Content-Type: application/octet-stream\r\n";
        if (options_.send_content_length) header += std::format("Content-Length: {}\r\n", body_length);
        if (options_.advertise_ranges) header += "Accept-Ranges: bytes\r\n";
        if (!etag.empty()) header += std::format("ETag: {}\r\n", etag);
        if (!options_.content_disposition.empty()) header += std::format("Content-Disposition: {}\r\n", options_.content_disposition);
        header += "Connection: close\r\n\r\n";

        if (!send_all(fd, header) || is_head || body_length == 0) {
            return;
        }

        const uint8_t* body = options_.payload.data() + first;
        uint64_t to_send = body_length;

        // Probe requests for a single byte are never cut short.
        if (body_length > 1 && drops_left_.fetch_sub(1) > 0) {
            to_send = std::min<uint64_t>(options_.drop_after, body_length - 1);
        }

        uint64_t sent = 0;
        while (sent < to_send && !stopping_) {
            const bool throttled = options_.throttle_bytes > 0 && !throttling_disabled_;
            const uint64_t piece = throttled ? std::min<uint64_t>(options_.throttle_bytes, to_send - sent) : to_send - sent;
            if (!send_all(fd, body + sent, piece)) {
                return;
            }
            sent += piece;
            if (throttled && sent < to_send) {
                std::this_thread::sleep_for(options_.throttle_pause);
            }
        }

        if (to_send == body_length) {
            // Close gracefully so that the client sees a complete response.
            ::shutdown(fd, SHUT_WR);
            char drain[256];
            while (::recv(fd, drain, sizeof(drain), 0) > 0) {}
        }
    }

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
inline std::vector<uint8_t> make_payload(size_t size) {
    std::vector<uint8_t> payload(size);
    uint32_t state = 0x9E3779B9u;
    for (auto& byte : payload) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(state >> 24);
    }
    return payload;
}

} // namespace deoxdm::test
