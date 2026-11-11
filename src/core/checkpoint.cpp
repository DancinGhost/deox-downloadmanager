#include "deox/core/checkpoint.hpp"
#include <sstream>
#include <charconv>

namespace deox::core {

namespace {

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

} // namespace

std::string serialize_checkpoint(
    const std::string& url,
    uint64_t total_size,
    const std::string& etag,
    const std::string& last_modified,
    const std::string& output_filename,
    std::span<const ChunkState> chunks
) noexcept {
    std::string out;
    out.reserve(256 + chunks.size() * 64);

    out += "# DEOX DOWNLOAD CHECKPOINT v1\n";
    out += "url=" + url + "\n";
    out += "total_size=" + std::to_string(total_size) + "\n";
    out += "etag=" + etag + "\n";
    out += "last_modified=" + last_modified + "\n";
    out += "output_file=" + output_filename + "\n";
    out += "chunks_count=" + std::to_string(chunks.size()) + "\n";

    for (const auto& ch : chunks) {
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

std::expected<ResumeData, DeoxError> deserialize_checkpoint(std::string_view content) noexcept {
    if (content.empty()) {
        return std::unexpected(DeoxError{ErrorCode::StateCorrupted, "Checkpoint file is empty"});
    }

    ResumeData data;
    size_t expected_chunks = 0;
    bool has_url = false;
    bool has_total_size = false;

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t end_line = content.find('\n', pos);
        std::string_view line = (end_line != std::string_view::npos) ? 
            content.substr(pos, end_line - pos) : content.substr(pos);
        pos = (end_line != std::string_view::npos) ? end_line + 1 : content.size();

        line = trim_line(line);
        if (line.empty() || line.front() == '#') {
            continue; // Skip comments and empty lines
        }

        const size_t eq_pos = line.find('=');
        if (eq_pos == std::string_view::npos) {
            continue;
        }

        const std::string_view key = line.substr(0, eq_pos);
        const std::string_view val = line.substr(eq_pos + 1);

        if (key == "url") {
            data.url = std::string(val);
            has_url = true;
        } else if (key == "total_size") {
            if (auto sz = parse_uint<uint64_t>(val)) {
                data.total_size = *sz;
                has_total_size = true;
            } else {
                return std::unexpected(DeoxError{ErrorCode::StateCorrupted, "Invalid total_size in checkpoint"});
            }
        } else if (key == "etag") {
            data.etag = std::string(val);
        } else if (key == "last_modified") {
            data.last_modified = std::string(val);
        } else if (key == "output_file") {
            data.output_file = std::string(val);
        } else if (key == "chunks_count") {
            if (auto cnt = parse_uint<size_t>(val)) {
                expected_chunks = *cnt;
                data.chunks.reserve(expected_chunks);
            }
        } else if (key == "chunk") {
            // chunk=index,start,end,downloaded,status
            std::vector<std::string_view> parts;
            size_t c_pos = 0;
            while (c_pos < val.size()) {
                size_t comma = val.find(',', c_pos);
                if (comma != std::string_view::npos) {
                    parts.push_back(val.substr(c_pos, comma - c_pos));
                    c_pos = comma + 1;
                } else {
                    parts.push_back(val.substr(c_pos));
                    break;
                }
            }

            if (parts.size() != 5) {
                return std::unexpected(DeoxError{ErrorCode::StateCorrupted, "Malformed chunk line in checkpoint"});
            }

            auto idx = parse_uint<uint32_t>(parts[0]);
            auto start = parse_uint<uint64_t>(parts[1]);
            auto end = parse_uint<uint64_t>(parts[2]);
            auto downloaded = parse_uint<uint64_t>(parts[3]);
            auto status = chunk_status_from_string(parts[4]);

            if (!idx || !start || !end || !downloaded || !status) {
                return std::unexpected(DeoxError{ErrorCode::StateCorrupted, "Invalid values in chunk record"});
            }

            ChunkStatus ch_status = *status;
            // If it was marked downloading before crash, reset to pending
            if (ch_status == ChunkStatus::Downloading) {
                ch_status = ChunkStatus::Pending;
            }

            data.chunks.push_back(ChunkState{
                .index = *idx,
                .range = ByteRange{*start, *end},
                .bytes_downloaded = *downloaded,
                .status = ch_status
            });
        }
    }

    if (!has_url || !has_total_size) {
        return std::unexpected(DeoxError{ErrorCode::StateCorrupted, "Checkpoint missing essential url or total_size"});
    }

    if (expected_chunks > 0 && data.chunks.size() != expected_chunks) {
        return std::unexpected(DeoxError{ErrorCode::StateCorrupted, "Checkpoint chunk count mismatch"});
    }

    return data;
}

std::expected<void, DeoxError> validate_resume_compatibility(
    const ResumeData& resume,
    const std::string& current_url,
    uint64_t current_size,
    std::string_view current_etag
) noexcept {
    if (resume.url != current_url) {
        return std::unexpected(DeoxError{ErrorCode::InvalidArgument, "Checkpoint URL does not match current target URL"});
    }

    if (resume.total_size != current_size) {
        return std::unexpected(DeoxError{ErrorCode::InvalidArgument, "Remote file size has changed since checkpoint was created"});
    }

    if (!resume.etag.empty() && !current_etag.empty() && resume.etag != current_etag) {
        return std::unexpected(DeoxError{ErrorCode::InvalidArgument, "Remote file ETag changed; file was modified on server"});
    }

    return {};
}

} // namespace deox::core
