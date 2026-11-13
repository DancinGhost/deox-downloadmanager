// SPDX-License-Identifier: MIT
#pragma once

// Operating-system abstraction for requests to stop the program: Ctrl-C and
// its relatives.
//
//   POSIX    SIGINT (Ctrl-C), SIGTERM (kill, service managers) and SIGHUP
//            (terminal closed). SIGHUP stays ignored if it was ignored at
//            start-up, as under nohup.
//   Windows  Ctrl-C, Ctrl-Break, closing the console window, log-off and
//            shutdown. For the last three Windows ends the process shortly
//            after the request, so only a brief clean-up window remains.
//
// The handler only records the request; the program polls
// interrupt_requested() and stops in an orderly way. A second Ctrl-C ends
// the program immediately, for when the orderly stop hangs.

namespace deoxdm::platform {

// Installs the process-wide handlers. Call once, from main().
void install_interrupt_handler() noexcept;

// Whether a stop has been requested since install_interrupt_handler().
[[nodiscard]] bool interrupt_requested() noexcept;

} // namespace deoxdm::platform
