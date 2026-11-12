// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/progress.hpp"
#include "deoxdm/core/state_machine.hpp"
#include "deoxdm/core/types.hpp"
#include "deoxdm/engine/download_engine.hpp"

#include <chrono>
#include <span>
#include <string>
#include <string_view>

namespace deoxdm::cli {

// Console front end for one or more downloads.
//
// On a terminal it draws a live progress display; when the output is piped or
// redirected it prints plain milestone lines instead. Progress goes to
// standard output, errors always go to standard error, even in quiet mode.
class TerminalUI {
public:
    explicit TerminalUI(bool quiet = false);

    // Announces the next download; `position`/`count` number it within a batch.
    void begin(std::string_view url, size_t position, size_t count);

    void on_state_change(core::DownloadPhase phase, std::string_view message);
    void on_info(const engine::DownloadInfo& info);
    void on_progress(const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks);

    void finish_success(const core::DownloadResult& result);
    void finish_failure(const core::Error& error);

private:
    void erase_progress();
    void render_tty(const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks);
    void render_plain(const core::ProgressSnapshot& snapshot);
    [[nodiscard]] std::string paint(std::string_view color, std::string_view text) const;

    bool quiet_{false};
    bool is_tty_{false};
    bool use_color_{false};
    bool error_color_{false};
    bool resumable_{false};
    int rendered_lines_{0};
    double next_logged_percentage_{0.0};
    std::chrono::steady_clock::time_point last_plain_log_{};
};

} // namespace deoxdm::cli
