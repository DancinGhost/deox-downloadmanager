#include "deox/version.hpp"
#include "deox/core/types.hpp"
#include "deox/shell/engine.hpp"
#include "deox/shell/terminal_ui.hpp"

#include <csignal>
#include <iostream>
#include <format>
#include <stop_token>
#include <charconv>

namespace {

std::stop_source g_global_stop_source;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_global_stop_source.request_stop();
    }
}

void print_version() {
    std::cout << std::format("{} version {}\n", deox::PROJECT_NAME, deox::VERSION_STRING);
    std::cout << deox::PROJECT_DESCRIPTION << "\n";
    std::cout << "License: MIT License (Cleanroom C++23, zero GPL contamination)\n";
    std::cout << "Repository: https://github.com/DancinGhost/deox-downloadmanager\n";
}

void print_help(std::string_view program_name) {
    std::cout << std::format("Usage: {} [OPTIONS] <URL>\n\n", program_name);
    std::cout << "High-performance, memory-safe download manager in modern C++23.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  <URL>                      HTTP/HTTPS download target URL\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <FILE>        Output target file path (default: extracted from URL)\n";
    std::cout << "  -c, --connections <NUM>    Parallel segment connections (default: 4, min: 1, max: 64)\n";
    std::cout << "  -s, --sha256 <HEX>         Expected SHA-256 checksum to verify upon completion\n";
    std::cout << "  -t, --timeout <SEC>        Network connection/transfer timeout in seconds (default: 30)\n";
    std::cout << "  -r, --retries <NUM>        Max chunk retries on network transient error (default: 3)\n";
    std::cout << "  -k, --insecure             Allow insecure TLS connections (skip certificate verification)\n";
    std::cout << "      --no-resume            Ignore existing checkpoint and restart download from scratch\n";
    std::cout << "  -q, --quiet                Quiet mode (suppress progress output, only show errors)\n";
    std::cout << "  -v, --version              Display version and licensing information\n";
    std::cout << "  -h, --help                 Display this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  deox https://example.com/file.tar.gz\n";
    std::cout << "  deox -c 8 -o ubuntu.iso https://releases.ubuntu.com/24.04/ubuntu-24.04-desktop-amd64.iso\n";
    std::cout << "  deox -s e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 https://example.com/data.bin\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    deox::core::DownloadConfig config;
    std::string program_name = argv[0];

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_help(program_name);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a file path argument\n";
                return 1;
            }
            config.output_path = argv[i];
        } else if (arg == "-c" || arg == "--connections") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires an integer argument\n";
                return 1;
            }
            uint32_t val = 4;
            const auto [ptr, ec] = std::from_chars(argv[i], argv[i] + std::string_view(argv[i]).size(), val);
            if (ec != std::errc{} || val == 0 || val > 64) {
                std::cerr << "Error: invalid connections count (must be between 1 and 64)\n";
                return 1;
            }
            config.connections = val;
        } else if (arg == "-s" || arg == "--sha256") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a 64-character hex hash\n";
                return 1;
            }
            config.expected_sha256 = argv[i];
        } else if (arg == "-t" || arg == "--timeout") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a timeout in seconds\n";
                return 1;
            }
            uint32_t val = 30;
            std::from_chars(argv[i], argv[i] + std::string_view(argv[i]).size(), val);
            config.timeout_seconds = val;
        } else if (arg == "-r" || arg == "--retries") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires an integer argument\n";
                return 1;
            }
            uint32_t val = 3;
            std::from_chars(argv[i], argv[i] + std::string_view(argv[i]).size(), val);
            config.max_retries = val;
        } else if (arg == "-k" || arg == "--insecure") {
            config.allow_insecure_tls = true;
        } else if (arg == "--no-resume") {
            config.resume_enabled = false;
        } else if (arg == "-q" || arg == "--quiet") {
            config.quiet = true;
        } else if (arg.starts_with("-")) {
            std::cerr << "Error: unrecognized option '" << arg << "'\n";
            print_help(program_name);
            return 1;
        } else {
            if (!config.url.empty()) {
                std::cerr << "Error: unexpected extra argument '" << arg << "'\n";
                return 1;
            }
            config.url = arg;
        }
    }

    if (config.url.empty()) {
        std::cerr << "Error: no target URL specified\n";
        print_help(program_name);
        return 1;
    }

    // Set up POSIX signal handling for clean cancellation and checkpointing
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    deox::shell::TerminalUI ui(config.quiet);
    deox::shell::DownloadEngine engine(std::move(config));

    engine.set_state_listener([&ui](deox::core::DownloadPhase phase, std::string_view msg) {
        ui.on_state_change(phase, msg);
    });

    engine.set_progress_listener([&ui](const deox::core::ProgressSnapshot& snapshot, std::span<const deox::core::ChunkState> chunks) {
        ui.on_progress(snapshot, chunks);
    });

    const auto result = engine.run(g_global_stop_source.get_token());

    if (result) {
        ui.finish(true);
        return 0;
    } else {
        ui.finish(false, result.error().message);
        return (result.error().code == deox::core::ErrorCode::Cancelled) ? 130 : 1;
    }
}
