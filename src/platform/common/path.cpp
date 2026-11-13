// SPDX-License-Identifier: MIT

#include "deoxdm/platform/path.hpp"

namespace deoxdm::platform {

// std::u8string marks the text as UTF-8, so path converts it to the native
// encoding correctly on every platform (UTF-16 on Windows).

std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace deoxdm::platform
