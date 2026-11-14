// SPDX-License-Identifier: MIT
// End-to-end checkpoint and resume tests.

#include "integration_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <stop_token>
#include <vector>

using namespace deoxdm;
using deoxdm::engine::CheckpointStore;
using deoxdm::engine::DownloadEngine;
using deoxdm::test::count_ranged_gets;
using deoxdm::test::kPayloadSize;
using deoxdm::test::make_config;
using deoxdm::test::make_payload;
using deoxdm::test::MockHttpServer;
using deoxdm::test::read_file;
using deoxdm::test::TempDir;
using deoxdm::test::write_file;

DEOXDM_TEST(integration_cancel_then_resume) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .throttle_bytes = 2048, .throttle_pause = std::chrono::milliseconds(15)});
    const TempDir dir;
    const auto out = dir / "out.bin";
    const auto config = make_config(server.url(), out);

    // First run: stop once a good part has arrived.
    std::stop_source stop;
    std::vector<core::DownloadPhase> phases;
    DownloadEngine first(config);
    first.set_state_listener([&](core::DownloadPhase phase, std::string_view) { phases.push_back(phase); });
    first.set_progress_listener([&](const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState>) {
        if (snapshot.bytes_downloaded >= kPayloadSize / 4) stop.request_stop();
    });

    const auto interrupted = first.run(stop.get_token());
    ASSERT_FALSE(interrupted.has_value());
    ASSERT_EQ(interrupted.error().code, core::ErrorCode::Cancelled);
    ASSERT_EQ(phases.back(), core::DownloadPhase::Cancelled);
    ASSERT_TRUE(CheckpointStore::exists(out));
    ASSERT_EQ(std::filesystem::file_size(out), kPayloadSize); // preallocated, partly filled

    const auto checkpoint = CheckpointStore::load(out);
    ASSERT_TRUE(checkpoint.has_value());
    uint64_t saved = 0;
    for (const auto& chunk : checkpoint->chunks) saved += chunk.bytes_downloaded;
    ASSERT_TRUE(saved >= kPayloadSize / 4);
    ASSERT_TRUE(saved < kPayloadSize);

    // Second run: continue with what is there.
    server.stop_throttling();
    std::optional<engine::DownloadInfo> info;
    DownloadEngine second(config);
    second.set_info_listener([&](const engine::DownloadInfo& i) { info = i; });

    const auto resumed = second.run();
    ASSERT_TRUE(resumed.has_value());
    ASSERT_TRUE(resumed->resumed);
    ASSERT_TRUE(info->resumed);
    ASSERT_EQ(info->bytes_already_present, saved);
    ASSERT_EQ(resumed->total_bytes, kPayloadSize);
    ASSERT_EQ(resumed->bytes_transferred, kPayloadSize - saved); // nothing was fetched twice
    ASSERT_EQ(read_file(out), payload);
    ASSERT_FALSE(CheckpointStore::exists(out));
}

DEOXDM_TEST(integration_resume_refetches_failed_chunks) {
    // Regression: a chunk recorded as "failed" was skipped on resume and the
    // download was reported as complete with a hole in the file.
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "out.bin";

    // State after a failed run: the first half is on disk, the second half
    // was given up on after 1000 bytes.
    const uint64_t half = kPayloadSize / 2;
    std::vector<uint8_t> partial(kPayloadSize, 0);
    std::copy_n(payload.begin(), half + 1000, partial.begin());
    write_file(out, partial);

    const core::ResumeData state{
        .url = server.url(),
        .total_size = kPayloadSize,
        .etag = "\"mock-v1\"",
        .last_modified = "",
        .output_file = "out.bin",
        .chunks = {
            {.index = 0, .range = {0, half - 1}, .bytes_downloaded = half, .status = core::ChunkStatus::Completed},
            {.index = 1, .range = {half, kPayloadSize - 1}, .bytes_downloaded = 1000, .status = core::ChunkStatus::Failed}
        }
    };
    ASSERT_TRUE(CheckpointStore::save(out, state).has_value());

    const auto result = DownloadEngine(make_config(server.url(), out)).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->resumed);
    ASSERT_EQ(read_file(out), payload);
    // Exactly one request, starting where the failed chunk had stopped.
    ASSERT_EQ(server.request_count("GET"), 1u);
    ASSERT_EQ(count_ranged_gets(server, half + 1000), 1u);
}

DEOXDM_TEST(integration_stale_checkpoint_is_discarded) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "out.bin";

    // A checkpoint claiming that everything but the last byte is done, taken
    // from a different version of the file.
    write_file(out, std::vector<uint8_t>(kPayloadSize, 0xEE));
    const core::ResumeData stale{
        .url = server.url(),
        .total_size = kPayloadSize,
        .etag = "\"older-version\"",
        .last_modified = "",
        .output_file = "out.bin",
        .chunks = {{.index = 0, .range = {0, kPayloadSize - 1}, .bytes_downloaded = kPayloadSize - 1, .status = core::ChunkStatus::Pending}}
    };
    ASSERT_TRUE(CheckpointStore::save(out, stale).has_value());

    const auto result = DownloadEngine(make_config(server.url(), out)).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->resumed);
    ASSERT_EQ(read_file(out), payload);
}

DEOXDM_TEST(integration_checkpoint_without_file_is_discarded) {
    // The partial file was deleted but its checkpoint stayed behind.
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "out.bin";

    const core::ResumeData orphan{
        .url = server.url(),
        .total_size = kPayloadSize,
        .etag = "\"mock-v1\"",
        .last_modified = "",
        .output_file = "out.bin",
        .chunks = {{.index = 0, .range = {0, kPayloadSize - 1}, .bytes_downloaded = kPayloadSize, .status = core::ChunkStatus::Completed}}
    };
    ASSERT_TRUE(CheckpointStore::save(out, orphan).has_value());

    const auto result = DownloadEngine(make_config(server.url(), out)).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->resumed);
    ASSERT_EQ(read_file(out), payload);
}

DEOXDM_TEST(integration_no_resume_starts_over) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "out.bin";

    write_file(out, std::vector<uint8_t>(kPayloadSize, 0xEE));
    const core::ResumeData state{
        .url = server.url(),
        .total_size = kPayloadSize,
        .etag = "\"mock-v1\"",
        .last_modified = "",
        .output_file = "out.bin",
        .chunks = {{.index = 0, .range = {0, kPayloadSize - 1}, .bytes_downloaded = kPayloadSize / 2, .status = core::ChunkStatus::Pending}}
    };
    ASSERT_TRUE(CheckpointStore::save(out, state).has_value());

    auto config = make_config(server.url(), out);
    config.resume_enabled = false;
    const auto result = DownloadEngine(config).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->resumed);
    ASSERT_EQ(read_file(out), payload);
    ASSERT_FALSE(CheckpointStore::exists(out));
}

DEOXDM_TEST(integration_overwrite_ignores_existing_checkpoint) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "file.bin";

    write_file(out, std::vector<uint8_t>(kPayloadSize, 0xAA));
    const core::ResumeData stale{
        .url = server.url("/file.bin"),
        .total_size = kPayloadSize,
        .etag = "\"mock-v1\"",
        .last_modified = "",
        .output_file = "file.bin",
        .chunks = {{.index = 0, .range = {0, kPayloadSize - 1}, .bytes_downloaded = kPayloadSize / 2, .status = core::ChunkStatus::Pending}}
    };
    ASSERT_TRUE(CheckpointStore::save(out, stale).has_value());

    auto config = make_config(server.url("/file.bin"), dir.path());
    config.overwrite = true;
    const auto result = DownloadEngine(config).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->resumed);
    ASSERT_EQ(result->output_path, out);
    ASSERT_EQ(read_file(out), payload);
    ASSERT_FALSE(CheckpointStore::exists(out));
}
