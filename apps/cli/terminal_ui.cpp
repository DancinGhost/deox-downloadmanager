// SPDX-License-Identifier: MIT
#include "terminal_ui.hpp"
#include "deoxdm/core/formatting.hpp"
#include "deoxdm/core/url.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>

#include "deoxdm/platform/terminal.hpp"

namespace deoxdm::cli {

namespace {

constexpr std::string_view kCyan = "1;36";
constexpr std::string_view kGreen = "1;32";
constexpr std::string_view kYellow = "1;33";
constexpr std::string_view kRed = "1;31";
constexpr std::string_view kBold = "1";
constexpr std::string_view kDim = "90";

using platform::StandardStream;

int terminal_width() {
    return platform::terminal_columns(StandardStream::Output);
}

} // namespace

TerminalUI::TerminalUI(bool quiet)
    : quiet_(quiet),
      // The live display redraws itself with cursor movement sequences, so it
      // needs a terminal that understands them.
      is_tty_(platform::is_terminal(StandardStream::Output) && platform::enable_ansi_escapes(StandardStream::Output)),
      use_color_(platform::supports_color(StandardStream::Output)),
      error_color_(platform::supports_color(StandardStream::Error)) {}

std::string TerminalUI::paint(std::string_view color, std::string_view text) const {
    if (!use_color_) {
        return std::string(text);
    }
    return std::format("\033[{}m{}\033[0m", color, text);
}

void TerminalUI::erase_progress() {
    for (; rendered_lines_ > 0; --rendered_lines_) {
        std::cout << "\033[A\033[2K";
    }
}

// URLs, file names and error messages can come from a server or an input file.
// Control characters in them could drive the terminal (escape sequences), so
// everything of that kind goes through printable_text() before it is shown.
void TerminalUI::begin(std::string_view raw_url, size_t position, size_t count) {
    resumable_ = false;
    rendered_lines_ = 0;
    next_logged_percentage_ = 0.0;
    last_plain_log_ = {};

    if (quiet_) return;
    const std::string url = core::printable_text(core::redact_credentials(raw_url));
    if (count > 1) {
        std::cout << std::format("{} ({}/{}) {}\n", paint(kCyan, "[deox-dm]"), position, count, url);
    } else {
        std::cout << std::format("{} {}\n", paint(kCyan, "[deox-dm]"), url);
    }
    std::cout.flush();
}

void TerminalUI::on_state_change(core::DownloadPhase phase, std::string_view message) {
    if (quiet_) return;

    // on_info() describes the plan and finish_success() / finish_failure()
    // the outcome, in more detail than the state messages do.
    if (phase == core::DownloadPhase::Planning || phase == core::DownloadPhase::Completed ||
        phase == core::DownloadPhase::Failed || phase == core::DownloadPhase::Cancelled) {
        return;
    }

    erase_progress();
    std::cout << std::format("{} {}\n", paint(kCyan, "[deox-dm]"), core::printable_text(message));
    std::cout.flush();
}

void TerminalUI::on_info(const engine::DownloadInfo& info) {
    resumable_ = info.resumable;
    if (quiet_) return;

    erase_progress();
    const std::string size = info.total_size ? core::format_bytes(*info.total_size) : std::string("unknown size");
    std::cout << std::format("{} Saving to {} ({})\n",
        paint(kCyan, "[deox-dm]"), paint(kBold, core::printable_text(info.output_path.string())), size);
    if (info.resumed) {
        std::cout << std::format("{} Continuing an earlier download, {} already on disk\n",
            paint(kCyan, "[deox-dm]"), core::format_bytes(info.bytes_already_present));
    }
    std::cout.flush();
}

void TerminalUI::on_progress(const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks) {
    if (quiet_) return;

    if (is_tty_) {
        render_tty(snapshot, chunks);
    } else {
        render_plain(snapshot);
    }
}

void TerminalUI::render_tty(const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks) {
    erase_progress();

    // Keep every line shorter than the terminal: a wrapped line would break
    // the cursor arithmetic that erases the previous frame.
    const int width = terminal_width();
    const std::string downloaded = core::format_bytes(snapshot.bytes_downloaded);
    const std::string speed = core::format_speed(snapshot.current_speed_bps);

    if (snapshot.size_known) {
        const std::string total = core::format_bytes(snapshot.total_bytes);
        const std::string eta = core::format_duration(snapshot.estimated_remaining);
        const std::string tail = std::format(" {} / {} | {} | ETA {}", downloaded, total, speed, eta);
        // "[" + bar + "] 100.0%" needs bar + 9 columns.
        const int bar_width = std::clamp(width - static_cast<int>(tail.size()) - 10, 0, 30);

        if (bar_width >= 5) {
            std::cout << paint(kGreen, core::format_progress_bar(snapshot.percentage / 100.0, static_cast<size_t>(bar_width)));
        } else {
            std::cout << paint(kGreen, std::format("{:5.1f}%", snapshot.percentage));
        }
        std::cout << std::format(" {} / {} | {} | ETA {}\n", downloaded, total, paint(kYellow, speed), eta);
    } else {
        std::cout << std::format("{} downloaded | {}\n", downloaded, paint(kYellow, speed));
    }
    rendered_lines_ = 1;

    // One cell per connection: done, percentage in progress, or waiting.
    if (chunks.size() > 1) {
        std::string line = "Segments:";
        int used = static_cast<int>(line.size());
        size_t shown = 0;
        for (const auto& chunk : chunks) {
            std::string cell;
            std::string_view color = kDim;
            if (chunk.is_complete()) {
                cell = " [done]";
                color = kGreen;
            } else if (chunk.status == core::ChunkStatus::Downloading) {
                cell = std::format(" [{:3.0f}%]", chunk.progress_ratio() * 100.0);
                color = kYellow;
            } else {
                cell = std::format(" [{:3.0f}%]", chunk.progress_ratio() * 100.0);
            }

            const int reserve = (shown + 1 < chunks.size()) ? 8 : 0; // room for " +NN more"
            if (used + static_cast<int>(cell.size()) + reserve >= width) {
                break;
            }
            used += static_cast<int>(cell.size());
            line += paint(color, cell);
            ++shown;
        }
        if (shown < chunks.size()) {
            line += std::format(" +{}", chunks.size() - shown);
        }
        std::cout << line << "\n";
        rendered_lines_ = 2;
    }

    std::cout.flush();
}

void TerminalUI::render_plain(const core::ProgressSnapshot& snapshot) {
    const auto now = std::chrono::steady_clock::now();

    if (snapshot.size_known) {
        // One line per 10% step.
        if (snapshot.percentage < next_logged_percentage_) {
            return;
        }
        next_logged_percentage_ = (std::floor(snapshot.percentage / 10.0) + 1.0) * 10.0;
        std::cout << std::format("[deox-dm] {:5.1f}% | {} / {} | {} | ETA {}\n",
            snapshot.percentage,
            core::format_bytes(snapshot.bytes_downloaded),
            core::format_bytes(snapshot.total_bytes),
            core::format_speed(snapshot.current_speed_bps),
            core::format_duration(snapshot.estimated_remaining));
    } else {
        // No percentage to go by: one line every few seconds.
        if (now - last_plain_log_ < std::chrono::seconds(5)) {
            return;
        }
        std::cout << std::format("[deox-dm] {} downloaded | {}\n",
            core::format_bytes(snapshot.bytes_downloaded),
            core::format_speed(snapshot.current_speed_bps));
    }

    last_plain_log_ = now;
    std::cout.flush();
}

void TerminalUI::finish_success(const core::DownloadResult& result) {
    if (quiet_) return;

    erase_progress();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(result.elapsed);
    std::cout << std::format("{} {} ({}) in {} at {}\n",
        paint(kGreen, "[deox-dm] Done:"),
        core::printable_text(result.output_path.string()),
        core::format_bytes(result.total_bytes),
        seconds.count() > 0 ? core::format_duration(seconds) : std::string("under a second"),
        core::format_speed(result.average_speed_bps));
    if (!result.sha256.empty()) {
        std::cout << std::format("{} SHA-256 verified: {}\n", paint(kGreen, "[deox-dm]"), result.sha256);
    }
    std::cout.flush();
}

void TerminalUI::finish_failure(const core::Error& error) {
    if (!quiet_) {
        erase_progress();
        std::cout.flush();
    }

    const bool was_cancelled = error.code == core::ErrorCode::Cancelled;
    const std::string text = was_cancelled ? std::string("[deox-dm] Cancelled") : std::format("[deox-dm] Failed: {}", core::printable_text(error.message));

    std::cerr << (error_color_ ? std::format("\033[{}m{}\033[0m", was_cancelled ? kYellow : kRed, text) : text) << "\n";
    // A checksum mismatch discards the checkpoint: the data is complete but wrong.
    if (resumable_ && error.code != core::ErrorCode::ChecksumMismatch) {
        std::cerr << "[deox-dm] Progress was saved. Run the same command again to continue.\n";
    }
    std::cerr.flush();
}

} // namespace deoxdm::cli
