// SPDX-License-Identifier: MIT
#pragma once

// Shared fixtures and helpers for end-to-end integration tests.

#include "mock_http_server.hpp"
#include "test_framework.hpp"

#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/core/checksum.hpp"
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/engine/download_engine.hpp"
#include "deoxdm/net/http_client.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace deoxdm::test {

inline constexpr size_t kPayloadSize = 256 * 1024;

inline std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void write_file(const std::filesystem::path& path, std::span<const uint8_t> content) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
}

// A configuration that splits the small test payload into several chunks.
inline core::DownloadConfig make_config(const std::string& url, const std::filesystem::path& output) {
    core::DownloadConfig config;
    config.url = url;
    config.output_path = output;
    config.connections = 4;
    config.min_chunk_size = 16 * 1024;
    config.timeout_seconds = 10;
    config.max_retries = 2;
    return config;
}

inline size_t count_ranged_gets(const MockHttpServer& server, uint64_t min_start = 0) {
    size_t count = 0;
    for (const auto& request : server.requests()) {
        if (request.method == "GET" && request.range_start && *request.range_start >= min_start) ++count;
    }
    return count;
}

} // namespace deoxdm::test
