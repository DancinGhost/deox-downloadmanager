// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deoxdm::core {

enum class ErrorCode : uint32_t {
    Success = 0,
    InvalidUrl,
    NetworkError,
    HttpError,
    TlsError,
    RangeNotSupported,
    FileSystemError,
    ChunkError,
    ChecksumMismatch,
    StateCorrupted,
    Cancelled,
    InvalidArgument,
    InternalError
};

[[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success:           return "success";
        case ErrorCode::InvalidUrl:        return "invalid URL";
        case ErrorCode::NetworkError:      return "network error";
        case ErrorCode::HttpError:         return "HTTP error";
        case ErrorCode::TlsError:          return "TLS error";
        case ErrorCode::RangeNotSupported: return "range not supported";
        case ErrorCode::FileSystemError:   return "file system error";
        case ErrorCode::ChunkError:        return "chunk error";
        case ErrorCode::ChecksumMismatch:  return "checksum mismatch";
        case ErrorCode::StateCorrupted:    return "corrupted state";
        case ErrorCode::Cancelled:         return "cancelled";
        case ErrorCode::InvalidArgument:   return "invalid argument";
        case ErrorCode::InternalError:     return "internal error";
    }
    return "unknown error";
}

struct Error {
    ErrorCode code{ErrorCode::Success};
    std::string message;
    uint32_t http_status{0};

    [[nodiscard]] std::string to_string() const {
        if (http_status > 0) {
            return std::format("{} (HTTP {}): {}", error_code_name(code), http_status, message);
        }
        return std::format("{}: {}", error_code_name(code), message);
    }
};

// Whether repeating the same request has a realistic chance of succeeding.
[[nodiscard]] constexpr bool is_retryable(const Error& error) noexcept {
    switch (error.code) {
        case ErrorCode::NetworkError:
            return true;
        case ErrorCode::HttpError:
            return error.http_status == 408 || error.http_status == 425 || error.http_status == 429 ||
                   error.http_status == 500 || error.http_status == 502 || error.http_status == 503 ||
                   error.http_status == 504;
        default:
            return false;
    }
}

// Marks a range whose end is not known in advance (server did not report a size).
inline constexpr uint64_t kOpenEndedByte = std::numeric_limits<uint64_t>::max() - 1;

struct ByteRange {
    uint64_t start_byte{0};
    uint64_t end_byte{0}; // Inclusive: [start_byte, end_byte]

    [[nodiscard]] constexpr bool is_open_ended() const noexcept {
        return end_byte == kOpenEndedByte;
    }

    [[nodiscard]] constexpr uint64_t length() const noexcept {
        return (end_byte >= start_byte) ? (end_byte - start_byte + 1) : 0;
    }

    [[nodiscard]] constexpr bool contains(uint64_t offset) const noexcept {
        return offset >= start_byte && offset <= end_byte;
    }

    [[nodiscard]] constexpr bool operator==(const ByteRange& other) const noexcept = default;
};

enum class ChunkStatus : uint8_t {
    Pending = 0,
    Downloading,
    Completed,
    Failed
};

[[nodiscard]] constexpr std::string_view chunk_status_to_string(ChunkStatus status) noexcept {
    switch (status) {
        case ChunkStatus::Pending:     return "pending";
        case ChunkStatus::Downloading: return "downloading";
        case ChunkStatus::Completed:   return "completed";
        case ChunkStatus::Failed:      return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::optional<ChunkStatus> chunk_status_from_string(std::string_view s) noexcept {
    if (s == "pending")     return ChunkStatus::Pending;
    if (s == "downloading") return ChunkStatus::Downloading;
    if (s == "completed")   return ChunkStatus::Completed;
    if (s == "failed")      return ChunkStatus::Failed;
    return std::nullopt;
}

struct ChunkState {
    uint32_t index{0};
    ByteRange range{};
    uint64_t bytes_downloaded{0};
    ChunkStatus status{ChunkStatus::Pending};

    // An open-ended chunk is only complete once its status says so.
    [[nodiscard]] constexpr bool is_complete() const noexcept {
        if (range.is_open_ended()) return status == ChunkStatus::Completed;
        return bytes_downloaded >= range.length();
    }

    [[nodiscard]] constexpr uint64_t bytes_remaining() const noexcept {
        const uint64_t total = range.length();
        return (total > bytes_downloaded) ? (total - bytes_downloaded) : 0;
    }

    [[nodiscard]] constexpr double progress_ratio() const noexcept {
        if (range.is_open_ended()) return status == ChunkStatus::Completed ? 1.0 : 0.0;
        const uint64_t total = range.length();
        if (total == 0) return 1.0;
        return static_cast<double>(bytes_downloaded) / static_cast<double>(total);
    }

    [[nodiscard]] constexpr bool operator==(const ChunkState& other) const noexcept = default;
};

inline constexpr uint32_t kMaxConnections = 64;

struct DownloadConfig {
    std::string url;

    // Empty: save into the current directory under the name the server suggests.
    // Existing directory: save into it under the suggested name.
    // Anything else: the exact file to write (replaced if it already exists).
    std::filesystem::path output_path;

    uint32_t connections{4};              // 1..kMaxConnections
    uint64_t min_chunk_size{1024 * 1024}; // do not split below this size
    uint32_t timeout_seconds{30};         // connect timeout, and how long a stalled transfer is tolerated
    uint32_t max_retries{3};              // consecutive failed attempts per chunk before giving up
    uint64_t max_speed_bps{0};            // total download rate cap in bytes/second, 0 = unlimited
    uint64_t max_file_size{0};            // maximum allowed file size in bytes, 0 = unlimited
    bool allow_insecure_tls{false};
    bool resume_enabled{true};
    bool overwrite{false};                // with a derived file name: replace an existing file instead of renaming
    std::string expected_sha256;          // empty = no verification
    std::string user_agent;               // empty = "deoxdm/<version>"
    std::vector<std::string> headers;     // extra request headers, "Name: value"
};

struct RemoteFileInfo {
    std::optional<uint64_t> content_length; // nullopt when the server does not report a size
    bool accepts_ranges{false};
    std::string etag;
    std::string last_modified;
    std::string suggested_filename; // sanitized; from Content-Disposition or the final URL
    std::string effective_url;      // after redirects
    uint32_t http_status{0};
};

struct ResumeData {
    std::string url;
    uint64_t total_size{0};
    std::string etag;
    std::string last_modified;
    std::string output_file;
    std::vector<ChunkState> chunks;
};

struct DownloadResult {
    std::filesystem::path output_path;
    uint64_t total_bytes{0};
    uint64_t bytes_transferred{0}; // this run only; smaller than total_bytes after a resume
    std::chrono::milliseconds elapsed{0};
    double average_speed_bps{0.0};
    std::string sha256;            // set when a checksum was verified
    bool resumed{false};
};

} // namespace deoxdm::core
