// SPDX-License-Identifier: MIT
#include "deoxdm/core/state_machine.hpp"

#include <format>

namespace deoxdm::core {

std::expected<DownloadSessionState, Error> transition_state(
    const DownloadSessionState& current,
    const StateEvent& event
) noexcept {
    if (current.is_terminal()) {
        return std::unexpected(Error{
            ErrorCode::StateCorrupted,
            std::format("Cannot leave terminal state '{}'", download_phase_to_string(current.phase))
        });
    }

    switch (event.type) {
        case EventType::CancelRequested:
            return DownloadSessionState{DownloadPhase::Cancelled, event.message};
        case EventType::ErrorOccurred:
            return DownloadSessionState{DownloadPhase::Failed, event.message};
        default:
            break;
    }

    const auto next = [&event](DownloadPhase phase) {
        return DownloadSessionState{phase, event.message};
    };

    switch (current.phase) {
        case DownloadPhase::Initializing:
            if (event.type == EventType::StartProbing) return next(DownloadPhase::Probing);
            break;
        case DownloadPhase::Probing:
            if (event.type == EventType::ProbeCompleted) return next(DownloadPhase::Planning);
            break;
        case DownloadPhase::Planning:
            if (event.type == EventType::PlanReady) return next(DownloadPhase::Downloading);
            break;
        case DownloadPhase::Downloading:
            if (event.type == EventType::AllChunksFinished) return next(DownloadPhase::Assembling);
            break;
        case DownloadPhase::Assembling:
            if (event.type == EventType::AssemblyFinished) return next(DownloadPhase::Verifying);
            if (event.type == EventType::VerificationSkipped) return next(DownloadPhase::Completed);
            break;
        case DownloadPhase::Verifying:
            if (event.type == EventType::VerificationPassed) return next(DownloadPhase::Completed);
            if (event.type == EventType::VerificationFailed) return next(DownloadPhase::Failed);
            break;
        case DownloadPhase::Completed:
        case DownloadPhase::Failed:
        case DownloadPhase::Cancelled:
            break; // handled above
    }

    return std::unexpected(Error{
        ErrorCode::StateCorrupted,
        std::format("Event {} is not valid in state '{}'",
            static_cast<uint32_t>(event.type), download_phase_to_string(current.phase))
    });
}

} // namespace deoxdm::core
