#include "test_framework.hpp"
#include "deox/shell/engine.hpp"
#include "deox/core/checksum.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <format>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <charconv>

namespace {

class MockHttpServer {
public:
    explicit MockHttpServer(std::vector<uint8_t> payload)
        : payload_(std::move(payload)) {
        server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // Kernel assigns available port

        ::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(server_fd_, 16);

        socklen_t len = sizeof(addr);
        ::getsockname(server_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        server_thread_ = std::jthread([this](std::stop_token stop) {
            run_server(stop);
        });
    }

    ~MockHttpServer() {
        stop();
    }

    void stop() {
        if (!stopped_.exchange(true)) {
            server_thread_.request_stop();
            if (server_fd_ >= 0) {
                ::shutdown(server_fd_, SHUT_RDWR);
                ::close(server_fd_);
                server_fd_ = -1;
            }
        }
    }

    [[nodiscard]] uint16_t port() const noexcept {
        return port_;
    }

private:
    void run_server(std::stop_token stop) {
        while (!stop.stop_requested() && !stopped_.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                break;
            }

            std::thread([this, client_fd]() {
                handle_client(client_fd);
            }).detach();
        }
    }

    static bool write_all(int fd, const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t written = 0;
        while (written < len) {
            const ssize_t n = ::write(fd, ptr + written, len - written);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                return false;
            }
            written += static_cast<size_t>(n);
        }
        return true;
    }

    static std::string read_http_request(int fd) {
        std::string req;
        char buf[1024];
        while (req.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
        }
        return req;
    }

    void handle_client(int client_fd) {
        const std::string req = read_http_request(client_fd);
        if (req.empty()) {
            ::close(client_fd);
            return;
        }

        const bool is_head = req.starts_with("HEAD");
        const bool is_get = req.starts_with("GET");

        if (!is_head && !is_get) {
            const std::string resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            (void)::write(client_fd, resp.data(), resp.size());
            ::close(client_fd);
            return;
        }

        // Look for Range header: "Range: bytes=start-end"
        uint64_t range_start = 0;
        uint64_t range_end = payload_.size() > 0 ? payload_.size() - 1 : 0;
        bool has_range = false;

        const std::string_view req_sv = req;
        const auto range_pos = req_sv.find("Range: bytes=");
        if (range_pos != std::string_view::npos) {
            has_range = true;
            std::string_view rest = req_sv.substr(range_pos + 13);
            const auto line_end = rest.find_first_of("\r\n");
            std::string_view range_line = (line_end != std::string_view::npos) ? rest.substr(0, line_end) : rest;
            const auto dash = range_line.find('-');
            if (dash != std::string_view::npos) {
                std::string_view start_str = range_line.substr(0, dash);
                std::string_view end_str = range_line.substr(dash + 1);
                std::from_chars(start_str.data(), start_str.data() + start_str.size(), range_start);
                std::from_chars(end_str.data(), end_str.data() + end_str.size(), range_end);
            }
        }

        if (range_end >= payload_.size()) {
            range_end = payload_.size() > 0 ? payload_.size() - 1 : 0;
        }

        const uint64_t content_len = (range_end >= range_start) ? (range_end - range_start + 1) : 0;

        std::string header;
        if (has_range) {
            header = std::format(
                "HTTP/1.1 206 Partial Content\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: {}\r\n"
                "Content-Range: bytes {}-{}/{}\r\n"
                "Accept-Ranges: bytes\r\n"
                "ETag: \"deox-mock-etag-v1\"\r\n"
                "Connection: close\r\n\r\n",
                content_len, range_start, range_end, payload_.size()
            );
        } else {
            header = std::format(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: {}\r\n"
                "Accept-Ranges: bytes\r\n"
                "ETag: \"deox-mock-etag-v1\"\r\n"
                "Connection: close\r\n\r\n",
                payload_.size()
            );
        }

        write_all(client_fd, header.data(), header.size());

        if (is_get && content_len > 0) {
            write_all(client_fd, payload_.data() + range_start, content_len);
        }

        ::shutdown(client_fd, SHUT_WR);
        char drain[256];
        while (::read(client_fd, drain, sizeof(drain)) > 0) {}
        ::close(client_fd);
    }

    int server_fd_{-1};
    uint16_t port_{0};
    std::vector<uint8_t> payload_;
    std::atomic<bool> stopped_{false};
    std::jthread server_thread_;
};

} // namespace

DEOX_TEST(integration_segmented_parallel_download) {
    // Generate 64 KB of deterministic patterned data
    constexpr size_t test_size = 64 * 1024;
    std::vector<uint8_t> payload(test_size);
    for (size_t i = 0; i < test_size; ++i) {
        payload[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
    }

    const std::string expected_sha256 = deox::core::compute_sha256(
        std::span<const uint8_t>(payload.data(), payload.size())
    );

    MockHttpServer server(payload);

    const std::string url = std::format("http://127.0.0.1:{}/testdata.bin", server.port());
    const std::filesystem::path out_path = "/tmp/deox_integration_test.bin";
    std::filesystem::remove(out_path);

    deox::core::DownloadConfig config;
    config.url = url;
    config.output_path = out_path;
    config.connections = 4;
    config.min_chunk_size = 8 * 1024; // 8KB min chunk to force 4 segments
    config.expected_sha256 = expected_sha256;
    config.quiet = true;

    deox::shell::DownloadEngine engine(config);
    auto res = engine.run();
    ASSERT_TRUE(res.has_value());

    // Verify downloaded file size
    ASSERT_EQ(std::filesystem::file_size(out_path), test_size);

    // Verify file content matches byte-for-byte
    std::vector<uint8_t> downloaded(test_size);
    std::ifstream ifs(out_path, std::ios::binary);
    ifs.read(reinterpret_cast<char*>(downloaded.data()), static_cast<std::streamsize>(test_size));
    ASSERT_TRUE(ifs.good());
    ASSERT_EQ(downloaded, payload);

    std::filesystem::remove(out_path);
    server.stop();
}
