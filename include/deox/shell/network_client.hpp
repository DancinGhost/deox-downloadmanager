#pragma once

#include "deox/core/types.hpp"
#include "deox/shell/curl_wrapper.hpp"
#include <functional>
#include <stop_token>
#include <expected>

namespace deox::shell {

using ChunkDataSink = std::function<std::expected<void, core::DeoxError>(uint64_t offset, std::span<const uint8_t> data)>;
using ChunkProgressNotifier = std::function<void(uint32_t chunk_idx, uint64_t bytes_new)>;

class NetworkClient {
public:
    NetworkClient() = default;

    // Queries remote server headers to discover file size, range capability, etag, and filename
    [[nodiscard]] static std::expected<core::RemoteFileInfo, core::DeoxError> probe(
        const std::string& url,
        const core::DownloadConfig& config
    ) noexcept;

    // Downloads a single range-chunk, writing streamed bytes to sink and reporting progress
    [[nodiscard]] static std::expected<void, core::DeoxError> download_chunk(
        const std::string& url,
        uint32_t chunk_idx,
        const core::ByteRange& range,
        uint64_t already_downloaded,
        ChunkDataSink data_sink,
        ChunkProgressNotifier progress_notifier,
        std::stop_token stop_token,
        const core::DownloadConfig& config
    ) noexcept;
};

} // namespace deox::shell
