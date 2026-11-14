// SPDX-License-Identifier: MIT
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/platform/file.hpp"
#include "deoxdm/platform/path.hpp"
#include "deoxdm/version.hpp"

#include <format>
#include <string>

namespace deoxdm::engine {

namespace {

using core::Error;
using core::ErrorCode;

constexpr uint64_t kMaxCheckpointBytes = 1024 * 1024;

} // namespace

std::filesystem::path CheckpointStore::checkpoint_path_for(const std::filesystem::path& target_file) {
    std::filesystem::path path = target_file;
    path += CHECKPOINT_SUFFIX;
    return path;
}

bool CheckpointStore::exists(const std::filesystem::path& target_file) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(
        std::filesystem::symlink_status(checkpoint_path_for(target_file), ec));
}

std::expected<void, Error> CheckpointStore::save(
    const std::filesystem::path& target_file,
    const core::ResumeData& data
) {
    const auto checkpoint_path = checkpoint_path_for(target_file);
    if (auto res = platform::write_file_atomically(checkpoint_path, core::serialize_checkpoint(data)); !res) {
        return std::unexpected(Error{
            ErrorCode::FileSystemError,
            std::format("Cannot write checkpoint '{}': {}", platform::path_to_utf8(checkpoint_path), res.error().message())
        });
    }
    return {};
}

std::expected<core::ResumeData, Error> CheckpointStore::load(const std::filesystem::path& target_file) {
    const auto checkpoint_path = checkpoint_path_for(target_file);

    auto file = platform::File::open(checkpoint_path, platform::OpenMode::ReadOnly, {.follow_symlinks = false});
    if (!file) {
        return std::unexpected(Error{
            ErrorCode::FileSystemError,
            std::format("Cannot open checkpoint '{}': {}", platform::path_to_utf8(checkpoint_path), file.error().message())
        });
    }

    const auto file_size = file->size();
    if (!file_size) {
        return std::unexpected(Error{
            ErrorCode::FileSystemError,
            std::format("Cannot inspect checkpoint '{}': {}", platform::path_to_utf8(checkpoint_path), file_size.error().message())
        });
    }
    if (*file_size > kMaxCheckpointBytes) {
        return std::unexpected(Error{
            ErrorCode::StateCorrupted,
            std::format("Checkpoint '{}' exceeds maximum allowed size ({} bytes)", platform::path_to_utf8(checkpoint_path), kMaxCheckpointBytes)
        });
    }

    std::string content(static_cast<size_t>(*file_size), '\0');
    size_t offset = 0;
    while (offset < content.size()) {
        auto read = file->read_at(offset, std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(content.data() + offset), content.size() - offset));
        if (!read) {
            return std::unexpected(Error{
                ErrorCode::FileSystemError,
                std::format("Cannot read checkpoint '{}': {}", platform::path_to_utf8(checkpoint_path), read.error().message())
            });
        }
        if (*read == 0) {
            content.resize(offset);
            break;
        }
        offset += *read;
    }

    return core::deserialize_checkpoint(content);
}

void CheckpointStore::remove(const std::filesystem::path& target_file) noexcept {
    try {
        std::error_code ec;
        const auto checkpoint_path = checkpoint_path_for(target_file);
        std::filesystem::remove(checkpoint_path, ec);
        std::filesystem::remove(platform::atomic_temp_path(checkpoint_path), ec);
    } catch (...) {
        // Only path allocation can throw; a leftover checkpoint is harmless.
    }
}

} // namespace deoxdm::engine
