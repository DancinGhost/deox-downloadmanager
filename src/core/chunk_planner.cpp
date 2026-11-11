#include "deox/core/chunk_planner.hpp"
#include <algorithm>

namespace deox::core {

std::expected<ChunkPlan, DeoxError> create_chunk_plan(
    uint64_t total_size,
    uint32_t requested_connections,
    bool accepts_ranges,
    uint64_t min_chunk_size
) noexcept {
    ChunkPlan plan;
    plan.total_size = total_size;

    // Edge case: empty file or server does not support Range requests
    if (total_size == 0 || !accepts_ranges || requested_connections <= 1) {
        plan.is_segmented = false;
        plan.chunks.push_back(ChunkState{
            .index = 0,
            .range = ByteRange{0, total_size > 0 ? total_size - 1 : 0},
            .bytes_downloaded = 0,
            .status = ChunkStatus::Pending
        });
        return plan;
    }

    if (min_chunk_size == 0) {
        min_chunk_size = 1;
    }

    // Determine safe, optimal number of chunks
    const uint64_t max_possible_chunks = total_size / min_chunk_size;
    uint32_t num_chunks = requested_connections;
    if (max_possible_chunks < requested_connections) {
        num_chunks = static_cast<uint32_t>(std::max<uint64_t>(1, max_possible_chunks));
    }

    if (num_chunks <= 1) {
        plan.is_segmented = false;
        plan.chunks.push_back(ChunkState{
            .index = 0,
            .range = ByteRange{0, total_size - 1},
            .bytes_downloaded = 0,
            .status = ChunkStatus::Pending
        });
        return plan;
    }

    plan.is_segmented = true;
    plan.chunks.reserve(num_chunks);

    const uint64_t base_chunk_size = total_size / num_chunks;
    const uint64_t remainder = total_size % num_chunks;

    uint64_t current_offset = 0;
    for (uint32_t i = 0; i < num_chunks; ++i) {
        const uint64_t extra = (i < remainder) ? 1 : 0;
        const uint64_t chunk_len = base_chunk_size + extra;
        const uint64_t chunk_end = current_offset + chunk_len - 1;

        plan.chunks.push_back(ChunkState{
            .index = i,
            .range = ByteRange{current_offset, chunk_end},
            .bytes_downloaded = 0,
            .status = ChunkStatus::Pending
        });

        current_offset += chunk_len;
    }

    if (!verify_chunk_plan(plan)) {
        return std::unexpected(DeoxError{ErrorCode::ChunkError, "Generated chunk plan failed mathematical invariant verification"});
    }

    return plan;
}

bool verify_chunk_plan(const ChunkPlan& plan) noexcept {
    if (plan.chunks.empty()) {
        return plan.total_size == 0;
    }

    if (plan.total_size == 0) {
        return plan.chunks.size() == 1 && plan.chunks[0].range.start_byte == 0 && plan.chunks[0].range.end_byte == 0;
    }

    uint64_t expected_offset = 0;
    uint64_t sum_length = 0;

    for (size_t i = 0; i < plan.chunks.size(); ++i) {
        const auto& chunk = plan.chunks[i];
        if (chunk.index != i) return false;
        if (chunk.range.start_byte != expected_offset) return false;
        if (chunk.range.end_byte < chunk.range.start_byte) return false;

        const uint64_t len = chunk.range.length();
        sum_length += len;
        expected_offset = chunk.range.end_byte + 1;
    }

    return (sum_length == plan.total_size) && (expected_offset == plan.total_size);
}

} // namespace deox::core
