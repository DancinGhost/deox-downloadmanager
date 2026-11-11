#include "test_framework.hpp"
#include "deox/core/chunk_planner.hpp"

using namespace deox::core;

DEOX_TEST(chunk_planner_single_connection) {
    auto plan = create_chunk_plan(1000, 1, true, 100);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 1);
    ASSERT_EQ(plan->chunks[0].range.start_byte, 0);
    ASSERT_EQ(plan->chunks[0].range.end_byte, 999);
    ASSERT_EQ(plan->chunks[0].range.length(), 1000);
    ASSERT_FALSE(plan->is_segmented);
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOX_TEST(chunk_planner_multi_connection_exact_split) {
    // 4000 bytes split across 4 connections = 1000 bytes each
    auto plan = create_chunk_plan(4000, 4, true, 500);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 4);
    ASSERT_TRUE(plan->is_segmented);

    ASSERT_EQ(plan->chunks[0].range, ByteRange(0, 999));
    ASSERT_EQ(plan->chunks[1].range, ByteRange(1000, 1999));
    ASSERT_EQ(plan->chunks[2].range, ByteRange(2000, 2999));
    ASSERT_EQ(plan->chunks[3].range, ByteRange(3000, 3999));

    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOX_TEST(chunk_planner_remainder_distribution) {
    // 1003 bytes split across 4 connections: 251, 251, 251, 250
    auto plan = create_chunk_plan(1003, 4, true, 100);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 4);

    ASSERT_EQ(plan->chunks[0].range.length(), 251);
    ASSERT_EQ(plan->chunks[1].range.length(), 251);
    ASSERT_EQ(plan->chunks[2].range.length(), 251);
    ASSERT_EQ(plan->chunks[3].range.length(), 250);

    ASSERT_EQ(plan->chunks[0].range.start_byte, 0);
    ASSERT_EQ(plan->chunks[3].range.end_byte, 1002);

    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOX_TEST(chunk_planner_respects_no_ranges) {
    auto plan = create_chunk_plan(10000000, 8, false, 1000);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 1);
    ASSERT_FALSE(plan->is_segmented);
    ASSERT_TRUE(verify_chunk_plan(*plan));
}

DEOX_TEST(chunk_planner_zero_bytes) {
    auto plan = create_chunk_plan(0, 4, true, 100);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->chunks.size(), 1);
    ASSERT_TRUE(verify_chunk_plan(*plan));
}
