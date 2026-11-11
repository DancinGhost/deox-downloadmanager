#include "deox/core/state_machine.hpp"
#include <format>

namespace deox::core {

std::expected<DownloadSessionState, DeoxError> transition_state(
    const DownloadSessionState& current,
    const StateEvent& event
) noexcept {
    // Cancellation is allowed from any non-terminal state
    if (event.type == EventType::CancelRequested) {
        if (current.is_terminal()) {
            return current;
        }
        return DownloadSessionState{DownloadPhase::Cancelled, "Download cancelled by user"};
    }

    // Fatal errors transition to Failed from any non-terminal state
    if (event.type == EventType::ErrorOccurred) {
        return DownloadSessionState{DownloadPhase::Failed, event.message};
    }

    switch (current.phase) {
        case DownloadPhase::Initializing:
            if (event.type == EventType::StartProbing) {
                return DownloadSessionState{DownloadPhase::Probing, event.message};
            }
            break;

        case DownloadPhase::Probing:
            if (event.type == EventType::ProbeCompleted) {
                return DownloadSessionState{DownloadPhase::Planning, event.message};
            }
            break;

        case DownloadPhase::Planning:
            if (event.type == EventType::PlanReady) {
                return DownloadSessionState{DownloadPhase::Downloading, event.message};
            }
            break;

        case DownloadPhase::Downloading:
            if (event.type == EventType::AllChunksFinished) {
                return DownloadSessionState{DownloadPhase::Assembling, event.message};
            }
            break;

        case DownloadPhase::Assembling:
            if (event.type == EventType::AssemblyFinished) {
                return DownloadSessionState{DownloadPhase::Verifying, event.message};
            }
            break;

        case DownloadPhase::Verifying:
            if (event.type == EventType::VerificationPassed) {
                return DownloadSessionState{DownloadPhase::Completed, "Download and verification completed successfully"};
            } else if (event.type == EventType::VerificationFailed) {
                return DownloadSessionState{DownloadPhase::Failed, event.message};
            }
            break;

        case DownloadPhase::Completed:
        case DownloadPhase::Failed:
        case DownloadPhase::Cancelled:
            return std::unexpected(DeoxError{
                ErrorCode::StateCorrupted,
                std::format("Cannot transition from terminal state '{}'", download_phase_to_string(current.phase))
            });
    }

    return std::unexpected(DeoxError{
        ErrorCode::StateCorrupted,
        std::format("Invalid state transition from '{}' with event type {}", download_phase_to_string(current.phase), static_cast<uint32_t>(event.type))
    });
}

} // namespace deox::core
