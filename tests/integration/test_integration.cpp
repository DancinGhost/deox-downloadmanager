// SPDX-License-Identifier: MIT
// End-to-end tests: the real engine downloading from a local HTTP server.

#include "mock_http_server.hpp"
#include "test_framework.hpp"

#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/core/checksum.hpp"
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/engine/download_engine.hpp"
#include "deoxdm/net/http_client.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace deoxdm;
using deoxdm::engine::CheckpointStore;
using deoxdm::engine::DownloadEngine;
using deoxdm::test::make_payload;
using deoxdm::test::MockHttpServer;
using deoxdm::test::MockServerOptions;
using deoxdm::test::TempDir;

namespace {

constexpr size_t kPayloadSize = 256 * 1024;

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, std::span<const uint8_t> content) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
}

// A configuration that splits the small test payload into several chunks.
core::DownloadConfig make_config(const std::string& url, const std::filesystem::path& output) {
    core::DownloadConfig config;
    config.url = url;
    config.output_path = output;
    config.connections = 4;
    config.min_chunk_size = 16 * 1024;
    config.timeout_seconds = 10;
    config.max_retries = 2;
    return config;
}

size_t count_ranged_gets(const MockHttpServer& server, uint64_t min_start = 0) {
    size_t count = 0;
    for (const auto& request : server.requests()) {
        if (request.method == "GET" && request.range_start && *request.range_start >= min_start) ++count;
    }
    return count;
}

} // namespace

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

DEOXDM_TEST(integration_redirect_and_derived_filename) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    // The output is a directory: the name comes from the redirect target.
    const auto result = DownloadEngine(make_config(server.url("/redirect"), dir.path())).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "redirected.bin");
    ASSERT_EQ(read_file(dir / "redirected.bin"), payload);
}

DEOXDM_TEST(integration_content_disposition_filename) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload, .content_disposition = "attachment; filename=\"../../evil name.bin\""});
    const TempDir dir;

    // A directory that does not exist yet, marked by the trailing separator.
    const auto result = DownloadEngine(make_config(server.url("/download?id=42"), dir / "new" / "")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "new" / "evil name.bin"); // the directory part was stripped
    ASSERT_EQ(read_file(dir / "new" / "evil name.bin"), payload);
}

DEOXDM_TEST(integration_existing_file_is_not_overwritten_by_derived_name) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const std::vector<uint8_t> precious = {'k', 'e', 'e', 'p'};
    write_file(dir / "file.bin", precious);

    const auto first = DownloadEngine(make_config(server.url("/file.bin"), dir.path())).run();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->output_path, dir / "file.1.bin");
    ASSERT_EQ(read_file(dir / "file.bin"), precious);

    const auto second = DownloadEngine(make_config(server.url("/file.bin"), dir.path())).run();
    ASSERT_EQ(second->output_path, dir / "file.2.bin");

    auto overwrite = make_config(server.url("/file.bin"), dir.path());
    overwrite.overwrite = true;
    const auto third = DownloadEngine(overwrite).run();
    ASSERT_EQ(third->output_path, dir / "file.bin");
    ASSERT_EQ(read_file(dir / "file.bin"), payload);
}

DEOXDM_TEST(integration_explicit_output_replaces_larger_file) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    write_file(dir / "out.bin", make_payload(100'000)); // older, larger file

    const auto result = DownloadEngine(make_config(server.url(), dir / "out.bin")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
}

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
    // The server advertises range support but always sends the whole file.
    // Writing those bodies at the chunk offsets would corrupt the output.
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload, .support_ranges = false, .advertise_ranges = true});
    const TempDir dir;

    const auto segmented = DownloadEngine(make_config(server.url(), dir / "segmented.bin")).run();
    ASSERT_FALSE(segmented.has_value());
    ASSERT_EQ(segmented.error().code, core::ErrorCode::RangeNotSupported);

    // With a single connection the whole body is exactly what was asked for.
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

DEOXDM_TEST(integration_custom_headers_and_user_agent) {
    MockHttpServer server({.payload = make_payload(1024)});
    const TempDir dir;

    auto config = make_config(server.url(), dir / "out.bin");
    config.headers = {"X-Api-Key: secret-token"};
    config.user_agent = "custom-agent/9.9";
    ASSERT_TRUE(DownloadEngine(config).run().has_value());

    for (const auto& request : server.requests()) {
        ASSERT_TRUE(request.raw.find("X-Api-Key: secret-token\r\n") != std::string::npos);
        ASSERT_TRUE(request.raw.find("User-Agent: custom-agent/9.9\r\n") != std::string::npos);
    }

    MockHttpServer plain_server({.payload = make_payload(1024)});
    ASSERT_TRUE(DownloadEngine(make_config(plain_server.url(), dir / "plain.bin")).run().has_value());
    ASSERT_TRUE(plain_server.requests().front().raw.find("User-Agent: deox-dm/") != std::string::npos);
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

DEOXDM_TEST(integration_custom_headers_stay_with_the_origin) {
    const auto payload = make_payload(64 * 1024);
    MockHttpServer target({.payload = payload});
    // Another port is another origin, just like another host.
    MockHttpServer origin({.payload = payload, .redirect_location = target.url("/file.bin")});
    const TempDir dir;

    auto config = make_config(origin.url("/redirect"), dir / "out.bin");
    config.headers = {"X-Api-Key: secret-token", "Authorization: Bearer secret"};
    const auto result = DownloadEngine(config).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);

    ASSERT_FALSE(origin.requests().empty());
    for (const auto& request : origin.requests()) {
        ASSERT_TRUE(request.raw.find("X-Api-Key: secret-token\r\n") != std::string::npos);
    }
    ASSERT_FALSE(target.requests().empty());
    for (const auto& request : target.requests()) {
        ASSERT_TRUE(request.raw.find("secret") == std::string::npos);
    }
}

DEOXDM_TEST(integration_custom_headers_follow_same_origin_redirects) {
    const auto payload = make_payload(1024);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    auto config = make_config(server.url("/redirect"), dir / "out.bin");
    config.headers = {"X-Api-Key: secret-token"};
    ASSERT_TRUE(DownloadEngine(config).run().has_value());
    for (const auto& request : server.requests()) {
        ASSERT_TRUE(request.raw.find("X-Api-Key: secret-token\r\n") != std::string::npos);
    }
}

DEOXDM_TEST(integration_redirect_loop_is_stopped) {
    MockHttpServer server({.payload = make_payload(1024), .redirect_location = "/redirect"});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url("/redirect"), dir.path())).run();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().message.find("Too many redirects") != std::string::npos);
    ASSERT_TRUE(server.requests().size() <= 2 * 11u); // HEAD and ranged GET, 11 requests each
}

DEOXDM_TEST(integration_redirect_to_another_protocol_is_refused) {
    MockHttpServer server({.payload = make_payload(1024), .redirect_location = "ftp://127.0.0.1/file.bin"});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url("/redirect"), dir.path())).run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::InvalidUrl);
}

DEOXDM_TEST(integration_server_cannot_choose_a_hidden_file_name) {
    MockHttpServer server({.payload = make_payload(1024), .content_disposition = "attachment; filename=\".bashrc\""});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url(), dir.path())).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "bashrc");
    ASSERT_FALSE(std::filesystem::exists(dir / ".bashrc"));
}

DEOXDM_TEST(integration_derived_name_skips_planted_links) {
    const auto payload = make_payload(1024);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    // A dangling link under the name the server will suggest.
    std::filesystem::create_symlink(dir / "elsewhere.bin", dir / "file.bin");

    const auto result = DownloadEngine(make_config(server.url("/file.bin"), dir.path())).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "file.1.bin");
    ASSERT_FALSE(std::filesystem::exists(dir / "elsewhere.bin"));
}
