// SPDX-License-Identifier: MIT
//
// deox-dm: command line front end of Deox Download Manager.

#include "options.hpp"
#include "terminal_ui.hpp"

#include "deoxdm/engine/download_engine.hpp"
#include "deoxdm/platform/interrupt.hpp"
#include "deoxdm/version.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInterrupted = 130;

std::optional<std::string> read_text_file(const std::string& name) {
    if (name == "-") {
        return std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }
    std::ifstream file(name, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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

    if (std::ranges::count(options.input_files, std::string("-")) + std::ranges::count(options.header_files, std::string("-")) > 1) {
        std::cerr << program_name << ": standard input can only be read once\n";
        return kExitUsage;
    }

    for (const auto& file : options.header_files) {
        const auto content = read_text_file(file);
        if (!content) {
            std::cerr << program_name << ": cannot read header file '" << file << "'\n";
            return kExitUsage;
        }
        auto headers = deoxdm::cli::parse_header_list(*content);
        if (!headers) {
            std::cerr << program_name << ": invalid header file '" << file << "', " << headers.error() << "\n";
            return kExitUsage;
        }
        options.config.headers.insert(options.config.headers.end(), headers->begin(), headers->end());
    }

    std::vector<std::string> urls = std::move(options.urls);
    for (const auto& file : options.input_files) {
        const auto content = read_text_file(file);
        if (!content) {
            std::cerr << program_name << ": cannot read URL list '" << file << "'\n";
            return kExitUsage;
        }
        for (auto& url : deoxdm::cli::parse_url_list(*content)) {
            urls.push_back(std::move(url));
        }
    }
    if (urls.empty()) {
        std::cerr << program_name << ": no URL given\n";
        return kExitUsage;
    }

    // Ctrl-C and termination requests stop the downloads cleanly, so they can
    // be resumed. The handler only sets a flag; this thread turns it into a
    // stop request.
    deoxdm::platform::install_interrupt_handler();

    std::stop_source stop_source;
    std::mutex watcher_mutex;
    std::condition_variable_any watcher_cv;
    std::jthread signal_watcher([&](const std::stop_token& watcher_stop) {
        std::unique_lock lock(watcher_mutex);
        while (!watcher_stop.stop_requested()) {
            if (deoxdm::platform::interrupt_requested()) {
                stop_source.request_stop();
                return;
            }
            watcher_cv.wait_for(lock, watcher_stop, std::chrono::milliseconds(20), [] {
                return deoxdm::platform::interrupt_requested();
            });
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
