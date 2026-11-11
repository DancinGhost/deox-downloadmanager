#pragma once

#include "deox/core/types.hpp"
#include "deox/core/chunk_planner.hpp"
#include "deox/core/progress.hpp"
#include "deox/core/state_machine.hpp"
#include <functional>
#include <stop_token>
#include <expected>
#include <memory>

namespace deox::shell {

using ProgressListener = std::function<void(const core::ProgressSnapshot&, std::span<const core::ChunkState>)>;
using StateListener = std::function<void(core::DownloadPhase, std::string_view)>;

class DownloadEngine {
public:
    explicit DownloadEngine(core::DownloadConfig config) noexcept;
    ~DownloadEngine() noexcept = default;

    void set_progress_listener(ProgressListener listener) noexcept {
        progress_listener_ = std::move(listener);
    }

    void set_state_listener(StateListener listener) noexcept {
        state_listener_ = std::move(listener);
    }

    // Runs the download lifecycle synchronously to completion or cancellation
    [[nodiscard]] std::expected<void, core::DeoxError> run(std::stop_token stop_token = {}) noexcept;

    [[nodiscard]] const core::DownloadConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] const std::filesystem::path& output_path() const noexcept {
        return config_.output_path;
    }

private:
    void notify_state(core::DownloadPhase phase, std::string_view msg) noexcept;

    core::DownloadConfig config_;
    ProgressListener progress_listener_;
    StateListener state_listener_;
};

} // namespace deox::shell
