// SPDX-License-Identifier: MIT
//
// Windows implementation of deoxdm/platform/file.hpp.
//
// STATUS: experimental. Windows is not a supported target yet and this file
// is not built in CI; it exists so the rest of the code base stays free of
// POSIX assumptions. See docs/PLATFORMS.md.

#include "deoxdm/platform/file.hpp"
#include "backend.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace deoxdm::platform {

namespace {

std::unexpected<std::error_code> error_from(DWORD code) noexcept {
    return std::unexpected(std::error_code(static_cast<int>(code), std::system_category()));
}

std::unexpected<std::error_code> last_error() noexcept {
    return error_from(::GetLastError());
}

OVERLAPPED overlapped_at(uint64_t offset) noexcept {
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    return ov;
}

constexpr uint64_t kMaxFileOffset = static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max());

} // namespace

File::~File() noexcept {
    close();
}

File::File(File&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

bool File::is_open() const noexcept {
    return handle_ != nullptr;
}

void File::close() noexcept {
    if (handle_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

Result<File> File::open(const std::filesystem::path& path, OpenMode mode, OpenOptions options) noexcept {
    DWORD access = GENERIC_READ;
    DWORD disposition = OPEN_EXISTING;
    switch (mode) {
        // Truncate only after verifying the handle is a regular disk file and
        // not a reparse point.
        case OpenMode::CreateTruncate: access |= GENERIC_WRITE; disposition = OPEN_ALWAYS; break;
        case OpenMode::CreateNew:      access |= GENERIC_WRITE; disposition = CREATE_NEW; break;
        case OpenMode::OpenExisting:   access |= GENERIC_WRITE; disposition = OPEN_EXISTING; break;
        case OpenMode::ReadOnly:       break;
    }
    // Without following links, a reparse point is opened itself; it is
    // rejected below once the handle shows what it is.
    const DWORD flags = FILE_ATTRIBUTE_NORMAL | (options.follow_symlinks ? 0 : FILE_FLAG_OPEN_REPARSE_POINT);

    // FILE_SHARE_DELETE lets the file be renamed or removed while open, which
    // matches POSIX semantics closely enough for our use.
    const HANDLE handle = ::CreateFileW(path.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        disposition, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return last_error();
    }
    File file(static_cast<NativeHandle>(handle));
    if (::GetFileType(handle) != FILE_TYPE_DISK) {
        return error_from(ERROR_INVALID_PARAMETER);
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(handle, &info)) {
        return last_error();
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return error_from(ERROR_ACCESS_DENIED);
    }
    if (!options.follow_symlinks && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return error_from(ERROR_CANT_ACCESS_FILE);
    }
    if (mode == OpenMode::CreateTruncate) {
        if (auto res = file.resize(0); !res) {
            return std::unexpected(res.error());
        }
    }
    return file;
}

Result<void> File::write_at(uint64_t offset, std::span<const uint8_t> data) noexcept {
    if (handle_ == nullptr) {
        return error_from(ERROR_INVALID_HANDLE);
    }
    if (offset > kMaxFileOffset || data.size() > kMaxFileOffset - offset) {
        return error_from(ERROR_FILE_TOO_LARGE);
    }

    size_t written = 0;
    while (written < data.size()) {
        const DWORD request = static_cast<DWORD>(
            std::min<size_t>(data.size() - written, std::numeric_limits<DWORD>::max()));
        OVERLAPPED ov = overlapped_at(offset + written);
        DWORD done = 0;
        // With an OVERLAPPED structure on a synchronous handle WriteFile writes
        // at the given offset; concurrent calls do not interfere.
        if (!::WriteFile(static_cast<HANDLE>(handle_), data.data() + written, request, &done, &ov)) {
            return last_error();
        }
        if (done == 0) {
            return error_from(ERROR_WRITE_FAULT);
        }
        written += done;
    }
    return {};
}

Result<size_t> File::read_at(uint64_t offset, std::span<uint8_t> buffer) noexcept {
    if (handle_ == nullptr) {
        return error_from(ERROR_INVALID_HANDLE);
    }
    const DWORD request = static_cast<DWORD>(std::min<size_t>(buffer.size(), std::numeric_limits<DWORD>::max()));
    OVERLAPPED ov = overlapped_at(offset);
    DWORD done = 0;
    if (!::ReadFile(static_cast<HANDLE>(handle_), buffer.data(), request, &done, &ov)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_HANDLE_EOF) {
            return size_t{0};
        }
        return error_from(err);
    }
    return static_cast<size_t>(done);
}

Result<void> File::preallocate(uint64_t size) noexcept {
    if (handle_ == nullptr) {
        return error_from(ERROR_INVALID_HANDLE);
    }
    if (size > kMaxFileOffset) {
        return error_from(ERROR_FILE_TOO_LARGE);
    }
    FILE_ALLOCATION_INFO info{};
    info.AllocationSize.QuadPart = static_cast<LONGLONG>(size);
    if (!::SetFileInformationByHandle(static_cast<HANDLE>(handle_), FileAllocationInfo, &info, sizeof(info))) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL || err == ERROR_DISK_QUOTA_EXCEEDED) {
            return error_from(err);
        }
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    return resize(size);
}

Result<void> File::resize(uint64_t size) noexcept {
    if (handle_ == nullptr) {
        return error_from(ERROR_INVALID_HANDLE);
    }
    if (size > kMaxFileOffset) {
        return error_from(ERROR_FILE_TOO_LARGE);
    }
    FILE_END_OF_FILE_INFO info{};
    info.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
    if (!::SetFileInformationByHandle(static_cast<HANDLE>(handle_), FileEndOfFileInfo, &info, sizeof(info))) {
        return last_error();
    }
    return {};
}

Result<uint64_t> File::size() const noexcept {
    if (handle_ == nullptr) {
        return error_from(ERROR_INVALID_HANDLE);
    }
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(static_cast<HANDLE>(handle_), &size)) {
        return last_error();
    }
    return static_cast<uint64_t>(size.QuadPart);
}

Result<void> File::sync() noexcept {
    if (handle_ == nullptr) {
        return error_from(ERROR_INVALID_HANDLE);
    }
    if (!::FlushFileBuffers(static_cast<HANDLE>(handle_))) {
        return last_error();
    }
    return {};
}

namespace detail {

void remove_file_quietly(const std::filesystem::path& path) noexcept {
    ::DeleteFileW(path.c_str());
}

Result<void> replace_file(const std::filesystem::path& from, const std::filesystem::path& to) noexcept {
    if (!::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return last_error();
    }
    return {};
}

} // namespace detail

bool is_out_of_space(const std::error_code& error) noexcept {
    if (error.category() != std::system_category()) {
        return false;
    }
    const auto code = static_cast<DWORD>(error.value());
    return code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL || code == ERROR_DISK_QUOTA_EXCEEDED ||
           code == ERROR_FILE_TOO_LARGE;
}

} // namespace deoxdm::platform
