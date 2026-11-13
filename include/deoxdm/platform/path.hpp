// SPDX-License-Identifier: MIT
#pragma once

// Conversions between UTF-8 text and file system paths.
//
// Text in this program is UTF-8: file names derived from URLs and HTTP
// headers, Qt strings, checkpoint files, log messages. std::filesystem::path
// does not treat a std::string as UTF-8, though: it uses the native narrow
// encoding, which on Windows is the ANSI code page. path::string() converts
// back to that encoding and throws for characters it cannot represent.
//
// Convert between text and paths with these functions instead. On POSIX
// systems they are plain copies.

#include <filesystem>
#include <string>
#include <string_view>

namespace deoxdm::platform {

// The path named by UTF-8 text.
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view utf8);

// The path as UTF-8 text, for display, logging and storage.
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

} // namespace deoxdm::platform
