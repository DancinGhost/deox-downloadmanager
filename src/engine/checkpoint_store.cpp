// SPDX-License-Identifier: MIT
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/platform/file.hpp"
#include "deoxdm/platform/path.hpp"
#include "deoxdm/version.hpp"

#include <format>
#include <fstream>
#include <iterator>

namespace deoxdm::engine {

namespace {

using core::Error;
using core::ErrorCode;

} // namespace

std::filesystem::path CheckpointStore::checkpoint_path_for(const std::filesystem::path& target_file) {
    std::filesystem::path path = target_file;
    path += CHECKPOINT_SUFFIX;
    return path;
}

bool CheckpointStore::exists(const std::filesystem::path& target_file) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(checkpoint_path_for(target_file), ec);
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

    std::ifstream ifs(checkpoint_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return std::unexpected(Error{
            ErrorCode::FileSystemError,
            std::format("Cannot open checkpoint '{}'", platform::path_to_utf8(checkpoint_path))
        });
    }

    const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
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
