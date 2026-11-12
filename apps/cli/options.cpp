// SPDX-License-Identifier: MIT
#include "options.hpp"
#include "deoxdm/core/checksum.hpp"
#include "deoxdm/core/formatting.hpp"
#include "deoxdm/version.hpp"

#include <charconv>
#include <format>
#include <optional>

namespace deoxdm::cli {

namespace {

std::optional<uint32_t> parse_number(std::string_view text, uint32_t min, uint32_t max) {
    uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value < min || value > max) {
        return std::nullopt;
    }
    return value;
}

std::string_view trim(std::string_view s) {
    constexpr std::string_view spaces = " \t\r\n";
    const auto first = s.find_first_not_of(spaces);
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(spaces) - first + 1);
}

} // namespace

std::expected<Options, std::string> parse_arguments(std::span<const std::string_view> args) {
    Options options;
    std::string directory;
    std::string output;
    bool options_ended = false;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];

        if (options_ended || arg.empty() || arg.front() != '-' || arg == "-") {
            if (arg.empty()) {
                return std::unexpected("empty argument");
            }
            options.urls.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            options_ended = true;
            continue;
        }

        // "--name=value" carries its value inline.
        std::optional<std::string_view> inline_value;
        if (arg.starts_with("--")) {
            if (const auto eq = arg.find('='); eq != std::string_view::npos) {
                inline_value = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
            }
        }

        const auto is = [&](std::string_view short_name, std::string_view long_name) {
            return (!short_name.empty() && arg == short_name) || arg == long_name;
        };

        std::optional<std::string> problem;
        const auto value = [&](std::string_view description) -> std::string_view {
            if (inline_value) return *inline_value;
            if (i + 1 < args.size()) return args[++i];
            problem = std::format("option '{}' requires {}", arg, description);
            return {};
        };
        const auto flag = [&]() {
            if (inline_value) problem = std::format("option '{}' does not take a value", arg);
            return true;
        };

        if (is("-h", "--help")) {
            options.show_help = flag();
        } else if (is("-V", "--version") || arg == "-v") {
            options.show_version = flag();
        } else if (is("-q", "--quiet")) {
            options.quiet = flag();
        } else if (is("-k", "--insecure")) {
            options.config.allow_insecure_tls = flag();
        } else if (is("", "--no-resume")) {
            options.config.resume_enabled = !flag();
        } else if (is("", "--overwrite")) {
            options.config.overwrite = flag();
        } else if (is("-o", "--output")) {
            output = value("a file name");
            if (!problem && output.empty()) problem = "the output file name must not be empty";
        } else if (is("-d", "--dir")) {
            directory = value("a directory");
            if (!problem && directory.empty()) problem = "the directory must not be empty";
        } else if (is("-i", "--input-file")) {
            const std::string_view file = value("a file name");
            if (!problem) options.input_files.emplace_back(file);
        } else if (is("-c", "--connections")) {
            const auto number = parse_number(value("a number"), 1, core::kMaxConnections);
            if (!problem && !number) problem = std::format("connections must be a number between 1 and {}", core::kMaxConnections);
            if (number) options.config.connections = *number;
        } else if (is("-t", "--timeout")) {
            const auto number = parse_number(value("a number of seconds"), 0, 86400);
            if (!problem && !number) problem = "timeout must be a number of seconds between 0 and 86400";
            if (number) options.config.timeout_seconds = *number;
        } else if (is("-r", "--retries")) {
            const auto number = parse_number(value("a number"), 0, 1000);
            if (!problem && !number) problem = "retries must be a number between 0 and 1000";
            if (number) options.config.max_retries = *number;
        } else if (is("-l", "--limit-rate")) {
            const auto rate = core::parse_byte_size(value("a rate such as 500K or 2M"));
            if (!problem && !rate) problem = "limit-rate must be a size such as 500K or 2M (bytes per second)";
            if (rate) options.config.max_speed_bps = *rate;
        } else if (is("-s", "--sha256")) {
            const std::string_view digest = trim(value("a SHA-256 checksum"));
            if (!problem && !core::is_valid_sha256_hex(digest)) problem = "sha256 must be 64 hexadecimal characters";
            options.config.expected_sha256 = std::string(digest);
        } else if (is("-H", "--header")) {
            const std::string_view header = value("a header such as 'Name: value'");
            if (!problem && (header.find(':') == std::string_view::npos || header.find_first_of("\r\n") != std::string_view::npos)) {
                problem = std::format("invalid header '{}': expected 'Name: value'", header);
            }
            if (!problem) options.config.headers.emplace_back(header);
        } else if (is("-A", "--user-agent")) {
            options.config.user_agent = std::string(value("a user agent string"));
        } else {
            problem = std::format("unknown option '{}'", arg);
        }

        if (problem) {
            return std::unexpected(std::move(*problem));
        }
    }

    if (options.show_help || options.show_version) {
        return options;
    }

    if (options.urls.empty() && options.input_files.empty()) {
        return std::unexpected("no URL given");
    }

    const bool single_download = options.urls.size() == 1 && options.input_files.empty();
    if (!single_download) {
        if (!output.empty()) {
            return std::unexpected("--output names a single file; use --dir when downloading several URLs");
        }
        if (!options.config.expected_sha256.empty()) {
            return std::unexpected("--sha256 can only be used with a single URL");
        }
    }

    if (!output.empty()) {
        std::filesystem::path path(output);
        options.config.output_path = (!directory.empty() && path.is_relative()) ? std::filesystem::path(directory) / path : path;
    } else if (!directory.empty()) {
        // A trailing separator marks the path as a directory even before it exists.
        options.config.output_path = std::filesystem::path(directory) / "";
    }

    return options;
}

std::vector<std::string> parse_url_list(std::string_view content) {
    std::vector<std::string> urls;
    size_t pos = 0;
    while (pos < content.size()) {
        const size_t end = content.find('\n', pos);
        const std::string_view line = trim(content.substr(pos, end == std::string_view::npos ? end : end - pos));
        pos = (end == std::string_view::npos) ? content.size() : end + 1;
        if (!line.empty() && line.front() != '#') {
            urls.emplace_back(line);
        }
    }
    return urls;
}

std::string help_text(std::string_view program_name) {
    return std::format(
R"(Usage: {0} [OPTIONS] <URL>...

{1}.

Downloads each URL over several parallel connections when the server supports
range requests. An interrupted download leaves a "<file>{3}" checkpoint next
to the partial file; running the same command again continues it.

Arguments:
  <URL>...                   One or more http:// or https:// URLs

Output:
  -o, --output <FILE>        Write to FILE, replacing it if it exists (single URL only)
  -d, --dir <DIR>            Save into DIR (created if missing) instead of the current directory
      --overwrite            Replace an existing file instead of saving as "name.1.ext"
  -i, --input-file <FILE>    Read URLs from FILE, one per line ("-" for standard input)

Transfer:
  -c, --connections <NUM>    Parallel connections per download, 1-{2} (default: 4)
  -l, --limit-rate <RATE>    Cap the download speed, e.g. 500K or 2M bytes per second
  -t, --timeout <SEC>        Give up on a connection attempt or a stalled transfer
                             after SEC seconds; 0 disables the timeout (default: 30)
  -r, --retries <NUM>        Retries per segment after a transient failure (default: 3)
      --no-resume            Ignore an existing checkpoint and start over
  -H, --header <LINE>        Add a request header, e.g. -H "Authorization: Bearer ..." (repeatable)
  -A, --user-agent <NAME>    Send NAME as the User-Agent
  -k, --insecure             Do not verify TLS certificates

Verification:
  -s, --sha256 <HEX>         Verify the finished file against this SHA-256 checksum

Other:
  -q, --quiet                Print nothing but errors
  -V, --version              Show version information
  -h, --help                 Show this help

Proxies are taken from the http_proxy, https_proxy and no_proxy environment variables.

Exit status: 0 on success, 1 if a download failed, 2 on a usage error, 130 if interrupted.

Examples:
  {0} https://example.com/file.tar.gz
  {0} -c 8 -d ~/Downloads https://example.com/a.iso https://example.com/b.iso
  {0} -o linux.iso -s <sha256> https://example.com/linux.iso
)",
        program_name, PROJECT_DESCRIPTION, core::kMaxConnections, CHECKPOINT_SUFFIX);
}

std::string version_text() {
    return std::format("{} {} ({})\n{}\nLicense: MIT\n{}\n",
        CLI_EXECUTABLE, VERSION_STRING, PROJECT_NAME, PROJECT_DESCRIPTION, PROJECT_URL);
}

} // namespace deoxdm::cli
