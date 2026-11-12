// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"

#include <expected>
#include <filesystem>

namespace deoxdm::engine {

// Reads and writes the "<file>.deoxdm" checkpoint that sits next to an
// unfinished download.
class CheckpointStore {
public:
    [[nodiscard]] static std::filesystem::path checkpoint_path_for(const std::filesystem::path& target_file);

    [[nodiscard]] static bool exists(const std::filesystem::path& target_file) noexcept;

    // Replaces the checkpoint atomically: the new content is written to a
    // temporary file, flushed to disk, then renamed over the old checkpoint.
    [[nodiscard]] static std::expected<void, core::Error> save(
        const std::filesystem::path& target_file,
        const core::ResumeData& data
    );

    [[nodiscard]] static std::expected<core::ResumeData, core::Error> load(
        const std::filesystem::path& target_file
    );

    static void remove(const std::filesystem::path& target_file) noexcept;
};

} // namespace deoxdm::engine
