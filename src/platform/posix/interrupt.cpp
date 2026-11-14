// SPDX-License-Identifier: MIT
//
// POSIX implementation of deoxdm/platform/interrupt.hpp.

#include "deoxdm/platform/interrupt.hpp"
#include "backend.hpp"

#include <csignal>

namespace deoxdm::platform {

namespace {

// A signal handler may do very little: it only raises the flag, or terminates
// immediately if a stop was already requested by an earlier signal.
extern "C" void handle_stop_signal(int signal_number) {
    if (detail::g_interrupt_requested.exchange(true, std::memory_order_relaxed)) {
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        ::sigaction(signal_number, &dfl, nullptr);
        ::raise(signal_number);
    }
}

void install(int signal_number, bool keep_if_ignored) noexcept {
    struct sigaction previous{};
    if (keep_if_ignored && ::sigaction(signal_number, nullptr, &previous) == 0 && previous.sa_handler == SIG_IGN) {
        return;
    }
    struct sigaction action{};
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    // SA_RESETHAND restores the default action after the first delivery, so
    // a second Ctrl-C terminates at once. SA_RESTART keeps blocking calls in
    // other threads from failing with EINTR.
    // (glibc defines SA_RESETHAND as an unsigned constant, sa_flags is int.)
    action.sa_flags = static_cast<int>(SA_RESETHAND | SA_RESTART);
    ::sigaction(signal_number, &action, nullptr);
}

} // namespace

void install_interrupt_handler() noexcept {
    install(SIGINT, false);
    install(SIGTERM, false);
    install(SIGHUP, true); // under nohup, SIGHUP is ignored and must stay so
}

} // namespace deoxdm::platform
