// SPDX-License-Identifier: MIT
#include "deoxdm/engine/output_file.hpp"
#include "deoxdm/core/checksum.hpp"
#include "deoxdm/platform/path.hpp"

#include <format>
#include <system_error>
#include <utility>
#include <vector>

namespace deoxdm::engine {

namespace {

using core::Error;
using core::ErrorCode;

std::unexpected<Error> fs_error(std::string message) {
    return std::unexpected(Error{ErrorCode::FileSystemError, std::move(message)});
}

void remove_quietly(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

std::expected<OutputFile, Error> OutputFile::create(
    const std::filesystem::path& path,
    std::optional<uint64_t> total_size,
    bool server_chosen_name
) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return fs_error(std::format("Cannot create directory '{}': {}", platform::path_to_utf8(path.parent_path()), ec.message()));
        }
    }

    platform::OpenMode mode = platform::OpenMode::CreateTruncate;
    if (server_chosen_name && !std::filesystem::exists(std::filesystem::symlink_status(path, ec))) {
        mode = platform::OpenMode::CreateNew;
    }
    auto file = platform::File::open(path, mode, {.follow_symlinks = !server_chosen_name});
    if (!file) {
        return fs_error(std::format("Cannot open '{}' for writing: {}", platform::path_to_utf8(path), file.error().message()));
    }

    if (total_size && *total_size > 0) {
        // Reserving the blocks reports a full disk now rather than hours into
        // the download, and keeps the file contiguous.
        auto reserved = file->preallocate(*total_size);
        if (!reserved && platform::is_out_of_space(reserved.error())) {
            file->close();
            remove_quietly(path);
            return fs_error(std::format("Not enough disk space for '{}' ({} bytes): {}",
                platform::path_to_utf8(path), *total_size, reserved.error().message()));
        }
        // Some file systems cannot preallocate; a sparse file works just as well.
        if (!reserved) {
            if (auto sized = file->resize(*total_size); !sized) {
                file->close();
                remove_quietly(path);
                return fs_error(std::format("Cannot size '{}' to {} bytes: {}",
                    platform::path_to_utf8(path), *total_size, sized.error().message()));
            }
        }
    }

    return OutputFile(std::move(*file), path);
}

std::expected<OutputFile, Error> OutputFile::open_existing(
    const std::filesystem::path& path,
    uint64_t expected_size,
    bool server_chosen_name
) {
    auto file = platform::File::open(path, platform::OpenMode::OpenExisting, {.follow_symlinks = !server_chosen_name});
    if (!file) {
        return fs_error(std::format("Cannot open '{}': {}", platform::path_to_utf8(path), file.error().message()));
    }

    const auto size = file->size();
    if (!size) {
        return fs_error(std::format("Cannot inspect '{}': {}", platform::path_to_utf8(path), size.error().message()));
    }
    if (*size != expected_size) {
        return fs_error(std::format("'{}' does not match the unfinished download ({} bytes on disk, {} expected)",
            platform::path_to_utf8(path), *size, expected_size));
    }

    return OutputFile(std::move(*file), path);
}

std::expected<void, Error> OutputFile::write_at(uint64_t offset, std::span<const uint8_t> data) noexcept {
    if (!file_.is_open()) {
        return fs_error("File is not open");
    }
    if (auto res = file_.write_at(offset, data); !res) {
        return fs_error(std::format("Writing to '{}' failed: {}", platform::path_to_utf8(path_), res.error().message()));
    }
    return {};
}

std::expected<void, Error> OutputFile::truncate(uint64_t size) noexcept {
    if (!file_.is_open()) {
        return fs_error("File is not open");
    }
    if (auto res = file_.resize(size); !res) {
        return fs_error(std::format("Cannot resize '{}': {}", platform::path_to_utf8(path_), res.error().message()));
    }
    return {};
}

std::expected<void, Error> OutputFile::sync() noexcept {
    if (!file_.is_open()) {
        return fs_error("File is not open");
    }
    if (auto res = file_.sync(); !res) {
        return fs_error(std::format("Flushing '{}' to disk failed: {}", platform::path_to_utf8(path_), res.error().message()));
    }
    return {};
}

std::expected<std::string, Error> OutputFile::compute_sha256(std::stop_token stop_token) {
    if (!file_.is_open()) {
        return fs_error("File is not open");
    }

    core::Sha256 hasher;
    std::vector<uint8_t> buffer(1024 * 1024);

    uint64_t offset = 0;
    while (true) {
        if (stop_token.stop_requested()) {
            return std::unexpected(Error{ErrorCode::Cancelled, "Checksum verification cancelled"});
        }

        const auto bytes_read = file_.read_at(offset, buffer);
        if (!bytes_read) {
            return fs_error(std::format("Reading '{}' failed: {}", platform::path_to_utf8(path_), bytes_read.error().message()));
        }
        if (*bytes_read == 0) {
            break;
        }

        hasher.update(std::span<const uint8_t>(buffer.data(), *bytes_read));
        offset += *bytes_read;
    }

    return hasher.finalize();
}

} // namespace deoxdm::engine
