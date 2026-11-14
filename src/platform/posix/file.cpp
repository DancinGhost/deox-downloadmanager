// SPDX-License-Identifier: MIT
//
// POSIX implementation of deoxdm/platform/file.hpp (Linux, macOS, BSDs).

#include "deoxdm/platform/file.hpp"
#include "backend.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace deoxdm::platform {

namespace {

std::unexpected<std::error_code> last_error() noexcept {
    return std::unexpected(std::error_code(errno, std::system_category()));
}

std::unexpected<std::error_code> error_from(int code) noexcept {
    return std::unexpected(std::error_code(code, std::system_category()));
}

bool fits_off_t(uint64_t value) noexcept {
    return value <= static_cast<uint64_t>(std::numeric_limits<off_t>::max());
}

int flush_data(int fd) noexcept {
#if defined(__APPLE__)
    // macOS has no fdatasync; F_FULLFSYNC also flushes the drive's cache.
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
        return 0;
    }
    return ::fsync(fd);
#elif defined(__linux__)
    return ::fdatasync(fd);
#else
    return ::fsync(fd);
#endif
}

} // namespace

File::~File() noexcept {
    close();
}

File::File(File&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)) {}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

bool File::is_open() const noexcept {
    return handle_ >= 0;
}

void File::close() noexcept {
    if (handle_ >= 0) {
        ::close(handle_);
        handle_ = -1;
    }
}

Result<File> File::open(const std::filesystem::path& path, OpenMode mode, OpenOptions options) noexcept {
    int flags = O_CLOEXEC;
    switch (mode) {
        // Truncate only after fstat confirms that the opened descriptor is a
        // regular file, never a FIFO, device node, or symlink target.
        case OpenMode::CreateTruncate: flags |= O_RDWR | O_CREAT; break;
        case OpenMode::CreateNew:      flags |= O_RDWR | O_CREAT | O_EXCL; break;
        case OpenMode::OpenExisting:   flags |= O_RDWR; break;
        case OpenMode::ReadOnly:       flags |= O_RDONLY; break;
    }
    if (!options.follow_symlinks) {
        flags |= O_NOFOLLOW;
    }
    const mode_t permissions = options.owner_only ? 0600 : 0644;

    int fd = -1;
    do {
        fd = ::open(path.c_str(), flags, permissions);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return last_error();
    }

    File file(fd);
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return last_error();
    }
    if (!S_ISREG(st.st_mode)) {
        return error_from(EINVAL);
    }
    if (mode == OpenMode::CreateTruncate && st.st_size > 0) {
        if (auto res = file.resize(0); !res) {
            return std::unexpected(res.error());
        }
    }
    return file;
}

Result<void> File::write_at(uint64_t offset, std::span<const uint8_t> data) noexcept {
    if (handle_ < 0) {
        return error_from(EBADF);
    }
    if (!fits_off_t(offset) || data.size() > std::numeric_limits<uint64_t>::max() - offset ||
        !fits_off_t(offset + data.size())) {
        return error_from(EFBIG);
    }

    size_t written = 0;
    while (written < data.size()) {
        const ssize_t res = ::pwrite(handle_, data.data() + written, data.size() - written,
                                     static_cast<off_t>(offset + written));
        if (res < 0) {
            if (errno == EINTR) continue;
            return last_error();
        }
        if (res == 0) {
            return error_from(EIO);
        }
        written += static_cast<size_t>(res);
    }
    return {};
}

Result<size_t> File::read_at(uint64_t offset, std::span<uint8_t> buffer) noexcept {
    if (handle_ < 0) {
        return error_from(EBADF);
    }
    if (!fits_off_t(offset)) {
        return error_from(EFBIG);
    }
    while (true) {
        const ssize_t res = ::pread(handle_, buffer.data(), buffer.size(), static_cast<off_t>(offset));
        if (res < 0) {
            if (errno == EINTR) continue;
            return last_error();
        }
        return static_cast<size_t>(res);
    }
}

Result<void> File::preallocate(uint64_t size) noexcept {
    if (handle_ < 0) {
        return error_from(EBADF);
    }
    if (!fits_off_t(size)) {
        return error_from(EFBIG);
    }
#if defined(__APPLE__)
    // Ask for contiguous space first, then for any space.
    fstore_t store{F_ALLOCATECONTIG | F_ALLOCATEALL, F_PEOFPOSMODE, 0, static_cast<off_t>(size), 0};
    if (::fcntl(handle_, F_PREALLOCATE, &store) != 0) {
        store.fst_flags = F_ALLOCATEALL;
        if (::fcntl(handle_, F_PREALLOCATE, &store) != 0) {
            return errno == ENOSPC ? last_error() : error_from(ENOTSUP);
        }
    }
    return resize(size);
#else
    const int res = ::posix_fallocate(handle_, 0, static_cast<off_t>(size));
    if (res == 0) {
        return {};
    }
    if (res == EINVAL || res == EOPNOTSUPP || res == ENOSYS) {
        return error_from(ENOTSUP); // file system cannot preallocate
    }
    return error_from(res);
#endif
}

Result<void> File::resize(uint64_t size) noexcept {
    if (handle_ < 0) {
        return error_from(EBADF);
    }
    if (!fits_off_t(size)) {
        return error_from(EFBIG);
    }
    while (::ftruncate(handle_, static_cast<off_t>(size)) != 0) {
        if (errno != EINTR) {
            return last_error();
        }
    }
    return {};
}

Result<uint64_t> File::size() const noexcept {
    if (handle_ < 0) {
        return error_from(EBADF);
    }
    struct stat st{};
    if (::fstat(handle_, &st) != 0) {
        return last_error();
    }
    return static_cast<uint64_t>(st.st_size);
}

Result<void> File::sync() noexcept {
    if (handle_ < 0) {
        return error_from(EBADF);
    }
    if (flush_data(handle_) != 0) {
        return last_error();
    }
    return {};
}

namespace detail {

void remove_file_quietly(const std::filesystem::path& path) noexcept {
    ::unlink(path.c_str());
}

Result<void> replace_file(const std::filesystem::path& from, const std::filesystem::path& to) noexcept {
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return last_error();
    }
    return {};
}

} // namespace detail

bool is_out_of_space(const std::error_code& error) noexcept {
    if (error.category() != std::system_category() && error.category() != std::generic_category()) {
        return false;
    }
    return error.value() == ENOSPC || error.value() == EFBIG
#ifdef EDQUOT
        || error.value() == EDQUOT
#endif
        ;
}

} // namespace deoxdm::platform
