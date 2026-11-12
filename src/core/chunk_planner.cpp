// SPDX-License-Identifier: MIT
#include "deoxdm/core/chunk_planner.hpp"

#include <algorithm>

namespace deoxdm::core {

std::expected<ChunkPlan, Error> create_chunk_plan(
    uint64_t total_size,
    uint32_t requested_connections,
    bool accepts_ranges,
    uint64_t min_chunk_size
) noexcept {
    ChunkPlan plan;
    plan.total_size = total_size;

    if (total_size == 0) {
        return plan; // nothing to transfer
    }

    min_chunk_size = std::max<uint64_t>(min_chunk_size, 1);
    const uint64_t connections = std::clamp<uint32_t>(requested_connections, 1, kMaxConnections);
    const uint64_t max_useful_chunks = std::max<uint64_t>(total_size / min_chunk_size, 1);
    const uint64_t num_chunks = accepts_ranges ? std::min(connections, max_useful_chunks) : 1;

    plan.is_segmented = num_chunks > 1;
    plan.chunks.reserve(num_chunks);

    // Spread the remainder over the first chunks so sizes differ by at most one byte.
    const uint64_t base_chunk_size = total_size / num_chunks;
    const uint64_t remainder = total_size % num_chunks;

    uint64_t offset = 0;
    for (uint64_t i = 0; i < num_chunks; ++i) {
        const uint64_t chunk_len = base_chunk_size + ((i < remainder) ? 1 : 0);
        plan.chunks.push_back(ChunkState{
            .index = static_cast<uint32_t>(i),
            .range = ByteRange{offset, offset + chunk_len - 1},
            .bytes_downloaded = 0,
            .status = ChunkStatus::Pending
        });
        offset += chunk_len;
    }

    if (!verify_chunk_plan(plan)) {
        return std::unexpected(Error{ErrorCode::ChunkError, "Internal error: generated chunk plan does not cover the file exactly"});
    }

    return plan;
}

ChunkPlan create_unknown_size_plan() noexcept {
    ChunkPlan plan;
    plan.size_known = false;
    plan.chunks.push_back(ChunkState{
        .index = 0,
        .range = ByteRange{0, kOpenEndedByte},
        .bytes_downloaded = 0,
        .status = ChunkStatus::Pending
    });
    return plan;
}

bool verify_chunk_plan(const ChunkPlan& plan) noexcept {
    if (!plan.size_known) {
        return plan.chunks.size() == 1 && plan.chunks[0].index == 0 &&
               plan.chunks[0].range == ByteRange{0, kOpenEndedByte};
    }

    if (plan.total_size == 0) {
        return plan.chunks.empty();
    }

    uint64_t expected_offset = 0;
    for (size_t i = 0; i < plan.chunks.size(); ++i) {
        const auto& chunk = plan.chunks[i];
        if (chunk.index != i) return false;
        if (chunk.range.start_byte != expected_offset) return false;
        if (chunk.range.end_byte < chunk.range.start_byte) return false;
        if (chunk.range.end_byte >= plan.total_size) return false;
        expected_offset = chunk.range.end_byte + 1;
    }

    return expected_offset == plan.total_size;
}

} // namespace deoxdm::core
