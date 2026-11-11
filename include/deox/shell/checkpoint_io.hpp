#pragma once

#include "deox/core/types.hpp"
#include "deox/core/checkpoint.hpp"
#include <filesystem>
#include <expected>

namespace deox::shell {

class CheckpointIO {
public:
    [[nodiscard]] static std::filesystem::path checkpoint_path_for(const std::filesystem::path& target_file) noexcept;

    [[nodiscard]] static bool exists(const std::filesystem::path& target_file) noexcept;

    // Atomically saves checkpoint state using write-to-temp and rename(2)
    [[nodiscard]] static std::expected<void, core::DeoxError> save(
        const std::filesystem::path& target_file,
        const std::string& url,
        uint64_t total_size,
        const std::string& etag,
        const std::string& last_modified,
        std::span<const core::ChunkState> chunks
    ) noexcept;

    // Loads and parses checkpoint state
    [[nodiscard]] static std::expected<core::ResumeData, core::DeoxError> load(
        const std::filesystem::path& target_file
    ) noexcept;

    // Removes checkpoint file upon successful completion
    static void remove(const std::filesystem::path& target_file) noexcept;
};

} // namespace deox::shell
