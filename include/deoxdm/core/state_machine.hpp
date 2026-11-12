// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"
#include <expected>
#include <string>
#include <string_view>

namespace deoxdm::core {

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
        case DownloadPhase::Probing:      return "Connecting";
        case DownloadPhase::Planning:     return "Preparing";
        case DownloadPhase::Downloading:  return "Downloading";
        case DownloadPhase::Assembling:   return "Finishing";
        case DownloadPhase::Verifying:    return "Verifying";
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
    AllChunksFinished,
    AssemblyFinished,     // file is on disk, a checksum will be verified next
    VerificationSkipped,  // file is on disk, no checksum was requested
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

// The download lifecycle as a pure transition function:
//
//   Initializing -> Probing -> Planning -> Downloading -> Assembling -+-> Verifying -> Completed
//                                                                     +-------------> Completed
//
// ErrorOccurred and CancelRequested lead to Failed / Cancelled from every
// non-terminal phase. Terminal phases accept no further events.
[[nodiscard]] std::expected<DownloadSessionState, Error> transition_state(
    const DownloadSessionState& current,
    const StateEvent& event
) noexcept;

} // namespace deoxdm::core
