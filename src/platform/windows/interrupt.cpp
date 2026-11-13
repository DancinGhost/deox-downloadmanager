// SPDX-License-Identifier: MIT
//
// Windows implementation of deoxdm/platform/interrupt.hpp.
//
// STATUS: experimental, not built in CI. See docs/PLATFORMS.md.

#include "deoxdm/platform/interrupt.hpp"
#include "backend.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace deoxdm::platform {

namespace {

// Runs on a thread Windows creates for the event.
BOOL WINAPI handle_console_event(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        // The second request falls through to the default handler, which
        // ends the process.
        return detail::g_interrupt_requested.exchange(true, std::memory_order_relaxed) ? FALSE : TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        // Windows ends the process as soon as this handler returns. Waiting
        // a moment gives the download threads a chance to save checkpoints.
        detail::g_interrupt_requested.store(true, std::memory_order_relaxed);
        ::Sleep(2000);
        return TRUE;
    default:
        return FALSE;
    }
}

} // namespace

void install_interrupt_handler() noexcept {
    ::SetConsoleCtrlHandler(handle_console_event, TRUE);
}

} // namespace deoxdm::platform
