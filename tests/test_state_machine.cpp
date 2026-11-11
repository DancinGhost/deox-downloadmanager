#include "test_framework.hpp"
#include "deox/core/state_machine.hpp"

using namespace deox::core;

DEOX_TEST(state_machine_happy_path) {
    DownloadSessionState s{DownloadPhase::Initializing, ""};

    auto s1 = transition_state(s, StateEvent{EventType::StartProbing, "Probing"});
    ASSERT_TRUE(s1.has_value());
    ASSERT_EQ(s1->phase, DownloadPhase::Probing);

    auto s2 = transition_state(*s1, StateEvent{EventType::ProbeCompleted, "Probed"});
    ASSERT_TRUE(s2.has_value());
    ASSERT_EQ(s2->phase, DownloadPhase::Planning);

    auto s3 = transition_state(*s2, StateEvent{EventType::PlanReady, "Planned"});
    ASSERT_TRUE(s3.has_value());
    ASSERT_EQ(s3->phase, DownloadPhase::Downloading);

    auto s4 = transition_state(*s3, StateEvent{EventType::AllChunksFinished, "Finished"});
    ASSERT_TRUE(s4.has_value());
    ASSERT_EQ(s4->phase, DownloadPhase::Assembling);

    auto s5 = transition_state(*s4, StateEvent{EventType::AssemblyFinished, "Flushed"});
    ASSERT_TRUE(s5.has_value());
    ASSERT_EQ(s5->phase, DownloadPhase::Verifying);

    auto s6 = transition_state(*s5, StateEvent{EventType::VerificationPassed, "Checksum OK"});
    ASSERT_TRUE(s6.has_value());
    ASSERT_EQ(s6->phase, DownloadPhase::Completed);
    ASSERT_TRUE(s6->is_terminal());
}

DEOX_TEST(state_machine_cancellation_and_error) {
    DownloadSessionState s{DownloadPhase::Downloading, ""};

    auto cancelled = transition_state(s, StateEvent{EventType::CancelRequested, "Cancelled"});
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(cancelled->phase, DownloadPhase::Cancelled);
    ASSERT_TRUE(cancelled->is_terminal());

    // Terminal state cannot transition
    auto err = transition_state(*cancelled, StateEvent{EventType::StartProbing, "Nope"});
    ASSERT_FALSE(err.has_value());
}
