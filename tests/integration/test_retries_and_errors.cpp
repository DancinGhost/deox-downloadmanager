// SPDX-License-Identifier: MIT
// End-to-end retry, failure, and server misbehavior tests.

#include "integration_helpers.hpp"

#include <format>
#include <stop_token>
#include <string>
#include <vector>

using namespace deoxdm;
using deoxdm::engine::CheckpointStore;
using deoxdm::engine::DownloadEngine;
using deoxdm::test::kPayloadSize;
using deoxdm::test::make_config;
using deoxdm::test::make_payload;
using deoxdm::test::MockHttpServer;
using deoxdm::test::read_file;
using deoxdm::test::TempDir;

DEOXDM_TEST(integration_http_error_leaves_nothing_behind) {
    // Regression: the body of an error page used to be written into the file.
    MockHttpServer server({.payload = make_payload(1024)});
    const TempDir dir;

    std::vector<core::DownloadPhase> phases;
    DownloadEngine engine(make_config(server.url("/missing"), dir / "out.bin"));
    engine.set_state_listener([&](core::DownloadPhase phase, std::string_view) { phases.push_back(phase); });

    const auto result = engine.run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::HttpError);
    ASSERT_EQ(result.error().http_status, 404u);
    ASSERT_FALSE(std::filesystem::exists(dir / "out.bin"));
    ASSERT_FALSE(CheckpointStore::exists(dir / "out.bin"));
    ASSERT_EQ(phases.back(), core::DownloadPhase::Failed);
    // A missing file is not worth retrying: HEAD, one probing GET, nothing more.
    ASSERT_EQ(server.requests().size(), 2u);
}

DEOXDM_TEST(integration_unreachable_server) {
    uint16_t free_port = 0;
    {
        MockHttpServer server({.payload = {}});
        const std::string url = server.url("");
        free_port = static_cast<uint16_t>(std::stoi(url.substr(url.rfind(':') + 1)));
    } // the server is gone, the port refuses connections

    const TempDir dir;
    auto config = make_config(std::format("http://127.0.0.1:{}/file.bin", free_port), dir / "out.bin");
    config.timeout_seconds = 3;

    const auto result = DownloadEngine(config).run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::NetworkError);
    ASSERT_FALSE(std::filesystem::exists(dir / "out.bin"));
}

DEOXDM_TEST(integration_invalid_requests_fail_before_any_network_use) {
    MockHttpServer server({.payload = make_payload(1024)});
    const TempDir dir;

    auto bad_hash = make_config(server.url(), dir / "out.bin");
    bad_hash.expected_sha256 = "not-a-hash";
    const auto hash_result = DownloadEngine(bad_hash).run();
    ASSERT_FALSE(hash_result.has_value());
    ASSERT_EQ(hash_result.error().code, core::ErrorCode::InvalidArgument);

    const auto url_result = DownloadEngine(make_config("ftp://example.com/file", dir / "out.bin")).run();
    ASSERT_FALSE(url_result.has_value());
    ASSERT_EQ(url_result.error().code, core::ErrorCode::InvalidUrl);

    ASSERT_TRUE(server.requests().empty());
    ASSERT_FALSE(std::filesystem::exists(dir / "out.bin"));
}

DEOXDM_TEST(integration_retries_after_dropped_connections) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .drop_connections = 3, .drop_after = 5000});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url(), dir / "out.bin")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
    // Each retry continued after the 5000 bytes that had already arrived.
    ASSERT_EQ(server.request_count("GET"), 4u + 3u);
    ASSERT_EQ(result->bytes_transferred, kPayloadSize);
}

DEOXDM_TEST(integration_gives_up_when_retries_are_exhausted) {
    const auto payload = make_payload(kPayloadSize);
    // Every connection is cut before delivering a single byte.
    MockHttpServer server({.payload = payload, .drop_connections = 1000, .drop_after = 0});
    const TempDir dir;
    const auto out = dir / "out.bin";

    auto config = make_config(server.url(), out);
    config.connections = 1;
    config.max_retries = 1;

    const auto result = DownloadEngine(config).run();
    ASSERT_FALSE(result.has_value());
    // Regression: a download that failed after its retries was reported as "cancelled".
    ASSERT_EQ(result.error().code, core::ErrorCode::NetworkError);
    ASSERT_EQ(server.request_count("GET"), 2u); // the first attempt and one retry
    ASSERT_TRUE(CheckpointStore::exists(out));     // a later run may still succeed
}

DEOXDM_TEST(integration_retry_without_range_support_starts_over) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .support_ranges = false, .advertise_ranges = false,
                           .drop_connections = 1, .drop_after = 50'000});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url(), dir / "out.bin")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
}

DEOXDM_TEST(integration_server_that_ignores_ranges_is_detected) {
    // The server advertises range support on HEAD but always sends 200 OK with
    // the whole file on GET. HttpClient::download_chunk rejects that on a
    // segment, and DownloadEngine automatically falls back to a single
    // unsegmented download instead of corrupting the file or failing.
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .support_ranges = false, .advertise_ranges = true});
    const TempDir dir;

    const auto config = make_config(server.url(), dir / "segmented.bin");
    const net::ChunkRequest slice_request{
        .range = {1024, 2047},
        .already_downloaded = 0,
        .send_range = true,
        .whole_file = false
    };
    const auto chunk_res = net::HttpClient::download_chunk(
        config, slice_request, [](uint64_t, std::span<const uint8_t>) -> std::expected<void, core::Error> { return {}; }, {});
    ASSERT_FALSE(chunk_res.has_value());
    ASSERT_EQ(chunk_res.error().code, core::ErrorCode::RangeNotSupported);

    const auto segmented = DownloadEngine(config).run();
    ASSERT_TRUE(segmented.has_value());
    ASSERT_EQ(read_file(dir / "segmented.bin"), payload);
    ASSERT_FALSE(CheckpointStore::exists(dir / "segmented.bin"));

    // With a single connection the whole body is also handled cleanly.
    auto single = make_config(server.url(), dir / "single.bin");
    single.connections = 1;
    const auto result = DownloadEngine(single).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "single.bin"), payload);
}

DEOXDM_TEST(integration_checksum_mismatch) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const auto out = dir / "out.bin";

    auto config = make_config(server.url(), out);
    config.expected_sha256 = std::string(64, '0');

    std::vector<core::DownloadPhase> phases;
    DownloadEngine engine(config);
    engine.set_state_listener([&](core::DownloadPhase phase, std::string_view) { phases.push_back(phase); });

    const auto result = engine.run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::ChecksumMismatch);
    ASSERT_EQ(phases.back(), core::DownloadPhase::Failed);
    // The next attempt must download again rather than re-verify the same data.
    ASSERT_FALSE(CheckpointStore::exists(out));
}

DEOXDM_TEST(integration_cancel_during_probe_is_prompt) {
    // Regression: cancellation was only noticed when data arrived.
    const TempDir dir;
    std::stop_source stop;
    stop.request_stop();

    MockHttpServer server({.payload = make_payload(1024)});
    const auto result = DownloadEngine(make_config(server.url(), dir / "out.bin")).run(stop.get_token());
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::Cancelled);
    ASSERT_FALSE(std::filesystem::exists(dir / "out.bin"));
}

DEOXDM_TEST(integration_etag_change_mid_download_is_detected) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .etag = "\"v1\"", .drop_connections = 1, .drop_after = 4096});
    const TempDir dir;
    const auto out = dir / "out.bin";

    auto config = make_config(server.url(), out);
    config.connections = 1;
    config.max_retries = 2;

    DownloadEngine engine(config);
    engine.set_progress_listener([&](const core::ProgressSnapshot& snap, std::span<const core::ChunkState>) {
        if (snap.bytes_downloaded > 0) {
            server.set_etag("\"v2\""); // remote file mutated before the retry
        }
    });

    const auto result = engine.run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::InvalidArgument);
}
