// SPDX-License-Identifier: MIT
//
// Operating-system independent part of deoxdm/platform/file.hpp, built on the
// backend primitives in backend.hpp.

#include "deoxdm/platform/file.hpp"
#include "backend.hpp"

#include <cstdint>
#include <span>

namespace deoxdm::platform {

std::filesystem::path atomic_temp_path(const std::filesystem::path& path) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    return temp;
}

Result<void> write_file_atomically(const std::filesystem::path& path, std::string_view content) noexcept {
    std::filesystem::path tmp_path;
    try {
        tmp_path = atomic_temp_path(path);
    } catch (...) {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }

    // A leftover from a crash is removed first. Creating the file exclusively
    // and without following links means that nothing planted under this name,
    // such as a symbolic link in a shared directory, can redirect the write.
    detail::remove_file_quietly(tmp_path);
    auto file = File::open(tmp_path, OpenMode::CreateNew, {.follow_symlinks = false, .owner_only = true});
    if (!file) {
        return std::unexpected(file.error());
    }

    // The data must be on disk before the rename makes it visible, otherwise
    // a power loss could leave an empty file under the final name.
    const auto bytes = std::span(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    auto written = file->write_at(0, bytes);
    if (written) {
        written = file->sync();
    }
    file->close();

    if (written) {
        written = detail::replace_file(tmp_path, path);
    }
    if (!written) {
        detail::remove_file_quietly(tmp_path);
    }
    return written;
}

} // namespace deoxdm::platform
