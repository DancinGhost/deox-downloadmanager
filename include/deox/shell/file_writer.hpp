#pragma once

#include "deox/core/types.hpp"
#include <span>
#include <filesystem>
#include <expected>

namespace deox::shell {

class FileWriter {
public:
    FileWriter() noexcept = default;
    ~FileWriter() noexcept;

    // Move-only semantics (RAII file descriptor management)
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&& other) noexcept;
    FileWriter& operator=(FileWriter&& other) noexcept;

    // Factory method: opens file, optionally pre-allocates with posix_fallocate
    [[nodiscard]] static std::expected<FileWriter, core::DeoxError> open(
        const std::filesystem::path& path,
        uint64_t total_size,
        bool preallocate = true
    ) noexcept;

    // Concurrent thread-safe positional write using pwrite(2)
    [[nodiscard]] std::expected<size_t, core::DeoxError> write_at(
        uint64_t offset,
        std::span<const uint8_t> data
    ) noexcept;

    // Flushes dirty buffers to storage via fdatasync(2)
    [[nodiscard]] std::expected<void, core::DeoxError> sync() noexcept;

    // Computes SHA-256 directly from the on-disk file in 64KB streaming blocks
    [[nodiscard]] std::expected<std::string, core::DeoxError> compute_file_sha256() noexcept;

    // File path getter
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return fd_ >= 0;
    }

    void close() noexcept;

private:
    explicit FileWriter(int fd, std::filesystem::path path, uint64_t total_size) noexcept;

    int fd_{-1};
    std::filesystem::path path_;
    uint64_t total_size_{0};
};

} // namespace deox::shell
