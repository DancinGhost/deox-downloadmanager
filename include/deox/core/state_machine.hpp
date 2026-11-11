#pragma once

#include "deox/core/types.hpp"
#include <string_view>
#include <expected>

namespace deox::core {

enum class DownloadPhase : uint8_t {
    Initializing = 0,
    Probing,
    Planning,
    Downloading,
    Assembling,
    Verifying,
    Completed,
    Failed,
    Cancelled
};

[[nodiscard]] constexpr std::string_view download_phase_to_string(DownloadPhase phase) noexcept {
    switch (phase) {
        case DownloadPhase::Initializing: return "Initializing";
        case DownloadPhase::Probing:      return "Probing remote server";
        case DownloadPhase::Planning:     return "Planning chunks and allocation";
        case DownloadPhase::Downloading:  return "Downloading";
        case DownloadPhase::Assembling:   return "Flushing disk cache";
        case DownloadPhase::Verifying:    return "Verifying checksum";
        case DownloadPhase::Completed:    return "Completed";
        case DownloadPhase::Failed:       return "Failed";
        case DownloadPhase::Cancelled:    return "Cancelled";
    }
    return "Unknown";
}

enum class EventType : uint8_t {
    StartProbing,
    ProbeCompleted,
    PlanReady,
    DownloadStarted,
    AllChunksFinished,
    AssemblyFinished,
    VerificationPassed,
    VerificationFailed,
    ErrorOccurred,
    CancelRequested
};

struct StateEvent {
    EventType type;
    std::string message;
};

struct DownloadSessionState {
    DownloadPhase phase{DownloadPhase::Initializing};
    std::string message;

    [[nodiscard]] constexpr bool is_terminal() const noexcept {
        return phase == DownloadPhase::Completed || 
               phase == DownloadPhase::Failed || 
               phase == DownloadPhase::Cancelled;
    }
};

// Pure state machine transition function
[[nodiscard]] std::expected<DownloadSessionState, DeoxError> transition_state(
    const DownloadSessionState& current,
    const StateEvent& event
) noexcept;

} // namespace deox::core
