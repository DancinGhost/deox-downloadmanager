// SPDX-License-Identifier: MIT
//
// POSIX implementation of deoxdm/platform/terminal.hpp.

#include "deoxdm/platform/terminal.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

namespace deoxdm::platform {

namespace {

int descriptor(StandardStream stream) noexcept {
    return stream == StandardStream::Output ? STDOUT_FILENO : STDERR_FILENO;
}

} // namespace

bool is_terminal(StandardStream stream) noexcept {
    return ::isatty(descriptor(stream)) != 0;
}

int terminal_columns(StandardStream stream, int fallback) noexcept {
    winsize size{};
    if (::ioctl(descriptor(stream), TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    return fallback;
}

bool enable_ansi_escapes(StandardStream stream) noexcept {
    return is_terminal(stream);
}

} // namespace deoxdm::platform
