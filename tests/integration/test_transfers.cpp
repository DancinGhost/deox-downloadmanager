// SPDX-License-Identifier: MIT
// End-to-end transfer tests: segmented, single-stream, unknown size, rate limiting, and size caps.

#include "integration_helpers.hpp"

#include <atomic>
#include <chrono>
#include <optional>
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

DEOXDM_TEST(integration_segmented_download) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "out.bin";

    auto config = make_config(server.url(), out);
    config.expected_sha256 = core::compute_sha256(payload);

    std::vector<core::DownloadPhase> phases;
    std::optional<engine::DownloadInfo> info;
    std::atomic<int> progress_calls{0};

    DownloadEngine engine(config);
    engine.set_state_listener([&](core::DownloadPhase phase, std::string_view) { phases.push_back(phase); });
    engine.set_info_listener([&](const engine::DownloadInfo& i) { info = i; });
    engine.set_progress_listener([&](const core::ProgressSnapshot&, std::span<const core::ChunkState>) { ++progress_calls; });

    const auto result = engine.run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(out), payload);
    ASSERT_EQ(result->output_path, out);
    ASSERT_EQ(result->total_bytes, kPayloadSize);
    ASSERT_EQ(result->bytes_transferred, kPayloadSize);
    ASSERT_EQ(result->sha256, config.expected_sha256);
    ASSERT_FALSE(result->resumed);
    ASSERT_FALSE(CheckpointStore::exists(out)); // nothing left to resume

    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->total_size, kPayloadSize);
    ASSERT_EQ(info->connections, 4u);
    ASSERT_TRUE(info->resumable);
    ASSERT_TRUE(progress_calls > 0);

    const std::vector<core::DownloadPhase> expected_phases = {
        core::DownloadPhase::Probing, core::DownloadPhase::Planning, core::DownloadPhase::Downloading,
        core::DownloadPhase::Assembling, core::DownloadPhase::Verifying, core::DownloadPhase::Completed
    };
    ASSERT_EQ(phases, expected_phases);

    // HEAD answered everything, so the only GETs are the four segments.
    ASSERT_EQ(server.request_count("HEAD"), 1u);
    ASSERT_EQ(server.request_count("GET"), 4u);
    ASSERT_EQ(count_ranged_gets(server), 4u);
}

DEOXDM_TEST(integration_single_connection) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    auto config = make_config(server.url(), dir / "out.bin");
    config.connections = 1;

    const auto result = DownloadEngine(config).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
    ASSERT_EQ(server.request_count("GET"), 1u);
}

DEOXDM_TEST(integration_server_without_range_support) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .support_ranges = false, .advertise_ranges = false});
    const TempDir dir;

    std::optional<engine::DownloadInfo> info;
    DownloadEngine engine(make_config(server.url(), dir / "out.bin"));
    engine.set_info_listener([&](const engine::DownloadInfo& i) { info = i; });

    const auto result = engine.run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
    ASSERT_EQ(info->connections, 1u);
    ASSERT_FALSE(info->resumable);
    ASSERT_FALSE(CheckpointStore::exists(dir / "out.bin"));
}

DEOXDM_TEST(integration_unknown_size) {
    // Regression: without a Content-Length only the first byte was downloaded.
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .support_ranges = false, .advertise_ranges = false, .send_content_length = false});
    const TempDir dir;

    std::optional<engine::DownloadInfo> info;
    bool saw_unknown_size_progress = false;
    DownloadEngine engine(make_config(server.url(), dir / "out.bin"));
    engine.set_info_listener([&](const engine::DownloadInfo& i) { info = i; });
    engine.set_progress_listener([&](const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState>) {
        saw_unknown_size_progress = !snapshot.size_known;
    });

    const auto result = engine.run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
    ASSERT_EQ(result->total_bytes, kPayloadSize);
    ASSERT_FALSE(info->total_size.has_value());
    ASSERT_TRUE(saw_unknown_size_progress);
}

DEOXDM_TEST(integration_head_not_allowed) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .allow_head = false, .advertise_ranges = false});
    const TempDir dir;

    std::optional<engine::DownloadInfo> info;
    DownloadEngine engine(make_config(server.url(), dir / "out.bin"));
    engine.set_info_listener([&](const engine::DownloadInfo& i) { info = i; });

    const auto result = engine.run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
    // The one-byte ranged GET revealed both the size and the range support.
    ASSERT_EQ(info->connections, 4u);
    ASSERT_EQ(info->total_size, kPayloadSize);
}

DEOXDM_TEST(integration_empty_file) {
    MockHttpServer server({.payload = {}});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url(), dir / "empty.bin")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::filesystem::exists(dir / "empty.bin"));
    ASSERT_EQ(std::filesystem::file_size(dir / "empty.bin"), 0u);
    ASSERT_EQ(server.request_count("GET"), 0u);
}

DEOXDM_TEST(integration_rate_limit) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    auto config = make_config(server.url(), dir / "out.bin");
    config.max_speed_bps = 512 * 1024; // 256 KB at 512 KB/s: about half a second

    const auto start = std::chrono::steady_clock::now();
    const auto result = DownloadEngine(config).run();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
    ASSERT_TRUE(elapsed >= std::chrono::milliseconds(400));
    ASSERT_TRUE(elapsed < std::chrono::seconds(10));
}

DEOXDM_TEST(integration_probe_reports_remote_details) {
    const auto payload = make_payload(5000);
    MockHttpServer server({.payload = payload, .etag = "\"abc\"", .content_disposition = "attachment; filename=named.dat"});

    core::DownloadConfig config;
    config.url = server.url("/x");
    const auto info = net::HttpClient::probe(config);
    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->content_length, 5000u);
    ASSERT_TRUE(info->accepts_ranges);
    ASSERT_EQ(info->etag, "\"abc\"");
    ASSERT_EQ(info->suggested_filename, "named.dat");
    ASSERT_EQ(info->http_status, 200u);
}

DEOXDM_TEST(integration_max_file_size_enforced) {
    const auto payload = make_payload(kPayloadSize);
    const TempDir dir;

    // 1. Known Content-Length: rejected immediately after probing, before creating a file.
    MockHttpServer known_server({.payload = payload});
    auto known_cfg = make_config(known_server.url(), dir / "known.bin");
    known_cfg.max_file_size = kPayloadSize - 1;
    const auto known_res = DownloadEngine(known_cfg).run();
    ASSERT_FALSE(known_res.has_value());
    ASSERT_EQ(known_res.error().code, core::ErrorCode::InvalidArgument);
    ASSERT_FALSE(std::filesystem::exists(dir / "known.bin"));
    ASSERT_EQ(known_server.request_count("GET"), 0u);

    // 2. Unknown Content-Length: aborted during streaming as soon as bytes exceed max_file_size.
    MockHttpServer stream_server({.payload = payload, .support_ranges = false, .advertise_ranges = false, .send_content_length = false});
    auto stream_cfg = make_config(stream_server.url(), dir / "stream.bin");
    stream_cfg.max_file_size = 16 * 1024;
    const auto stream_res = DownloadEngine(stream_cfg).run();
    ASSERT_FALSE(stream_res.has_value());
    ASSERT_EQ(stream_res.error().code, core::ErrorCode::InvalidArgument);
    ASSERT_FALSE(std::filesystem::exists(dir / "stream.bin"));
}
