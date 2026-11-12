// SPDX-License-Identifier: MIT
//
// deox-dm: command line front end of Deox Download Manager.

#include "options.hpp"
#include "terminal_ui.hpp"

#include "deoxdm/engine/download_engine.hpp"
#include "deoxdm/version.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInterrupted = 130;

// A signal handler may do very little. It only raises a flag; a helper thread
// turns the flag into a stop request.
std::atomic<bool> g_interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void handle_signal(int signal_number) {
    g_interrupted.store(true, std::memory_order_relaxed);
    // A second Ctrl-C terminates immediately instead of waiting for a clean stop.
    std::signal(signal_number, SIG_DFL);
}

bool read_url_file(const std::string& name, std::vector<std::string>& urls) {
    std::string content;
    if (name == "-") {
        content.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    } else {
        std::ifstream file(name, std::ios::binary);
        if (!file) {
            return false;
        }
        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    for (auto& url : deoxdm::cli::parse_url_list(content)) {
        urls.push_back(std::move(url));
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string_view program_name = (argc > 0 && argv[0] != nullptr) ? argv[0] : deoxdm::CLI_EXECUTABLE;
    if (const auto slash = program_name.find_last_of("/\\"); slash != std::string_view::npos) {
        program_name.remove_prefix(slash + 1);
    }
    const std::vector<std::string_view> args(argv + (argc > 0 ? 1 : 0), argv + argc);

    if (args.empty()) {
        std::cerr << deoxdm::cli::help_text(program_name);
        return kExitUsage;
    }

    auto parsed = deoxdm::cli::parse_arguments(args);
    if (!parsed) {
        std::cerr << program_name << ": " << parsed.error() << "\n"
                  << "Try '" << program_name << " --help' for more information.\n";
        return kExitUsage;
    }
    deoxdm::cli::Options& options = *parsed;

    if (options.show_help) {
        std::cout << deoxdm::cli::help_text(program_name);
        return 0;
    }
    if (options.show_version) {
        std::cout << deoxdm::cli::version_text();
        return 0;
    }

    std::vector<std::string> urls = std::move(options.urls);
    for (const auto& file : options.input_files) {
        if (!read_url_file(file, urls)) {
            std::cerr << program_name << ": cannot read URL list '" << file << "'\n";
            return kExitUsage;
        }
    }
    if (urls.empty()) {
        std::cerr << program_name << ": no URL given\n";
        return kExitUsage;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::stop_source stop_source;
    std::jthread signal_watcher([&stop_source](const std::stop_token& watcher_stop) {
        while (!watcher_stop.stop_requested()) {
            if (g_interrupted.load(std::memory_order_relaxed)) {
                stop_source.request_stop();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    deoxdm::cli::TerminalUI ui(options.quiet);
    size_t failures = 0;
    bool interrupted = false;

    for (size_t i = 0; i < urls.size() && !interrupted; ++i) {
        deoxdm::core::DownloadConfig config = options.config;
        config.url = urls[i];

        ui.begin(urls[i], i + 1, urls.size());

        deoxdm::engine::DownloadEngine engine(std::move(config));
        engine.set_state_listener([&ui](deoxdm::core::DownloadPhase phase, std::string_view message) {
            ui.on_state_change(phase, message);
        });
        engine.set_info_listener([&ui](const deoxdm::engine::DownloadInfo& info) {
            ui.on_info(info);
        });
        engine.set_progress_listener([&ui](const deoxdm::core::ProgressSnapshot& snapshot, std::span<const deoxdm::core::ChunkState> chunks) {
            ui.on_progress(snapshot, chunks);
        });

        const auto result = engine.run(stop_source.get_token());
        if (result) {
            ui.finish_success(*result);
        } else {
            ui.finish_failure(result.error());
            ++failures;
            interrupted = result.error().code == deoxdm::core::ErrorCode::Cancelled;
        }
    }

    if (interrupted) {
        return kExitInterrupted;
    }
    if (failures > 0 && urls.size() > 1) {
        std::cerr << "[deox-dm] " << failures << " of " << urls.size() << " downloads failed\n";
    }
    return failures > 0 ? kExitFailure : 0;
}
