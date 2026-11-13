// SPDX-License-Identifier: MIT
#pragma once

// Operating-system abstraction for the console the program runs in.

namespace deoxdm::platform {

enum class StandardStream {
    Output, // standard output
    Error   // standard error
};

// Whether the stream is attached to an interactive terminal (not a pipe or
// a file).
[[nodiscard]] bool is_terminal(StandardStream stream) noexcept;

// Width of the terminal in character cells, or `fallback` when unknown.
[[nodiscard]] int terminal_columns(StandardStream stream, int fallback = 80) noexcept;

// Makes the terminal interpret ANSI/VT escape sequences (colors, cursor
// movement). Always true on POSIX terminals; on Windows it switches the
// console into virtual-terminal mode. Returns false if that is impossible.
[[nodiscard]] bool enable_ansi_escapes(StandardStream stream) noexcept;

// Whether colored output should be written to the stream: it is a terminal,
// escape sequences work (enable_ansi_escapes()) and the user has not opted
// out with NO_COLOR (https://no-color.org) or TERM=dumb.
[[nodiscard]] bool supports_color(StandardStream stream) noexcept;

} // namespace deoxdm::platform
