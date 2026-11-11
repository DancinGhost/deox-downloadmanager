#pragma once

#include "deox/core/types.hpp"
#include "deox/core/progress.hpp"
#include "deox/core/state_machine.hpp"
#include <span>
#include <chrono>

namespace deox::shell {

class TerminalUI {
public:
    explicit TerminalUI(bool quiet = false) noexcept;
    ~TerminalUI() noexcept;

    void on_state_change(core::DownloadPhase phase, std::string_view message) noexcept;
    void on_progress(const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks) noexcept;

    void finish(bool success, std::string_view message = "") noexcept;

    [[nodiscard]] bool is_tty() const noexcept {
        return is_tty_;
    }

private:
    void render_tty(const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks) noexcept;
    void render_non_tty(const core::ProgressSnapshot& snapshot) noexcept;

    bool quiet_{false};
    bool is_tty_{false};
    bool header_rendered_{false};
    int rendered_lines_{0};
    double last_logged_percentage_{-10.0};
    std::chrono::steady_clock::time_point last_render_time_{};
};

} // namespace deox::shell
