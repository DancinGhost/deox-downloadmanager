#pragma once

#include "deox/core/types.hpp"
#include "deox/core/chunk_planner.hpp"
#include <string>
#include <string_view>
#include <expected>

namespace deox::core {

// Pure function to serialize checkpoint data to string
[[nodiscard]] std::string serialize_checkpoint(
    const std::string& url,
    uint64_t total_size,
    const std::string& etag,
    const std::string& last_modified,
    const std::string& output_filename,
    std::span<const ChunkState> chunks
) noexcept;

// Pure function to deserialize checkpoint string into ResumeData
[[nodiscard]] std::expected<ResumeData, DeoxError> deserialize_checkpoint(
    std::string_view content
) noexcept;

// Pure function to validate that saved checkpoint is compatible with fresh probe
[[nodiscard]] std::expected<void, DeoxError> validate_resume_compatibility(
    const ResumeData& resume,
    const std::string& current_url,
    uint64_t current_size,
    std::string_view current_etag
) noexcept;

} // namespace deox::core
