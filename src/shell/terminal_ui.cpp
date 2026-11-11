#include "deox/shell/terminal_ui.hpp"
#include "deox/core/formatting.hpp"
#include <unistd.h>
#include <iostream>
#include <format>
#include <cmath>

namespace deox::shell {

TerminalUI::TerminalUI(bool quiet) noexcept
    : quiet_(quiet), is_tty_(isatty(STDOUT_FILENO) != 0) {
    last_render_time_ = std::chrono::steady_clock::now();
}

TerminalUI::~TerminalUI() noexcept {
    if (is_tty_ && rendered_lines_ > 0) {
        std::cout << "\n";
    }
}

void TerminalUI::on_state_change(core::DownloadPhase phase, std::string_view message) noexcept {
    if (quiet_) return;

    if (is_tty_ && rendered_lines_ > 0) {
        // Clear previous lines
        for (int i = 0; i < rendered_lines_; ++i) {
            std::cout << "\033[A\033[2K";
        }
        rendered_lines_ = 0;
    }

    const std::string_view phase_str = core::download_phase_to_string(phase);
    std::cout << std::format("\033[1;36m[deox]\033[0m \033[1m{}\033[0m: {}\n", phase_str, message);
    std::cout.flush();
}

void TerminalUI::on_progress(
    const core::ProgressSnapshot& snapshot,
    std::span<const core::ChunkState> chunks
) noexcept {
    if (quiet_) return;

    const auto now = std::chrono::steady_clock::now();
    if (now - last_render_time_ < std::chrono::milliseconds(50) && !snapshot.is_finished()) {
        return; // Rate limit rendering to 20 fps to avoid CPU overhead
    }
    last_render_time_ = now;

    if (is_tty_) {
        render_tty(snapshot, chunks);
    } else {
        render_non_tty(snapshot);
    }
}

void TerminalUI::render_tty(
    const core::ProgressSnapshot& snapshot,
    std::span<const core::ChunkState> chunks
) noexcept {
    // Erase previously rendered lines
    if (rendered_lines_ > 0) {
        for (int i = 0; i < rendered_lines_; ++i) {
            std::cout << "\033[A\033[2K";
        }
    }

    const std::string bar = core::format_progress_bar(snapshot.percentage / 100.0, 24, true);
    const std::string downloaded_str = core::format_bytes(snapshot.bytes_downloaded);
    const std::string total_str = core::format_bytes(snapshot.total_bytes);
    const std::string speed_str = core::format_speed(snapshot.current_speed_bps);
    const std::string eta_str = core::format_duration(snapshot.estimated_remaining);

    // Line 1: Overall progress
    std::cout << std::format("\033[1;32m{}\033[0m | {} / {} | \033[1;33m{}\033[0m | ETA: {}\n",
        bar, downloaded_str, total_str, speed_str, eta_str
    );

    // Line 2: Segment breakdown if multiple chunks
    if (chunks.size() > 1) {
        std::string chunk_bar = std::format("Segments ({}): ", chunks.size());
        for (const auto& ch : chunks) {
            const int pct = static_cast<int>(std::round(ch.progress_ratio() * 100.0));
            if (ch.status == core::ChunkStatus::Completed) {
                chunk_bar += std::format("\033[1;32m[{}:OK]\033[0m ", ch.index + 1);
            } else if (ch.status == core::ChunkStatus::Downloading) {
                chunk_bar += std::format("\033[1;33m[{}:{:2d}%]\033[0m ", ch.index + 1, pct);
            } else {
                chunk_bar += std::format("\033[90m[{}:--]\033[0m ", ch.index + 1);
            }
        }
        std::cout << chunk_bar << "\n";
        rendered_lines_ = 2;
    } else {
        rendered_lines_ = 1;
    }

    std::cout.flush();
}

void TerminalUI::render_non_tty(const core::ProgressSnapshot& snapshot) noexcept {
    // Only log periodically (e.g. every 10% or when complete)
    if (snapshot.percentage - last_logged_percentage_ >= 10.0 || snapshot.is_finished()) {
        const std::string downloaded_str = core::format_bytes(snapshot.bytes_downloaded);
        const std::string total_str = core::format_bytes(snapshot.total_bytes);
        const std::string speed_str = core::format_speed(snapshot.current_speed_bps);
        const std::string eta_str = core::format_duration(snapshot.estimated_remaining);

        std::cout << std::format("[deox] {:5.1f}% | {} / {} | {} | ETA: {}\n",
            snapshot.percentage, downloaded_str, total_str, speed_str, eta_str
        );
        std::cout.flush();
        last_logged_percentage_ = snapshot.percentage;
    }
}

void TerminalUI::finish(bool success, std::string_view message) noexcept {
    if (quiet_) return;

    if (is_tty_ && rendered_lines_ > 0) {
        for (int i = 0; i < rendered_lines_; ++i) {
            std::cout << "\033[A\033[2K";
        }
        rendered_lines_ = 0;
    }

    if (success) {
        std::cout << "\033[1;32m[deox] Download completed successfully.\033[0m\n";
    } else {
        std::cout << std::format("\033[1;31m[deox] Download failed: {}\033[0m\n", message);
    }
    std::cout.flush();
}

} // namespace deox::shell
