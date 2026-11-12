// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deoxdm::cli {

struct Options {
    std::vector<std::string> urls;
    std::vector<std::string> input_files; // files listing URLs, "-" for standard input
    core::DownloadConfig config;          // applies to every URL; config.url is left empty
    bool quiet{false};
    bool show_help{false};
    bool show_version{false};
};

// Parses the command line (without the program name). On failure the error is
// a message suitable for showing to the user.
[[nodiscard]] std::expected<Options, std::string> parse_arguments(std::span<const std::string_view> args);

// Extracts URLs from the content of an input file: one per line, blank lines
// and lines starting with '#' are ignored.
[[nodiscard]] std::vector<std::string> parse_url_list(std::string_view content);

[[nodiscard]] std::string help_text(std::string_view program_name);
[[nodiscard]] std::string version_text();

} // namespace deoxdm::cli
