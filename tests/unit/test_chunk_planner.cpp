// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/chunk_planner.hpp"

using namespace deoxdm::core;

DEOXDM_TEST(chunk_planner_single_connection) {
    auto plan = create_chunk_plan(1000, 1, true, 100);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 1u);
    ASSERT_EQ(plan->chunks[0].range, (ByteRange{0, 999}));
    ASSERT_EQ(plan->chunks[0].range.length(), 1000u);
    ASSERT_FALSE(plan->is_segmented);
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOXDM_TEST(chunk_planner_multi_connection_exact_split) {
    auto plan = create_chunk_plan(4000, 4, true, 500);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 4u);
    ASSERT_TRUE(plan->is_segmented);

    ASSERT_EQ(plan->chunks[0].range, (ByteRange{0, 999}));
    ASSERT_EQ(plan->chunks[1].range, (ByteRange{1000, 1999}));
    ASSERT_EQ(plan->chunks[2].range, (ByteRange{2000, 2999}));
    ASSERT_EQ(plan->chunks[3].range, (ByteRange{3000, 3999}));
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOXDM_TEST(chunk_planner_remainder_distribution) {
    // 1003 bytes over 4 connections: 251, 251, 251, 250
    auto plan = create_chunk_plan(1003, 4, true, 100);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 4u);
    ASSERT_EQ(plan->chunks[0].range.length(), 251u);
    ASSERT_EQ(plan->chunks[1].range.length(), 251u);
    ASSERT_EQ(plan->chunks[2].range.length(), 251u);
    ASSERT_EQ(plan->chunks[3].range.length(), 250u);
    ASSERT_EQ(plan->chunks[3].range.end_byte, 1002u);
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOXDM_TEST(chunk_planner_respects_no_ranges) {
    auto plan = create_chunk_plan(10000000, 8, false, 1000);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 1u);
    ASSERT_FALSE(plan->is_segmented);
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOXDM_TEST(chunk_planner_small_file_is_not_split) {
    // Splitting 1500 bytes with a 1000 byte minimum would create tiny chunks.
    auto plan = create_chunk_plan(1500, 8, true, 1000);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 1u);

    auto three = create_chunk_plan(3500, 8, true, 1000);
    ASSERT_TRUE(three.has_value());
    ASSERT_EQ(three->chunks.size(), 3u);
    ASSERT_TRUE(verify_chunk_plan(*three));
}

DEOXDM_TEST(chunk_planner_limits_connections) {
    auto plan = create_chunk_plan(1'000'000, 5000, true, 1);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), static_cast<size_t>(kMaxConnections));
    ASSERT_TRUE(verify_chunk_plan(*plan));

    // Zero connections is treated as one rather than dividing by zero.
    auto zero = create_chunk_plan(1000, 0, true, 1);
    ASSERT_TRUE(zero.has_value());
    ASSERT_EQ(zero->chunks.size(), 1u);
}

DEOXDM_TEST(chunk_planner_zero_bytes) {
    auto plan = create_chunk_plan(0, 4, true, 100);
    ASSERT_TRUE(plan.has_value());
    ASSERT_TRUE(plan->chunks.empty());
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOXDM_TEST(chunk_planner_unknown_size) {
    const auto plan = create_unknown_size_plan();
    ASSERT_FALSE(plan.size_known);
    ASSERT_EQ(plan.chunks.size(), 1u);
    ASSERT_TRUE(plan.chunks[0].range.is_open_ended());
    ASSERT_TRUE(verify_chunk_plan(plan));

    // An open-ended chunk is finished when it is marked so, not by byte count.
    ChunkState chunk = plan.chunks[0];
    chunk.bytes_downloaded = 123456;
    ASSERT_FALSE(chunk.is_complete());
    chunk.status = ChunkStatus::Completed;
    ASSERT_TRUE(chunk.is_complete());
}

DEOXDM_TEST(chunk_planner_verify_detects_bad_plans) {
    auto plan = *create_chunk_plan(4000, 4, true, 500);

    auto gap = plan;
    gap.chunks[1].range.start_byte += 1;
    ASSERT_FALSE(verify_chunk_plan(gap));

    auto overlap = plan;
    overlap.chunks[0].range.end_byte += 10;
    ASSERT_FALSE(verify_chunk_plan(overlap));

    auto short_plan = plan;
    short_plan.chunks.pop_back();
    ASSERT_FALSE(verify_chunk_plan(short_plan));

    auto misnumbered = plan;
    misnumbered.chunks[2].index = 5;
    ASSERT_FALSE(verify_chunk_plan(misnumbered));
}
