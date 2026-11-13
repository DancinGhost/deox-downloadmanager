// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"
#include "deoxdm/platform/file.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>

namespace deoxdm::engine {

// The file a download is written into.
//
// All I/O is positional (see platform::File), so any number of threads may
// write disjoint regions concurrently. Errors are translated into
// core::Error values with messages that name the file.
class OutputFile {
public:
    OutputFile() noexcept = default;

    OutputFile(OutputFile&&) noexcept = default;
    OutputFile& operator=(OutputFile&&) noexcept = default;

    // Starts a fresh download: creates the file (and missing parent
    // directories), discards any previous content and, when the final size is
    // known, reserves the disk space up front.
    //
    // `server_chosen_name`: the file name came from the server, not from the
    // user. A symbolic link under that name is then never followed, and when
    // nothing has that name yet the file is created exclusively, so nothing
    // can be slipped in between the caller's check and the creation.
    [[nodiscard]] static std::expected<OutputFile, core::Error> create(
        const std::filesystem::path& path,
        std::optional<uint64_t> total_size,
        bool server_chosen_name = false
    );

    // Continues a download: the file must already exist with exactly the
    // expected size. Its content is left untouched. `server_chosen_name` as
    // for create().
    [[nodiscard]] static std::expected<OutputFile, core::Error> open_existing(
        const std::filesystem::path& path,
        uint64_t expected_size,
        bool server_chosen_name = false
    );

    // Writes all of `data` at `offset`, or fails.
    [[nodiscard]] std::expected<void, core::Error> write_at(uint64_t offset, std::span<const uint8_t> data) noexcept;

    // Cuts the file to `size` bytes.
    [[nodiscard]] std::expected<void, core::Error> truncate(uint64_t size) noexcept;

    // Forces written data to stable storage.
    [[nodiscard]] std::expected<void, core::Error> sync() noexcept;

    // Hashes the file's current content. Returns Cancelled if stopped early.
    [[nodiscard]] std::expected<std::string, core::Error> compute_sha256(std::stop_token stop_token = {});

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return file_.is_open();
    }

    void close() noexcept {
        file_.close();
    }

private:
    OutputFile(platform::File file, std::filesystem::path path) noexcept
        : file_(std::move(file)), path_(std::move(path)) {}

    platform::File file_;
    std::filesystem::path path_;
};

} // namespace deoxdm::engine
