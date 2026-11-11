#include "deox/shell/engine.hpp"
#include "deox/core/url.hpp"
#include "deox/core/checksum.hpp"
#include "deox/shell/file_writer.hpp"
#include "deox/shell/checkpoint_io.hpp"
#include "deox/shell/network_client.hpp"

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <algorithm>
#include <format>
#include <iostream>

namespace deox::shell {

DownloadEngine::DownloadEngine(core::DownloadConfig config) noexcept
    : config_(std::move(config)) {}

void DownloadEngine::notify_state(core::DownloadPhase phase, std::string_view msg) noexcept {
    if (state_listener_) {
        state_listener_(phase, msg);
    }
}

std::expected<void, core::DeoxError> DownloadEngine::run(std::stop_token stop_token) noexcept {
    // 1. Parse and validate URL
    auto parsed_url = core::parse_url(config_.url);
    if (!parsed_url) {
        return std::unexpected(parsed_url.error());
    }

    // Determine target output path if empty
    if (config_.output_path.empty()) {
        config_.output_path = parsed_url->filename;
    } else if (std::filesystem::is_directory(config_.output_path)) {
        config_.output_path /= parsed_url->filename;
    }

    notify_state(core::DownloadPhase::Probing, std::format("Connecting to {}", parsed_url->host));

    // 2. Check for existing checkpoint to resume
    bool resuming = false;
    core::ResumeData resume_data;
    if (config_.resume_enabled && CheckpointIO::exists(config_.output_path)) {
        auto loaded = CheckpointIO::load(config_.output_path);
        if (loaded) {
            resume_data = std::move(*loaded);
            resuming = true;
        }
    }

    // 3. Probe remote file headers
    auto probe_res = NetworkClient::probe(config_.url, config_);
    if (!probe_res) {
        // If probing fails but we have checkpoint, we might still report probe error
        notify_state(core::DownloadPhase::Failed, probe_res.error().message);
        return std::unexpected(probe_res.error());
    }

    const auto remote_info = *probe_res;
    const uint64_t total_size = remote_info.content_length;

    // Validate resume compatibility
    if (resuming) {
        auto valid_res = core::validate_resume_compatibility(
            resume_data, config_.url, total_size, remote_info.etag
        );
        if (!valid_res) {
            // Incompatible checkpoint: discard and start fresh
            resuming = false;
            CheckpointIO::remove(config_.output_path);
        }
    }

    // 4. Create or restore chunk plan
    notify_state(core::DownloadPhase::Planning, "Creating chunk layout");
    core::ChunkPlan plan;

    if (resuming) {
        plan.total_size = resume_data.total_size;
        plan.chunks = std::move(resume_data.chunks);
        plan.is_segmented = plan.chunks.size() > 1;
    } else {
        auto plan_res = core::create_chunk_plan(
            total_size, config_.connections, remote_info.accepts_ranges, config_.min_chunk_size
        );
        if (!plan_res) {
            return std::unexpected(plan_res.error());
        }
        plan = std::move(*plan_res);

        // Save initial checkpoint
        if (config_.resume_enabled && plan.is_segmented) {
            (void)CheckpointIO::save(
                config_.output_path,
                config_.url,
                total_size,
                remote_info.etag,
                remote_info.last_modified,
                plan.chunks
            );
        }
    }

    // 5. Open and pre-allocate target file
    auto file_res = FileWriter::open(config_.output_path, total_size, !resuming);
    if (!file_res) {
        return std::unexpected(file_res.error());
    }
    FileWriter file_writer = std::move(*file_res);

    // 6. Concurrency and Worker Dispatch
    notify_state(core::DownloadPhase::Downloading, std::format("Downloading with {} connection(s)", plan.chunks.size()));

    std::mutex state_mutex;
    std::vector<core::ChunkState> active_chunks = plan.chunks;
    std::atomic<bool> fatal_error{false};
    core::DeoxError first_error;
    std::stop_source stop_source;

    // Connect stop_source with external stop_token
    std::stop_callback stop_cb(stop_token, [&stop_source]() {
        stop_source.request_stop();
    });

    const uint32_t num_workers = std::min<uint32_t>(config_.connections, static_cast<uint32_t>(plan.chunks.size()));
    const auto start_time = std::chrono::steady_clock::now();
    auto last_progress_time = start_time;
    uint64_t last_progress_bytes = 0;
    double smoothed_speed = 0.0;

    // Background thread for smooth progress calculation & checkpointing
    std::atomic<bool> workers_done{false};
    std::jthread monitor_thread([&](std::stop_token mon_token) {
        auto last_checkpoint_time = std::chrono::steady_clock::now();

        while (!mon_token.stop_requested() && !workers_done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            std::vector<core::ChunkState> snapshot_chunks;
            {
                std::scoped_lock lock(state_mutex);
                snapshot_chunks = active_chunks;
            }

            const auto now = std::chrono::steady_clock::now();
            const auto total_elapsed = now - start_time;
            const auto interval_elapsed = now - last_progress_time;

            uint64_t current_downloaded = 0;
            for (const auto& ch : snapshot_chunks) {
                current_downloaded += ch.bytes_downloaded;
            }

            const double dt = std::chrono::duration<double>(interval_elapsed).count();
            if (dt >= 0.1) {
                const uint64_t delta_bytes = (current_downloaded >= last_progress_bytes) ? 
                    (current_downloaded - last_progress_bytes) : 0;
                const double instant_speed = static_cast<double>(delta_bytes) / dt;
                smoothed_speed = core::update_ema_speed(smoothed_speed, instant_speed);
                last_progress_bytes = current_downloaded;
                last_progress_time = now;
            }

            const auto progress = core::calculate_progress(
                snapshot_chunks, total_size, total_elapsed, smoothed_speed
            );

            if (progress_listener_) {
                progress_listener_(progress, snapshot_chunks);
            }

            // Periodic checkpoint save every 1.5 seconds
            const auto since_checkpoint = now - last_checkpoint_time;
            if (config_.resume_enabled && since_checkpoint >= std::chrono::milliseconds(1500)) {
                (void)CheckpointIO::save(
                    config_.output_path,
                    config_.url,
                    total_size,
                    remote_info.etag,
                    remote_info.last_modified,
                    snapshot_chunks
                );
                last_checkpoint_time = now;
            }
        }
    });

    // Worker Threads
    std::vector<std::jthread> workers;
    workers.reserve(num_workers);

    for (uint32_t w_id = 0; w_id < num_workers; ++w_id) {
        workers.emplace_back([&, w_id](std::stop_token worker_token) {
            (void)w_id;
            while (!worker_token.stop_requested() && !fatal_error.load()) {
                uint32_t chunk_idx = 0;
                core::ByteRange range{};
                bool found = false;

                {
                    std::scoped_lock lock(state_mutex);
                    for (auto& ch : active_chunks) {
                        if (ch.status == core::ChunkStatus::Pending && !ch.is_complete()) {
                            ch.status = core::ChunkStatus::Downloading;
                            chunk_idx = ch.index;
                            range = ch.range;
                            found = true;
                            break;
                        }
                    }
                }

                if (!found) {
                    break; // No more pending chunks
                }

                // Download with retries
                bool chunk_success = false;
                for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
                    if (worker_token.stop_requested() || fatal_error.load()) break;

                    auto sink = [&](uint64_t offset, std::span<const uint8_t> data) -> std::expected<void, core::DeoxError> {
                        auto w_res = file_writer.write_at(offset, data);
                        if (!w_res) return std::unexpected(w_res.error());
                        return {};
                    };

                    auto progress_notifier = [&](uint32_t idx, uint64_t new_bytes) {
                        std::scoped_lock lock(state_mutex);
                        if (idx < active_chunks.size()) {
                            active_chunks[idx].bytes_downloaded += new_bytes;
                        }
                    };

                    // Re-read current downloaded bytes in case earlier attempt was partial
                    uint64_t current_downloaded = 0;
                    {
                        std::scoped_lock lock(state_mutex);
                        current_downloaded = active_chunks[chunk_idx].bytes_downloaded;
                    }

                    auto dl_res = NetworkClient::download_chunk(
                        config_.url,
                        chunk_idx,
                        range,
                        current_downloaded,
                        sink,
                        progress_notifier,
                        worker_token,
                        config_
                    );

                    if (dl_res) {
                        std::scoped_lock lock(state_mutex);
                        active_chunks[chunk_idx].status = core::ChunkStatus::Completed;
                        chunk_success = true;
                        break;
                    }

                    if (dl_res.error().code == core::ErrorCode::Cancelled) {
                        break;
                    }

                    // On retryable error, brief exponential backoff
                    if (attempt < config_.max_retries) {
                        const auto backoff_ms = std::chrono::milliseconds(200 * (1 << attempt));
                        std::this_thread::sleep_for(backoff_ms);
                    } else {
                        // Max retries exceeded
                        std::scoped_lock lock(state_mutex);
                        active_chunks[chunk_idx].status = core::ChunkStatus::Failed;
                        if (!fatal_error.exchange(true)) {
                            first_error = dl_res.error();
                        }
                        stop_source.request_stop();
                    }
                }

                if (!chunk_success) {
                    break;
                }
            }
        });
    }

    // Wait for all worker threads to finish naturally
    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers.clear();
    workers_done.store(true);

    // Stop monitor thread
    monitor_thread.request_stop();

    // Final progress update
    {
        std::scoped_lock lock(state_mutex);
        const auto now = std::chrono::steady_clock::now();
        const auto progress = core::calculate_progress(
            active_chunks, total_size, now - start_time, smoothed_speed
        );
        if (progress_listener_) {
            progress_listener_(progress, active_chunks);
        }
    }

    // Check if interrupted or fatal error
    if (stop_source.stop_requested() || stop_token.stop_requested()) {
        notify_state(core::DownloadPhase::Cancelled, "Download stopped");
        if (config_.resume_enabled) {
            std::scoped_lock lock(state_mutex);
            (void)CheckpointIO::save(
                config_.output_path, config_.url, total_size, remote_info.etag, remote_info.last_modified, active_chunks
            );
        }
        return std::unexpected(core::DeoxError{core::ErrorCode::Cancelled, "Download was cancelled"});
    }

    if (fatal_error.load()) {
        notify_state(core::DownloadPhase::Failed, first_error.message);
        if (config_.resume_enabled) {
            std::scoped_lock lock(state_mutex);
            (void)CheckpointIO::save(
                config_.output_path, config_.url, total_size, remote_info.etag, remote_info.last_modified, active_chunks
            );
        }
        return std::unexpected(first_error);
    }

    // 7. Flush file to disk
    notify_state(core::DownloadPhase::Assembling, "Flushing file buffers to disk");
    auto sync_res = file_writer.sync();
    if (!sync_res) {
        return std::unexpected(sync_res.error());
    }

    // 8. Checksum verification if expected hash is provided
    if (!config_.expected_sha256.empty()) {
        notify_state(core::DownloadPhase::Verifying, "Calculating SHA-256 checksum");
        auto hash_res = file_writer.compute_file_sha256();
        if (!hash_res) {
            return std::unexpected(hash_res.error());
        }

        const std::string computed_hash = *hash_res;
        if (!core::verify_sha256(computed_hash, config_.expected_sha256)) {
            notify_state(core::DownloadPhase::Failed, "SHA-256 checksum mismatch!");
            return std::unexpected(core::DeoxError{
                core::ErrorCode::ChecksumMismatch,
                std::format("Checksum mismatch: expected '{}', got '{}'", config_.expected_sha256, computed_hash)
            });
        }
    }

    // 9. Completed successfully! Clean up checkpoint
    notify_state(core::DownloadPhase::Completed, "Download completed successfully");
    if (config_.resume_enabled) {
        CheckpointIO::remove(config_.output_path);
    }

    return {};
}

} // namespace deox::shell
