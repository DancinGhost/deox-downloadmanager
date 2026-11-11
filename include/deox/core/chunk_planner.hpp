#pragma once

#include "deox/core/types.hpp"
#include <vector>
#include <expected>

namespace deox::core {

struct ChunkPlan {
    std::vector<ChunkState> chunks;
    uint64_t total_size{0};
    bool is_segmented{false};

    [[nodiscard]] size_t num_chunks() const noexcept {
        return chunks.size();
    }
};

// Pure function to generate an optimal, disjoint, gap-free chunk plan
[[nodiscard]] std::expected<ChunkPlan, DeoxError> create_chunk_plan(
    uint64_t total_size,
    uint32_t requested_connections,
    bool accepts_ranges = true,
    uint64_t min_chunk_size = 1024 * 1024
) noexcept;

// Pure verification function to assert plan invariants
[[nodiscard]] bool verify_chunk_plan(const ChunkPlan& plan) noexcept;

} // namespace deox::core
