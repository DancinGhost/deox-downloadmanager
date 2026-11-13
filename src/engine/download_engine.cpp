// SPDX-License-Identifier: MIT
#include "deoxdm/engine/download_engine.hpp"
#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/core/checksum.hpp"
#include "deoxdm/core/formatting.hpp"
#include "deoxdm/core/url.hpp"
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/engine/output_file.hpp"
#include "deoxdm/net/http_client.hpp"
#include "deoxdm/engine/rate_limiter.hpp"
#include "deoxdm/platform/path.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace deoxdm::engine {

namespace {

using core::ChunkState;
using core::ChunkStatus;
using core::Error;
using core::ErrorCode;
using core::EventType;
using Clock = std::chrono::steady_clock;

constexpr auto kProgressInterval = std::chrono::milliseconds(200);
constexpr auto kCheckpointInterval = std::chrono::seconds(2);
constexpr auto kMaxBackoff = std::chrono::seconds(10);

std::unexpected<Error> cancelled() {
    return std::unexpected(Error{ErrorCode::Cancelled, "Download cancelled"});
}

uint64_t sum_downloaded(std::span<const ChunkState> chunks) noexcept {
    uint64_t total = 0;
    for (const auto& chunk : chunks) total += chunk.bytes_downloaded;
    return total;
}

} // namespace

// Everything the worker threads of one run share.
struct DownloadEngine::Session {
    std::string url;
    core::RemoteFileInfo remote;
    core::ChunkPlan plan;
    OutputFile file;
    RateLimiter limiter;
    bool checkpointing{false}; // the download can be resumed, keep a checkpoint
    bool send_range{false};    // chunks are fetched with Range requests
    bool resumed{false};
    uint64_t bytes_at_start{0};
    Clock::time_point started_at{};

    std::mutex mutex;                // guards chunks and fatal_error
    std::vector<ChunkState> chunks;
    std::optional<Error> fatal_error;
    std::condition_variable_any wakeup; // interruptible sleeps
    double smoothed_speed{0.0};      // monitor thread only, read after it is joined

    Session(std::string download_url, uint64_t max_speed_bps)
        : url(std::move(download_url)), limiter(max_speed_bps) {}

    [[nodiscard]] std::vector<ChunkState> snapshot() {
        std::scoped_lock lock(mutex);
        return chunks;
    }

    [[nodiscard]] std::optional<uint64_t> total_size() const {
        return plan.size_known ? std::optional<uint64_t>(plan.total_size) : std::nullopt;
    }

    [[nodiscard]] core::ResumeData resume_data(std::span<const ChunkState> current) const {
        return core::ResumeData{
            .url = url,
            .total_size = plan.total_size,
            .etag = remote.etag,
            .last_modified = remote.last_modified,
            .output_file = platform::path_to_utf8(file.path().filename()),
            .chunks = std::vector<ChunkState>(current.begin(), current.end())
        };
    }
};

DownloadEngine::DownloadEngine(core::DownloadConfig config)
    : config_(std::move(config)) {}

void DownloadEngine::advance(EventType event, std::string message) {
    auto next = core::transition_state(state_, core::StateEvent{event, std::move(message)});
    if (!next) {
        return; // an event that does not apply to the current phase changes nothing
    }
    state_ = std::move(*next);
    if (state_listener_) {
        state_listener_(state_.phase, state_.message);
    }
}

std::expected<core::DownloadResult, Error> DownloadEngine::run(std::stop_token stop_token) noexcept {
    state_ = {};

    std::expected<core::DownloadResult, Error> result;
    try {
        result = run_session(stop_token);
    } catch (const std::exception& e) {
        result = std::unexpected(Error{ErrorCode::InternalError, e.what()});
    } catch (...) {
        result = std::unexpected(Error{ErrorCode::InternalError, "Unknown internal error"});
    }

    if (!result) {
        try {
            const Error& error = result.error();
            if (error.code == ErrorCode::Cancelled) {
                advance(EventType::CancelRequested, error.message);
            } else if (error.code == ErrorCode::ChecksumMismatch) {
                advance(EventType::VerificationFailed, error.message);
            } else {
                advance(EventType::ErrorOccurred, error.message);
            }
        } catch (...) {
            // A throwing listener must not turn a reported error into a crash.
        }
    }
    return result;
}

std::expected<DownloadEngine::OutputTarget, Error> DownloadEngine::resolve_output_path(
    const core::RemoteFileInfo& remote,
    std::string_view url_filename,
    bool can_resume
) const {
    const std::string name = !remote.suggested_filename.empty()
        ? remote.suggested_filename
        : std::string(url_filename);

    std::error_code ec;
    std::filesystem::path target;
    bool name_is_derived = false;

    if (config_.output_path.empty()) {
        target = platform::path_from_utf8(name);
        name_is_derived = true;
    } else if (std::filesystem::is_directory(config_.output_path, ec) || !config_.output_path.has_filename()) {
        target = config_.output_path / platform::path_from_utf8(name);
        name_is_derived = true;
    } else {
        target = config_.output_path;
    }

    // A name the user did not choose must never silently replace a file. Pick
    // "name.1.ext", "name.2.ext", ... instead, unless one of those names holds
    // an unfinished download of this very URL, which is then continued.
    if (name_is_derived && !config_.overwrite) {
        const auto is_our_unfinished_download = [&](const std::filesystem::path& candidate) {
            if (!can_resume || !CheckpointStore::exists(candidate)) return false;
            const auto checkpoint = CheckpointStore::load(candidate);
            return checkpoint && checkpoint->url == config_.url;
        };
        // A dangling symbolic link counts as taken: writing through it would
        // create a file somewhere else.
        const auto is_free = [&](const std::filesystem::path& candidate) {
            return !std::filesystem::exists(std::filesystem::symlink_status(candidate, ec)) &&
                   !CheckpointStore::exists(candidate);
        };

        const std::filesystem::path directory = target.parent_path();
        bool found = false;
        for (uint32_t n = 0; n < 10000 && !found; ++n) {
            const std::filesystem::path candidate = (n == 0)
                ? target
                : directory / platform::path_from_utf8(core::numbered_filename(name, n));
            if (is_our_unfinished_download(candidate) || is_free(candidate)) {
                target = candidate;
                found = true;
            }
        }
        if (!found) {
            return std::unexpected(Error{ErrorCode::FileSystemError,
                std::format("Cannot find an unused file name for '{}'", platform::path_to_utf8(target))});
        }
    }

    if (std::filesystem::is_directory(target, ec)) {
        return std::unexpected(Error{ErrorCode::FileSystemError,
            std::format("'{}' is a directory", platform::path_to_utf8(target))});
    }

    return OutputTarget{std::move(target), name_is_derived};
}

std::expected<core::DownloadResult, Error> DownloadEngine::run_session(const std::stop_token& stop_token) {
    // 1. Validate the request before touching the network
    const auto url = core::parse_url(config_.url);
    if (!url) {
        return std::unexpected(url.error());
    }
    if (!config_.expected_sha256.empty() && !core::is_valid_sha256_hex(config_.expected_sha256)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "The expected SHA-256 must be 64 hexadecimal characters"});
    }
    config_.connections = std::clamp<uint32_t>(config_.connections, 1, core::kMaxConnections);

    // 2. Ask the server what it is offering
    advance(EventType::StartProbing, std::format("Connecting to {}", url->host));

    auto probe = net::HttpClient::probe(config_, stop_token);
    if (!probe) {
        return std::unexpected(probe.error());
    }

    Session session(config_.url, config_.max_speed_bps);
    session.remote = std::move(*probe);
    const core::RemoteFileInfo& remote = session.remote;

    advance(EventType::ProbeCompleted, "Preparing download");

    // 3. Decide where the file goes and whether an earlier run can be continued
    const bool can_resume = config_.resume_enabled && remote.accepts_ranges &&
                            remote.content_length && *remote.content_length > 0;

    const auto output_target = resolve_output_path(remote, url->filename, can_resume);
    if (!output_target) {
        return std::unexpected(output_target.error());
    }
    const std::filesystem::path& output_path = output_target->path;
    const bool server_chosen_name = output_target->server_chosen_name;

    if (can_resume && CheckpointStore::exists(output_path)) {
        auto checkpoint = CheckpointStore::load(output_path);
        if (checkpoint && core::validate_resume_compatibility(*checkpoint, config_.url, remote)) {
            if (auto file = OutputFile::open_existing(output_path, checkpoint->total_size, server_chosen_name)) {
                session.file = std::move(*file);
                session.plan.total_size = checkpoint->total_size;
                session.plan.chunks = std::move(checkpoint->chunks);
                session.plan.is_segmented = session.plan.chunks.size() > 1;
                session.resumed = true;
            }
        }
    }

    if (!session.resumed) {
        // Starting over: a checkpoint that may be lying around no longer
        // describes the file and must not be picked up by a later run.
        CheckpointStore::remove(output_path);

        if (remote.content_length) {
            auto plan = core::create_chunk_plan(
                *remote.content_length, config_.connections, remote.accepts_ranges, config_.min_chunk_size);
            if (!plan) {
                return std::unexpected(plan.error());
            }
            session.plan = std::move(*plan);
        } else {
            session.plan = core::create_unknown_size_plan();
        }

        auto file = OutputFile::create(output_path, remote.content_length, server_chosen_name);
        if (!file) {
            return std::unexpected(file.error());
        }
        session.file = std::move(*file);
    }

    session.chunks = session.plan.chunks;
    session.checkpointing = can_resume;
    session.send_range = remote.accepts_ranges && session.plan.size_known;
    session.bytes_at_start = sum_downloaded(session.chunks);
    session.started_at = Clock::now();

    const auto pending_chunks = static_cast<uint32_t>(std::ranges::count_if(
        session.chunks, [](const ChunkState& chunk) { return !chunk.is_complete(); }));
    const uint32_t workers = std::min(config_.connections, pending_chunks);

    const auto save_checkpoint = [&](std::span<const ChunkState> chunks) {
        // Best effort: a checkpoint that cannot be written only costs resumability.
        (void)CheckpointStore::save(output_path, session.resume_data(chunks));
    };

    if (session.checkpointing && !session.resumed) {
        save_checkpoint(session.chunks);
    }

    if (info_listener_) {
        info_listener_(DownloadInfo{
            .output_path = output_path,
            .total_size = session.total_size(),
            .connections = std::max<uint32_t>(workers, 1),
            .resumable = session.checkpointing,
            .resumed = session.resumed,
            .bytes_already_present = session.bytes_at_start
        });
    }

    // 4. Transfer
    advance(EventType::PlanReady, std::format("{} with {} connection{}",
        session.resumed ? "Resuming" : "Downloading", std::max<uint32_t>(workers, 1), workers > 1 ? "s" : ""));

    const auto transferred = transfer(session, stop_token);

    if (!transferred) {
        if (session.checkpointing) {
            save_checkpoint(session.snapshot());
        } else {
            // Without range support there is nothing to continue from; do not
            // leave a truncated file behind that looks like the real thing.
            session.file.close();
            std::error_code ec;
            std::filesystem::remove(output_path, ec);
        }
        return std::unexpected(transferred.error());
    }

    // 5. Make it durable
    advance(EventType::AllChunksFinished, "Writing to disk");

    const uint64_t final_size = sum_downloaded(session.chunks);
    if (!session.plan.size_known) {
        // A retried stream may have ended up shorter than an earlier attempt.
        if (auto resized = session.file.truncate(final_size); !resized) {
            return std::unexpected(resized.error());
        }
    }
    if (auto synced = session.file.sync(); !synced) {
        return std::unexpected(synced.error());
    }

    // 6. Verify
    core::DownloadResult result;

    if (!config_.expected_sha256.empty()) {
        advance(EventType::AssemblyFinished, "Verifying SHA-256 checksum");

        auto digest = session.file.compute_sha256(stop_token);
        if (!digest) {
            return std::unexpected(digest.error());
        }
        if (!core::verify_sha256(*digest, config_.expected_sha256)) {
            // The data is complete but wrong. Drop the checkpoint so that the
            // next attempt downloads everything again instead of re-verifying
            // the same bad file forever.
            CheckpointStore::remove(output_path);
            return std::unexpected(Error{ErrorCode::ChecksumMismatch,
                std::format("SHA-256 mismatch: expected {}, got {}", config_.expected_sha256, *digest)});
        }
        result.sha256 = std::move(*digest);
    }

    session.file.close();
    CheckpointStore::remove(output_path);

    const auto elapsed = Clock::now() - session.started_at;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    result.output_path = output_path;
    result.total_bytes = final_size;
    result.bytes_transferred = final_size - session.bytes_at_start;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    result.average_speed_bps = seconds > 0.001 ? static_cast<double>(result.bytes_transferred) / seconds : 0.0;
    result.resumed = session.resumed;

    const std::string done_message = std::format("Saved {}", platform::path_to_utf8(output_path));
    advance(config_.expected_sha256.empty() ? EventType::VerificationSkipped : EventType::VerificationPassed, done_message);

    return result;
}

std::expected<void, Error> DownloadEngine::transfer(Session& session, const std::stop_token& stop_token) {
    // Workers stop when the caller asks for it or when one of them hits an
    // error that makes continuing pointless.
    std::stop_source abort_source;
    const std::stop_callback forward_stop(stop_token, [&abort_source] { abort_source.request_stop(); });
    const std::stop_token abort_token = abort_source.get_token();

    const auto report_progress = [&](std::span<const ChunkState> chunks) {
        if (!progress_listener_) return;
        progress_listener_(
            core::calculate_progress(chunks, session.total_size(), Clock::now() - session.started_at,
                                     session.smoothed_speed, session.bytes_at_start),
            chunks);
    };

    // Reports progress a few times per second and keeps the checkpoint fresh.
    std::jthread monitor([&](const std::stop_token& monitor_token) {
        auto last_sample_time = session.started_at;
        uint64_t last_sample_bytes = session.bytes_at_start;
        auto last_checkpoint = Clock::now();

        while (true) {
            std::vector<ChunkState> chunks;
            {
                std::unique_lock lock(session.mutex);
                session.wakeup.wait_for(lock, monitor_token, kProgressInterval, [] { return false; });
                if (monitor_token.stop_requested()) {
                    return;
                }
                chunks = session.chunks;
            }

            const auto now = Clock::now();
            const uint64_t downloaded = sum_downloaded(chunks);
            const double dt = std::chrono::duration<double>(now - last_sample_time).count();
            if (dt > 0.0) {
                const uint64_t delta = downloaded > last_sample_bytes ? downloaded - last_sample_bytes : 0;
                session.smoothed_speed = core::update_ema_speed(session.smoothed_speed, static_cast<double>(delta) / dt, 0.2);
                last_sample_time = now;
                last_sample_bytes = downloaded;
            }

            report_progress(chunks);

            if (session.checkpointing && now - last_checkpoint >= kCheckpointInterval) {
                // Flush first: the checkpoint must never claim bytes that a
                // power loss could still take away.
                if (session.file.sync()) {
                    (void)CheckpointStore::save(session.file.path(), session.resume_data(chunks));
                }
                last_checkpoint = Clock::now();
            }
        }
    });

    const auto worker = [&] {
        while (!abort_token.stop_requested()) {
            std::optional<uint32_t> claimed;
            {
                std::scoped_lock lock(session.mutex);
                for (auto& chunk : session.chunks) {
                    if (chunk.status == ChunkStatus::Pending && !chunk.is_complete()) {
                        chunk.status = ChunkStatus::Downloading;
                        claimed = chunk.index;
                        break;
                    }
                }
            }
            if (!claimed) {
                return; // nothing left to do
            }

            std::expected<void, Error> outcome;
            try {
                outcome = download_chunk_with_retries(session, *claimed, abort_token);
            } catch (const std::exception& e) {
                outcome = std::unexpected(Error{ErrorCode::InternalError, e.what()});
            }

            {
                std::scoped_lock lock(session.mutex);
                ChunkState& chunk = session.chunks[*claimed];
                if (outcome) {
                    chunk.status = ChunkStatus::Completed;
                    continue;
                }
                if (outcome.error().code == ErrorCode::Cancelled) {
                    chunk.status = ChunkStatus::Pending;
                    return;
                }
                chunk.status = ChunkStatus::Failed;
                if (!session.fatal_error) {
                    session.fatal_error = std::move(outcome.error());
                }
            }
            abort_source.request_stop(); // one chunk is lost for good, stop the others
            return;
        }
    };

    const auto pending = static_cast<uint32_t>(std::ranges::count_if(
        session.chunks, [](const ChunkState& chunk) { return !chunk.is_complete(); }));
    const uint32_t worker_count = std::min(config_.connections, pending);

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (uint32_t i = 0; i < worker_count; ++i) {
            workers.emplace_back(worker);
        }
    } // joins all workers

    monitor.request_stop();
    monitor.join();

    // From here on this thread is the only one touching the session.
    report_progress(session.chunks);

    if (session.fatal_error) {
        return std::unexpected(*session.fatal_error);
    }
    if (stop_token.stop_requested()) {
        return cancelled();
    }
    if (!std::ranges::all_of(session.chunks, [](const ChunkState& chunk) { return chunk.is_complete(); })) {
        return std::unexpected(Error{ErrorCode::InternalError, "Download ended with unfinished chunks"});
    }
    return {};
}

std::expected<void, Error> DownloadEngine::download_chunk_with_retries(
    Session& session,
    uint32_t chunk_index,
    const std::stop_token& stop_token
) {
    const bool whole_file = session.chunks.size() == 1;
    bool send_range = session.send_range;
    uint32_t failures = 0;

    const auto bytes_on_disk = [&] {
        std::scoped_lock lock(session.mutex);
        return session.chunks[chunk_index].bytes_downloaded;
    };
    const auto restart_chunk = [&] {
        std::scoped_lock lock(session.mutex);
        session.chunks[chunk_index].bytes_downloaded = 0;
    };

    const net::ChunkDataSink sink = [&](uint64_t offset, std::span<const uint8_t> data) -> std::expected<void, Error> {
        if (!session.limiter.acquire(data.size(), stop_token)) {
            return cancelled();
        }
        if (auto written = session.file.write_at(offset, data); !written) {
            return written;
        }
        std::scoped_lock lock(session.mutex);
        session.chunks[chunk_index].bytes_downloaded += data.size();
        return {};
    };

    while (true) {
        const uint64_t before = bytes_on_disk();
        const net::ChunkRequest request{
            .range = session.plan.chunks[chunk_index].range,
            .already_downloaded = before,
            .send_range = send_range,
            .whole_file = whole_file
        };

        auto outcome = net::HttpClient::download_chunk(config_, request, sink, stop_token);
        if (outcome) {
            return {};
        }
        if (stop_token.stop_requested() || outcome.error().code == ErrorCode::Cancelled) {
            return cancelled();
        }

        Error error = std::move(outcome.error());

        // A server that stops honouring ranges can still deliver the file in
        // one piece, as long as nothing else depends on ranges.
        if (error.code == ErrorCode::RangeNotSupported && whole_file && send_range) {
            send_range = false;
            restart_chunk();
            continue;
        }

        if (!core::is_retryable(error)) {
            return std::unexpected(std::move(error));
        }

        // Only consecutive failures without any progress count against the limit.
        if (bytes_on_disk() > before) {
            failures = 0;
        }
        if (++failures > config_.max_retries) {
            if (config_.max_retries > 0) {
                error.message += std::format(" (gave up after {} attempts)", failures);
            }
            return std::unexpected(std::move(error));
        }

        if (!send_range) {
            restart_chunk(); // without ranges a retry has to start over
        }

        const auto backoff = std::min<std::chrono::milliseconds>(
            std::chrono::milliseconds(500) * (1u << std::min<uint32_t>(failures - 1, 5)), kMaxBackoff);
        {
            std::unique_lock lock(session.mutex);
            session.wakeup.wait_for(lock, stop_token, backoff, [] { return false; });
        }
        if (stop_token.stop_requested()) {
            return cancelled();
        }
    }
}

} // namespace deoxdm::engine
