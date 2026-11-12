// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

// Project identity. Every user-visible name is defined here so the CLI, the
// desktop application and the network layer agree on them.
namespace deoxdm {

// The build system reads the version from this header; keep the three numbers
// and the string in sync.
inline constexpr int VERSION_MAJOR = 1;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

inline constexpr std::string_view VERSION_STRING = "1.1.0";

// Human-readable product name.
inline constexpr std::string_view PROJECT_NAME = "Deox Download Manager";
inline constexpr std::string_view PROJECT_DESCRIPTION =
    "Segmented, resumable HTTP(S) download manager";
inline constexpr std::string_view PROJECT_URL = "https://github.com/DancinGhost/deox-downloadmanager";

// Executable names.
inline constexpr std::string_view CLI_EXECUTABLE = "deox-dm";
inline constexpr std::string_view GUI_EXECUTABLE = "deox-dm-gui";

// Reverse-DNS application id, used for the desktop entry, the icon and the
// per-user settings/data directories.
inline constexpr std::string_view APPLICATION_ID = "io.github.DancinGhost.DeoxDownloadManager";

// Product token sent in the default User-Agent header ("deox-dm/1.1.0").
inline constexpr std::string_view USER_AGENT_PRODUCT = CLI_EXECUTABLE;

// Suffix of the checkpoint file kept next to an unfinished download.
inline constexpr std::string_view CHECKPOINT_SUFFIX = ".deoxdm";

} // namespace deoxdm
