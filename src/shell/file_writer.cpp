#include "deox/shell/file_writer.hpp"
#include "deox/core/checksum.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <format>
#include <array>

namespace deox::shell {

FileWriter::FileWriter(int fd, std::filesystem::path path, uint64_t total_size) noexcept
    : fd_(fd), path_(std::move(path)), total_size_(total_size) {}

FileWriter::~FileWriter() noexcept {
    close();
}

FileWriter::FileWriter(FileWriter&& other) noexcept
    : fd_(other.fd_), path_(std::move(other.path_)), total_size_(other.total_size_) {
    other.fd_ = -1;
    other.total_size_ = 0;
}

FileWriter& FileWriter::operator=(FileWriter&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        total_size_ = other.total_size_;
        other.fd_ = -1;
        other.total_size_ = 0;
    }
    return *this;
}

void FileWriter::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::expected<FileWriter, core::DeoxError> FileWriter::open(
    const std::filesystem::path& path,
    uint64_t total_size,
    bool preallocate
) noexcept {
    // Ensure parent directory exists
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(core::DeoxError{
                core::ErrorCode::FileSystemError,
                std::format("Failed to create parent directory '{}': {}", path.parent_path().string(), ec.message())
            });
        }
    }

    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return std::unexpected(core::DeoxError{
            core::ErrorCode::FileSystemError,
            std::format("Failed to open file '{}': {}", path.string(), std::strerror(errno))
        });
    }

    if (preallocate && total_size > 0) {
        const int alloc_res = ::posix_fallocate(fd, 0, static_cast<off_t>(total_size));
        if (alloc_res != 0) {
            // If fallocate fails (e.g. disk full), close and return error
            ::close(fd);
            return std::unexpected(core::DeoxError{
                core::ErrorCode::FileSystemError,
                std::format("Disk preallocation of {} bytes failed for '{}': {}", total_size, path.string(), std::strerror(alloc_res))
            });
        }
    }

    return FileWriter(fd, path, total_size);
}

std::expected<size_t, core::DeoxError> FileWriter::write_at(
    uint64_t offset,
    std::span<const uint8_t> data
) noexcept {
    if (fd_ < 0) {
        return std::unexpected(core::DeoxError{core::ErrorCode::FileSystemError, "File is not open"});
    }

    if (data.empty()) {
        return 0;
    }

    size_t total_written = 0;
    const size_t to_write = data.size();

    while (total_written < to_write) {
        const ssize_t res = ::pwrite(
            fd_,
            data.data() + total_written,
            to_write - total_written,
            static_cast<off_t>(offset + total_written)
        );

        if (res < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            return std::unexpected(core::DeoxError{
                core::ErrorCode::FileSystemError,
                std::format("pwrite error at offset {}: {}", offset + total_written, std::strerror(errno))
            });
        }

        if (res == 0) {
            break; // No more bytes could be written
        }

        total_written += static_cast<size_t>(res);
    }

    return total_written;
}

std::expected<void, core::DeoxError> FileWriter::sync() noexcept {
    if (fd_ < 0) {
        return std::unexpected(core::DeoxError{core::ErrorCode::FileSystemError, "File is not open"});
    }

    if (::fdatasync(fd_) != 0) {
        return std::unexpected(core::DeoxError{
            core::ErrorCode::FileSystemError,
            std::format("fdatasync failed: {}", std::strerror(errno))
        });
    }

    return {};
}

std::expected<std::string, core::DeoxError> FileWriter::compute_file_sha256() noexcept {
    if (fd_ < 0) {
        return std::unexpected(core::DeoxError{core::ErrorCode::FileSystemError, "File is not open"});
    }

    // Flush dirty pages to disk first
    (void)sync();

    core::Sha256 hasher;
    std::array<uint8_t, 64 * 1024> buf{}; // 64 KB buffer for streaming hash

    uint64_t offset = 0;
    while (true) {
        const ssize_t bytes_read = ::pread(fd_, buf.data(), buf.size(), static_cast<off_t>(offset));
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(core::DeoxError{
                core::ErrorCode::FileSystemError,
                std::format("pread failed during SHA-256 calculation: {}", std::strerror(errno))
            });
        }
        if (bytes_read == 0) {
            break; // EOF
        }

        hasher.update(std::span<const uint8_t>(buf.data(), static_cast<size_t>(bytes_read)));
        offset += static_cast<uint64_t>(bytes_read);
    }

    return hasher.finalize();
}

} // namespace deox::shell
