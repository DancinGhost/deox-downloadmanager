// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"

#include <expected>
#include <functional>
#include <span>
#include <stop_token>

namespace deoxdm::net {

// Receives the bytes of a chunk in order. `offset` is the absolute position
// in the file. Returning an error aborts the transfer with that error.
using ChunkDataSink = std::function<std::expected<void, core::Error>(uint64_t offset, std::span<const uint8_t> data)>;

struct ChunkRequest {
    core::ByteRange range;          // the whole chunk; open-ended when the size is unknown
    uint64_t already_downloaded{0}; // bytes of this chunk that are already on disk
    bool send_range{true};          // false: plain GET of the entire resource
    bool whole_file{false};         // the chunk spans the entire resource
};

class HttpClient {
public:
    // Asks the server about the resource: size, range support, validators and
    // the file name it suggests. Uses HEAD, and a one-byte ranged GET when
    // HEAD is refused or does not tell the whole story.
    [[nodiscard]] static std::expected<core::RemoteFileInfo, core::Error> probe(
        const core::DownloadConfig& config,
        std::stop_token stop_token = {}
    );

    // Downloads the not yet fetched part of one chunk into `sink`.
    //
    // Guarantees on success: exactly the requested bytes were delivered, in
    // order. A server that ignores the Range header, answers with a different
    // range, or sends too little or too much data yields an error and never
    // silently corrupts the file.
    [[nodiscard]] static std::expected<void, core::Error> download_chunk(
        const core::DownloadConfig& config,
        const ChunkRequest& request,
        const ChunkDataSink& sink,
        std::stop_token stop_token
    );
};

} // namespace deoxdm::net
