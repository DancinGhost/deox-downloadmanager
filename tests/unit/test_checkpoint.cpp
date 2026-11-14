// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/checkpoint.hpp"

using namespace deoxdm::core;

namespace {

ResumeData sample_resume_data() {
    return ResumeData{
        .url = "https://example.com/test.iso",
        .total_size = 1000,
        .etag = "\"etag-123\"",
        .last_modified = "Thu, 01 Jan 2026 00:00:00 GMT",
        .output_file = "test.iso",
        .chunks = {
            ChunkState{.index = 0, .range = ByteRange{0, 499}, .bytes_downloaded = 500, .status = ChunkStatus::Completed},
            ChunkState{.index = 1, .range = ByteRange{500, 999}, .bytes_downloaded = 250, .status = ChunkStatus::Downloading}
        }
    };
}

RemoteFileInfo matching_remote() {
    RemoteFileInfo remote;
    remote.content_length = 1000;
    remote.accepts_ranges = true;
    remote.etag = "\"etag-123\"";
    remote.last_modified = "Thu, 01 Jan 2026 00:00:00 GMT";
    return remote;
}

} // namespace

DEOXDM_TEST(checkpoint_round_trip) {
    const auto restored = deserialize_checkpoint(serialize_checkpoint(sample_resume_data()));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->url, "https://example.com/test.iso");
    ASSERT_EQ(restored->total_size, 1000u);
    ASSERT_EQ(restored->etag, "\"etag-123\"");
    ASSERT_EQ(restored->last_modified, "Thu, 01 Jan 2026 00:00:00 GMT");
    ASSERT_EQ(restored->output_file, "test.iso");
    ASSERT_EQ(restored->chunks.size(), 2u);
    ASSERT_EQ(restored->chunks[0].bytes_downloaded, 500u);
    ASSERT_EQ(restored->chunks[0].status, ChunkStatus::Completed);
    // A chunk that was in flight is picked up again from where it stopped.
    ASSERT_EQ(restored->chunks[1].bytes_downloaded, 250u);
    ASSERT_EQ(restored->chunks[1].status, ChunkStatus::Pending);
}

DEOXDM_TEST(checkpoint_unfinished_chunks_are_always_pending) {
    // Regression: a chunk stored as "failed", or as "completed" with bytes
    // missing, used to be skipped on resume, leaving a hole in the file.
    auto data = sample_resume_data();
    data.chunks[0] = ChunkState{.index = 0, .range = ByteRange{0, 499}, .bytes_downloaded = 100, .status = ChunkStatus::Completed};
    data.chunks[1] = ChunkState{.index = 1, .range = ByteRange{500, 999}, .bytes_downloaded = 0, .status = ChunkStatus::Failed};

    const auto restored = deserialize_checkpoint(serialize_checkpoint(data));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->chunks[0].status, ChunkStatus::Pending);
    ASSERT_EQ(restored->chunks[1].status, ChunkStatus::Pending);
}

DEOXDM_TEST(checkpoint_rejects_corrupt_input) {
    const auto corrupt = [](std::string_view text) {
        const auto result = deserialize_checkpoint(text);
        return !result.has_value() && result.error().code == ErrorCode::StateCorrupted;
    };

    ASSERT_TRUE(corrupt(""));
    ASSERT_TRUE(corrupt("garbage"));
    ASSERT_TRUE(corrupt("url=http://x/\n"));                                   // no size
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=abc\n"));
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\n"));                   // no chunks
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,99\n"));     // too few fields
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,99,0,pending,extra\n"));
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,99,0,sleeping\n"));
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunks_count=2\nchunk=0,0,99,0,pending\n")); // truncated
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,49,0,pending\n"));                 // does not cover the file
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,49,0,pending\nchunk=1,40,99,0,pending\n")); // overlap
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=7,0,99,0,pending\n"));                 // index out of order
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,199,0,pending\n"));                // beyond the end
    ASSERT_TRUE(corrupt("url=http://x/\ntotal_size=100\nchunk=0,0,99,500,pending\n"));               // more bytes than the chunk holds

    ASSERT_TRUE(deserialize_checkpoint("# comment\r\nurl=http://x/\r\ntotal_size=100\r\nchunk=0,0,99,10,pending\r\n").has_value());
}

DEOXDM_TEST(checkpoint_validation) {
    const ResumeData resume = sample_resume_data();
    const std::string url = resume.url;

    ASSERT_TRUE(validate_resume_compatibility(resume, url, matching_remote()).has_value());
    ASSERT_FALSE(validate_resume_compatibility(resume, "https://example.com/other.iso", matching_remote()).has_value());

    auto resized = matching_remote();
    resized.content_length = 2000;
    ASSERT_FALSE(validate_resume_compatibility(resume, url, resized).has_value());

    auto unknown_size = matching_remote();
    unknown_size.content_length.reset();
    ASSERT_FALSE(validate_resume_compatibility(resume, url, unknown_size).has_value());

    auto changed = matching_remote();
    changed.etag = "\"different\"";
    ASSERT_FALSE(validate_resume_compatibility(resume, url, changed).has_value());

    auto no_ranges = matching_remote();
    no_ranges.accepts_ranges = false;
    ASSERT_FALSE(validate_resume_compatibility(resume, url, no_ranges).has_value());

    // Without ETags the modification date decides.
    auto dated = matching_remote();
    dated.etag.clear();
    ASSERT_TRUE(validate_resume_compatibility(resume, url, dated).has_value());
    dated.last_modified = "Fri, 02 Jan 2026 00:00:00 GMT";
    ASSERT_FALSE(validate_resume_compatibility(resume, url, dated).has_value());
}

DEOXDM_TEST(checkpoint_sanitizes_newlines_on_serialize) {
    auto data = sample_resume_data();
    data.etag = "\"v1\"\nurl=https://evil.example/payload.bin";
    data.last_modified = "Thu\r\ntotal_size=999999";

    const auto restored = deserialize_checkpoint(serialize_checkpoint(data));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->url, "https://example.com/test.iso");
    ASSERT_EQ(restored->total_size, 1000u);
}
