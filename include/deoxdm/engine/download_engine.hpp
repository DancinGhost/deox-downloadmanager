// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/chunk_planner.hpp"
#include "deoxdm/core/progress.hpp"
#include "deoxdm/core/state_machine.hpp"
#include "deoxdm/core/types.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace deoxdm::engine {

// What is known about a download once the server has been contacted and the
// output file has been chosen.
struct DownloadInfo {
    std::filesystem::path output_path;
    std::optional<uint64_t> total_size; // nullopt: the server does not report a size
    uint32_t connections{1};
    bool resumable{false};              // an interrupted run can be continued later
    bool resumed{false};                // this run continues an earlier one
    uint64_t bytes_already_present{0};
};

using StateListener = std::function<void(core::DownloadPhase, std::string_view message)>;
using InfoListener = std::function<void(const DownloadInfo&)>;
using ProgressListener = std::function<void(const core::ProgressSnapshot&, std::span<const core::ChunkState>)>;

// Runs one download from start to finish.
//
// Listeners are invoked from the thread that calls run() and from an internal
// monitoring thread, but never concurrently with each other, so a listener
// needs no locking of its own. It must not block for long.
class DownloadEngine {
public:
    explicit DownloadEngine(core::DownloadConfig config);

    void set_state_listener(StateListener listener) {
        state_listener_ = std::move(listener);
    }

    void set_info_listener(InfoListener listener) {
        info_listener_ = std::move(listener);
    }

    void set_progress_listener(ProgressListener listener) {
        progress_listener_ = std::move(listener);
    }

    // Blocks until the download completes, fails, or `stop_token` is
    // triggered. A stopped or failed download of a resumable file leaves a
    // checkpoint behind; calling run() again (also from a new process)
    // continues where it left off.
    [[nodiscard]] std::expected<core::DownloadResult, core::Error> run(std::stop_token stop_token = {}) noexcept;

    [[nodiscard]] const core::DownloadConfig& config() const noexcept {
        return config_;
    }

private:
    struct Session;

    [[nodiscard]] std::expected<core::DownloadResult, core::Error> run_session(const std::stop_token& stop_token);
    [[nodiscard]] std::expected<std::filesystem::path, core::Error> resolve_output_path(
        const core::RemoteFileInfo& remote, std::string_view url_filename, bool can_resume) const;
    [[nodiscard]] std::expected<void, core::Error> transfer(Session& session, const std::stop_token& stop_token);
    [[nodiscard]] std::expected<void, core::Error> download_chunk_with_retries(
        Session& session, uint32_t chunk_index, const std::stop_token& stop_token);

    void advance(core::EventType event, std::string message);

    core::DownloadConfig config_;
    core::DownloadSessionState state_;
    StateListener state_listener_;
    InfoListener info_listener_;
    ProgressListener progress_listener_;
};

} // namespace deoxdm::engine
