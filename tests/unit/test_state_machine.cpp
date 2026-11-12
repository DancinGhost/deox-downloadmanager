// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/state_machine.hpp"

#include <initializer_list>

using namespace deoxdm::core;

namespace {

// Applies the events in order and returns the final phase, or nullopt if any
// of them is rejected.
std::optional<DownloadPhase> run_events(std::initializer_list<EventType> events) {
    DownloadSessionState state;
    for (const EventType event : events) {
        auto next = transition_state(state, StateEvent{event, "message"});
        if (!next) return std::nullopt;
        state = *next;
    }
    return state.phase;
}

} // namespace

DEOXDM_TEST(state_machine_happy_path_with_verification) {
    ASSERT_EQ(run_events({EventType::StartProbing}), DownloadPhase::Probing);
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ProbeCompleted}), DownloadPhase::Planning);
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ProbeCompleted, EventType::PlanReady}), DownloadPhase::Downloading);
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ProbeCompleted, EventType::PlanReady,
                          EventType::AllChunksFinished, EventType::AssemblyFinished, EventType::VerificationPassed}),
              DownloadPhase::Completed);
}

DEOXDM_TEST(state_machine_happy_path_without_verification) {
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ProbeCompleted, EventType::PlanReady,
                          EventType::AllChunksFinished, EventType::VerificationSkipped}),
              DownloadPhase::Completed);
}

DEOXDM_TEST(state_machine_verification_failure) {
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ProbeCompleted, EventType::PlanReady,
                          EventType::AllChunksFinished, EventType::AssemblyFinished, EventType::VerificationFailed}),
              DownloadPhase::Failed);
}

DEOXDM_TEST(state_machine_rejects_out_of_order_events) {
    ASSERT_FALSE(run_events({EventType::PlanReady}).has_value());
    ASSERT_FALSE(run_events({EventType::StartProbing, EventType::StartProbing}).has_value());
    ASSERT_FALSE(run_events({EventType::StartProbing, EventType::AllChunksFinished}).has_value());
    ASSERT_FALSE(run_events({EventType::StartProbing, EventType::ProbeCompleted, EventType::PlanReady,
                             EventType::VerificationPassed}).has_value());
}

DEOXDM_TEST(state_machine_cancel_and_error_from_any_active_phase) {
    ASSERT_EQ(run_events({EventType::CancelRequested}), DownloadPhase::Cancelled);
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ErrorOccurred}), DownloadPhase::Failed);
    ASSERT_EQ(run_events({EventType::StartProbing, EventType::ProbeCompleted, EventType::PlanReady,
                          EventType::CancelRequested}), DownloadPhase::Cancelled);

    const auto failed = transition_state(DownloadSessionState{DownloadPhase::Downloading, ""},
                                         StateEvent{EventType::ErrorOccurred, "disk full"});
    ASSERT_TRUE(failed.has_value());
    ASSERT_EQ(failed->message, "disk full");
    ASSERT_TRUE(failed->is_terminal());
}

DEOXDM_TEST(state_machine_terminal_states_are_final) {
    for (const DownloadPhase phase : {DownloadPhase::Completed, DownloadPhase::Failed, DownloadPhase::Cancelled}) {
        const DownloadSessionState state{phase, ""};
        ASSERT_TRUE(state.is_terminal());
        // Regression: an error event used to turn a completed download into a failed one.
        ASSERT_FALSE(transition_state(state, StateEvent{EventType::ErrorOccurred, ""}).has_value());
        ASSERT_FALSE(transition_state(state, StateEvent{EventType::CancelRequested, ""}).has_value());
        ASSERT_FALSE(transition_state(state, StateEvent{EventType::StartProbing, ""}).has_value());
    }
}
