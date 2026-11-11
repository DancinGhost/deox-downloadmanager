#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include <optional>
#include <expected>
#include <format>

namespace deox::core {

enum class ErrorCode : uint32_t {
    Success = 0,
    InvalidUrl,
    NetworkError,
    HttpError,
    FileSystemError,
    FileTooLarge,
    ChunkError,
    ChecksumMismatch,
    StateCorrupted,
    Cancelled,
    InvalidArgument
};

struct DeoxError {
    ErrorCode code{ErrorCode::Success};
    std::string message;
    uint32_t http_status{0};

    [[nodiscard]] constexpr bool is_success() const noexcept {
        return code == ErrorCode::Success;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return code != ErrorCode::Success;
    }

    [[nodiscard]] std::string to_string() const {
        if (http_status > 0) {
            return std::format("[Error {} (HTTP {})]: {}", static_cast<uint32_t>(code), http_status, message);
        }
        return std::format("[Error {}]: {}", static_cast<uint32_t>(code), message);
    }
};

struct ByteRange {
    uint64_t start_byte{0};
    uint64_t end_byte{0}; // Inclusive: [start_byte, end_byte]

    [[nodiscard]] constexpr uint64_t length() const noexcept {
        return (end_byte >= start_byte) ? (end_byte - start_byte + 1) : 0;
    }

    [[nodiscard]] constexpr bool contains(uint64_t offset) const noexcept {
        return offset >= start_byte && offset <= end_byte;
    }

    [[nodiscard]] constexpr bool operator==(const ByteRange& other) const noexcept = default;

    [[nodiscard]] std::string to_range_header() const {
        return std::format("bytes={}-{}", start_byte, end_byte);
    }
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

    [[nodiscard]] constexpr bool is_complete() const noexcept {
        return bytes_downloaded >= range.length();
    }

    [[nodiscard]] constexpr uint64_t bytes_remaining() const noexcept {
        const uint64_t total = range.length();
        return (total > bytes_downloaded) ? (total - bytes_downloaded) : 0;
    }

    [[nodiscard]] constexpr double progress_ratio() const noexcept {
        const uint64_t total = range.length();
        if (total == 0) return 1.0;
        return static_cast<double>(bytes_downloaded) / static_cast<double>(total);
    }

    [[nodiscard]] constexpr bool operator==(const ChunkState& other) const noexcept = default;
};

struct DownloadConfig {
    std::string url;
    std::filesystem::path output_path;
    uint32_t connections{4};
    uint64_t min_chunk_size{1024 * 1024}; // 1 MB minimum per chunk to avoid thread overhead
    uint32_t timeout_seconds{30};
    uint32_t max_retries{3};
    bool allow_insecure_tls{false};
    std::string expected_sha256;
    bool resume_enabled{true};
    bool quiet{false};
};

struct RemoteFileInfo {
    uint64_t content_length{0};
    bool accepts_ranges{false};
    std::string etag;
    std::string last_modified;
    std::string suggested_filename;
    uint32_t http_status{200};
};

struct ResumeData {
    std::string url;
    uint64_t total_size{0};
    std::string etag;
    std::string last_modified;
    std::string output_file;
    std::vector<ChunkState> chunks;
};

} // namespace deox::core
