// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"
#include <expected>
#include <vector>

namespace deoxdm::core {

struct ChunkPlan {
    std::vector<ChunkState> chunks;
    uint64_t total_size{0};
    bool size_known{true};
    bool is_segmented{false};
};

// Splits [0, total_size) into contiguous, non-overlapping chunks, one per
// connection. Falls back to a single chunk when the server cannot serve ranges
// or the file is too small to be worth splitting. A zero-byte file yields a
// plan without chunks.
[[nodiscard]] std::expected<ChunkPlan, Error> create_chunk_plan(
    uint64_t total_size,
    uint32_t requested_connections,
    bool accepts_ranges = true,
    uint64_t min_chunk_size = 1024 * 1024
) noexcept;

// Plan for a download whose size the server does not report: one open-ended
// chunk that is read until the server closes the stream.
[[nodiscard]] ChunkPlan create_unknown_size_plan() noexcept;

// Checks that the chunks are indexed in order and cover the file exactly once.
[[nodiscard]] bool verify_chunk_plan(const ChunkPlan& plan) noexcept;

} // namespace deoxdm::core
