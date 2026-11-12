// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"
#include <expected>
#include <string>
#include <string_view>

namespace deoxdm::core {

// Serializes resume state into the line-based ".deoxdm" checkpoint format.
[[nodiscard]] std::string serialize_checkpoint(const ResumeData& data) noexcept;

// Parses and validates a checkpoint. The result is always safe to resume from:
// chunks are in order, cover the file exactly, never claim more bytes than
// they span, and every unfinished chunk is reset to Pending.
[[nodiscard]] std::expected<ResumeData, Error> deserialize_checkpoint(std::string_view content) noexcept;

// Checks that a checkpoint still describes the file the server offers now.
[[nodiscard]] std::expected<void, Error> validate_resume_compatibility(
    const ResumeData& resume,
    std::string_view current_url,
    const RemoteFileInfo& remote
) noexcept;

} // namespace deoxdm::core
