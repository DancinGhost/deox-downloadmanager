#include "test_framework.hpp"
#include "deox/core/checkpoint.hpp"

using namespace deox::core;

DEOX_TEST(checkpoint_round_trip) {
    std::vector<ChunkState> chunks = {
        ChunkState{.index = 0, .range = ByteRange{0, 499}, .bytes_downloaded = 500, .status = ChunkStatus::Completed},
        ChunkState{.index = 1, .range = ByteRange{500, 999}, .bytes_downloaded = 250, .status = ChunkStatus::Downloading}
    };

    const std::string serialized = serialize_checkpoint(
        "https://example.com/test.iso", 1000, "\"etag-123\"", "Sun, 01 Jan 2026", "test.iso", chunks
    );

    auto deserialized = deserialize_checkpoint(serialized);
    ASSERT_TRUE(deserialized.has_value());
    ASSERT_EQ(deserialized->url, "https://example.com/test.iso");
    ASSERT_EQ(deserialized->total_size, 1000);
    ASSERT_EQ(deserialized->etag, "\"etag-123\"");
    ASSERT_EQ(deserialized->output_file, "test.iso");
    ASSERT_EQ(deserialized->chunks.size(), 2);
    ASSERT_EQ(deserialized->chunks[0].bytes_downloaded, 500);
    ASSERT_EQ(deserialized->chunks[0].status, ChunkStatus::Completed);
    // Notice that downloading status resets to pending on resume
    ASSERT_EQ(deserialized->chunks[1].bytes_downloaded, 250);
    ASSERT_EQ(deserialized->chunks[1].status, ChunkStatus::Pending);
}

DEOX_TEST(checkpoint_validation) {
    ResumeData resume{
        .url = "https://example.com/file.bin",
        .total_size = 5000,
        .etag = "\"abc\"",
        .last_modified = "",
        .output_file = "file.bin",
        .chunks = {}
    };

    ASSERT_TRUE(validate_resume_compatibility(resume, "https://example.com/file.bin", 5000, "\"abc\"").has_value());
    ASSERT_FALSE(validate_resume_compatibility(resume, "https://example.com/other.bin", 5000, "\"abc\"").has_value());
    ASSERT_FALSE(validate_resume_compatibility(resume, "https://example.com/file.bin", 6000, "\"abc\"").has_value());
    ASSERT_FALSE(validate_resume_compatibility(resume, "https://example.com/file.bin", 5000, "\"different\"").has_value());
}
