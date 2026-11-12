// SPDX-License-Identifier: MIT
//
// Windows implementation of deoxdm/platform/terminal.hpp.
//
// STATUS: experimental, not built in CI. See docs/PLATFORMS.md.

#include "deoxdm/platform/terminal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace deoxdm::platform {

namespace {

HANDLE console(StandardStream stream) noexcept {
    return ::GetStdHandle(stream == StandardStream::Output ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
}

} // namespace

bool is_terminal(StandardStream stream) noexcept {
    DWORD mode = 0;
    return ::GetConsoleMode(console(stream), &mode) != 0;
}

int terminal_columns(StandardStream stream, int fallback) noexcept {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (::GetConsoleScreenBufferInfo(console(stream), &info)) {
        const int columns = info.srWindow.Right - info.srWindow.Left + 1;
        if (columns > 0) {
            return columns;
        }
    }
    return fallback;
}

bool enable_ansi_escapes(StandardStream stream) noexcept {
    const HANDLE handle = console(stream);
    DWORD mode = 0;
    if (!::GetConsoleMode(handle, &mode)) {
        return false;
    }
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
        return true;
    }
    return ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

} // namespace deoxdm::platform
