// SPDX-License-Identifier: MIT
#pragma once

// Internal interface between the operating-system independent part of the
// platform layer (src/platform/common/) and the backends
// (src/platform/<backend>/). Not part of the public headers.
//
// A new backend implements everything in include/deoxdm/platform/ that
// common/ does not, plus the functions declared here.

#include "deoxdm/platform/file.hpp"

#include <atomic>
#include <filesystem>

namespace deoxdm::platform::detail {

// Deletes a file, ignoring every error (including "does not exist").
void remove_file_quietly(const std::filesystem::path& path) noexcept;

// Renames `from` to `to`, replacing `to` if it exists, as one atomic step
// where the operating system allows it.
[[nodiscard]] Result<void> replace_file(const std::filesystem::path& from, const std::filesystem::path& to) noexcept;

// Set by the backend's interrupt handler, read by interrupt_requested().
// Lock-free, so it is safe to set from a signal handler.
extern std::atomic<bool> g_interrupt_requested;
static_assert(std::atomic<bool>::is_always_lock_free);

} // namespace deoxdm::platform::detail
