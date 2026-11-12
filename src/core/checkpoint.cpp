// SPDX-License-Identifier: MIT
#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/core/chunk_planner.hpp"

#include <charconv>
#include <format>
#include <optional>

namespace deoxdm::core {

namespace {

constexpr size_t kMaxCheckpointChunks = 4096;

std::string_view trim_line(std::string_view line) noexcept {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return line;
}

template <typename T>
std::optional<T> parse_uint(std::string_view sv) noexcept {
    T val{};
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
        return val;
    }
    return std::nullopt;
}

std::unexpected<Error> corrupted(std::string message) {
    return std::unexpected(Error{ErrorCode::StateCorrupted, std::move(message)});
}

} // namespace

std::string serialize_checkpoint(const ResumeData& data) noexcept {
    std::string out;
    out.reserve(256 + data.url.size() + data.chunks.size() * 64);

    out += "# DEOX DOWNLOAD MANAGER CHECKPOINT v1\n";
    out += "url=" + data.url + "\n";
    out += "total_size=" + std::to_string(data.total_size) + "\n";
    out += "etag=" + data.etag + "\n";
    out += "last_modified=" + data.last_modified + "\n";
    out += "output_file=" + data.output_file + "\n";
    out += "chunks_count=" + std::to_string(data.chunks.size()) + "\n";

    for (const auto& ch : data.chunks) {
        out += std::format("chunk={},{},{},{},{}\n",
            ch.index,
            ch.range.start_byte,
            ch.range.end_byte,
            ch.bytes_downloaded,
            chunk_status_to_string(ch.status)
        );
    }

    return out;
}

std::expected<ResumeData, Error> deserialize_checkpoint(std::string_view content) noexcept {
    ResumeData data;
    std::optional<size_t> expected_chunks;
    bool has_url = false;
    bool has_total_size = false;

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t end_line = content.find('\n', pos);
        std::string_view line = content.substr(pos, end_line == std::string_view::npos ? end_line : end_line - pos);
        pos = (end_line != std::string_view::npos) ? end_line + 1 : content.size();

        line = trim_line(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const size_t eq_pos = line.find('=');
        if (eq_pos == std::string_view::npos) {
            continue;
        }

        const std::string_view key = line.substr(0, eq_pos);
        const std::string_view val = line.substr(eq_pos + 1);

        if (key == "url") {
            data.url = std::string(val);
            has_url = !val.empty();
        } else if (key == "total_size") {
            const auto size = parse_uint<uint64_t>(val);
            if (!size) return corrupted("Checkpoint has an invalid total_size");
            data.total_size = *size;
            has_total_size = true;
        } else if (key == "etag") {
            data.etag = std::string(val);
        } else if (key == "last_modified") {
            data.last_modified = std::string(val);
        } else if (key == "output_file") {
            data.output_file = std::string(val);
        } else if (key == "chunks_count") {
            const auto count = parse_uint<size_t>(val);
            if (!count) return corrupted("Checkpoint has an invalid chunks_count");
            expected_chunks = *count;
        } else if (key == "chunk") {
            // chunk=index,start,end,downloaded,status
            std::string_view parts[5];
            std::string_view remaining = val;
            for (size_t i = 0; i < 5; ++i) {
                const size_t comma = remaining.find(',');
                const bool last = (i == 4);
                if ((comma == std::string_view::npos) != last) {
                    return corrupted("Checkpoint has a malformed chunk record");
                }
                parts[i] = remaining.substr(0, comma);
                if (!last) remaining.remove_prefix(comma + 1);
            }

            const auto idx = parse_uint<uint32_t>(parts[0]);
            const auto start = parse_uint<uint64_t>(parts[1]);
            const auto end = parse_uint<uint64_t>(parts[2]);
            const auto downloaded = parse_uint<uint64_t>(parts[3]);
            const auto status = chunk_status_from_string(parts[4]);

            if (!idx || !start || !end || !downloaded || !status) {
                return corrupted("Checkpoint has invalid values in a chunk record");
            }
            if (data.chunks.size() >= kMaxCheckpointChunks) {
                return corrupted("Checkpoint lists too many chunks");
            }

            data.chunks.push_back(ChunkState{
                .index = *idx,
                .range = ByteRange{*start, *end},
                .bytes_downloaded = *downloaded,
                .status = *status
            });
        }
    }

    if (!has_url || !has_total_size) {
        return corrupted("Checkpoint is missing the url or total_size field");
    }
    if (expected_chunks && data.chunks.size() != *expected_chunks) {
        return corrupted("Checkpoint is truncated: chunk count does not match");
    }
    if (data.total_size == 0 || data.chunks.empty()) {
        return corrupted("Checkpoint does not describe any data");
    }

    const ChunkPlan as_plan{.chunks = data.chunks, .total_size = data.total_size, .size_known = true, .is_segmented = data.chunks.size() > 1};
    if (!verify_chunk_plan(as_plan)) {
        return corrupted("Checkpoint chunks do not cover the file exactly");
    }

    for (auto& chunk : data.chunks) {
        if (chunk.bytes_downloaded > chunk.range.length()) {
            return corrupted("Checkpoint chunk claims more bytes than it spans");
        }
        // The stored status only reflects what a worker was doing when the
        // checkpoint was written. What matters for resuming is the byte count:
        // anything unfinished, including a failed chunk, must be fetched again.
        chunk.status = chunk.is_complete() ? ChunkStatus::Completed : ChunkStatus::Pending;
    }

    return data;
}

std::expected<void, Error> validate_resume_compatibility(
    const ResumeData& resume,
    std::string_view current_url,
    const RemoteFileInfo& remote
) noexcept {
    if (resume.url != current_url) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Checkpoint was created for a different URL"});
    }

    if (!remote.accepts_ranges) {
        return std::unexpected(Error{ErrorCode::RangeNotSupported, "Server does not support range requests, cannot resume"});
    }

    if (!remote.content_length || *remote.content_length != resume.total_size) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Remote file size changed since the checkpoint was created"});
    }

    if (!resume.etag.empty() && !remote.etag.empty()) {
        if (resume.etag != remote.etag) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "Remote file changed since the checkpoint was created (ETag differs)"});
        }
    } else if (!resume.last_modified.empty() && !remote.last_modified.empty() &&
               resume.last_modified != remote.last_modified) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Remote file changed since the checkpoint was created (Last-Modified differs)"});
    }

    return {};
}

} // namespace deoxdm::core
