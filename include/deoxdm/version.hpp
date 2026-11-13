// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

// There are no numbered releases yet: every build is a canary, identified by
// the commit it was built from. The build system defines DEOXDM_BUILD_ID
// (e.g. "canary-1a2b3c4"); a build without it is plain "canary".
#ifndef DEOXDM_BUILD_ID
#define DEOXDM_BUILD_ID "canary"
#endif

// Project identity. Every user-visible name is defined here so the CLI, the
// desktop application and the network layer agree on them.
namespace deoxdm {

// Shown by --version, in the About dialog and in the User-Agent header.
inline constexpr std::string_view VERSION_STRING = DEOXDM_BUILD_ID;

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

// Product token sent in the default User-Agent header ("deox-dm/canary-1a2b3c4").
inline constexpr std::string_view USER_AGENT_PRODUCT = CLI_EXECUTABLE;

// Suffix of the checkpoint file kept next to an unfinished download.
inline constexpr std::string_view CHECKPOINT_SUFFIX = ".deoxdm";

} // namespace deoxdm
