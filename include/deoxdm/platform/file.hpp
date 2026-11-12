// SPDX-License-Identifier: MIT
#pragma once

// Operating-system abstraction for files.
//
// Everything above this layer (engine, apps) is written against these
// declarations only. Each supported operating system provides one
// implementation under src/platform/<os>/; the build selects it.
//
// Errors are reported as std::error_code in std::system_category(), i.e. the
// native error number (errno on POSIX, GetLastError() on Windows), so callers
// can print error.message() without knowing the platform.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>

namespace deoxdm::platform {

template <typename T>
using Result = std::expected<T, std::error_code>;

enum class OpenMode {
    CreateTruncate, // read/write; create the file or discard its content
    OpenExisting,   // read/write; the file must exist, content is kept
    ReadOnly        // read only; the file must exist
};

// An open file with positional (offset-based) I/O.
//
// read_at() and write_at() do not use or move a shared file position, so any
// number of threads may operate on disjoint regions of the same File at once.
// Closing and moving are not thread-safe.
class File {
public:
#if defined(_WIN32)
    using NativeHandle = void*; // HANDLE
#else
    using NativeHandle = int;   // file descriptor
#endif

    File() noexcept = default;
    ~File() noexcept;

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    [[nodiscard]] static Result<File> open(const std::filesystem::path& path, OpenMode mode) noexcept;

    // Writes all of `data` at `offset`, retrying short writes.
    [[nodiscard]] Result<void> write_at(uint64_t offset, std::span<const uint8_t> data) noexcept;

    // Reads up to `buffer.size()` bytes at `offset`. Returns 0 at end of file.
    [[nodiscard]] Result<size_t> read_at(uint64_t offset, std::span<uint8_t> buffer) noexcept;

    // Reserves disk blocks for `size` bytes and extends the file to that size.
    // Fails with an out-of-space error (see is_out_of_space) when the volume
    // is too small; fails with std::errc::not_supported when the file system
    // cannot preallocate, in which case resize() is the fallback.
    [[nodiscard]] Result<void> preallocate(uint64_t size) noexcept;

    // Sets the file length, extending (sparsely where supported) or cutting.
    [[nodiscard]] Result<void> resize(uint64_t size) noexcept;

    [[nodiscard]] Result<uint64_t> size() const noexcept;

    // Flushes written data to stable storage.
    [[nodiscard]] Result<void> sync() noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    void close() noexcept;

private:
    explicit File(NativeHandle handle) noexcept : handle_(handle) {}

#if defined(_WIN32)
    NativeHandle handle_{nullptr};
#else
    NativeHandle handle_{-1};
#endif
};

// Replaces `path` with `content` atomically: the data is written to a
// temporary file in the same directory, flushed to disk and renamed over the
// target. Readers see either the old or the new content, never a mix, even
// after a crash or power loss.
[[nodiscard]] Result<void> write_file_atomically(const std::filesystem::path& path, std::string_view content) noexcept;

// The name of the temporary file write_file_atomically() uses for `path`.
[[nodiscard]] std::filesystem::path atomic_temp_path(const std::filesystem::path& path);

// Whether `error` means the disk or the user's quota is full.
[[nodiscard]] bool is_out_of_space(const std::error_code& error) noexcept;

} // namespace deoxdm::platform
