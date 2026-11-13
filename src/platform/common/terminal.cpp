// SPDX-License-Identifier: MIT
//
// Operating-system independent part of deoxdm/platform/terminal.hpp.

#include "deoxdm/platform/terminal.hpp"

#include <cstdlib>
#include <string_view>

namespace deoxdm::platform {

namespace {

// Empty when the variable is not set.
std::string_view environment(const char* name) noexcept {
#if defined(_MSC_VER)
#pragma warning(suppress : 4996) // getenv is fine: nothing here changes the environment
#endif
    const char* value = std::getenv(name);
    return value != nullptr ? std::string_view(value) : std::string_view();
}

} // namespace

bool supports_color(StandardStream stream) noexcept {
    return environment("NO_COLOR").empty() && environment("TERM") != "dumb" && is_terminal(stream) &&
           enable_ansi_escapes(stream);
}

} // namespace deoxdm::platform
