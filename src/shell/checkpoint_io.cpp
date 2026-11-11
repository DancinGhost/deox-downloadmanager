#include "deox/shell/checkpoint_io.hpp"
#include <fstream>
#include <format>

namespace deox::shell {

std::filesystem::path CheckpointIO::checkpoint_path_for(const std::filesystem::path& target_file) noexcept {
    return std::filesystem::path(target_file.string() + ".deox");
}

bool CheckpointIO::exists(const std::filesystem::path& target_file) noexcept {
    std::error_code ec;
    return std::filesystem::exists(checkpoint_path_for(target_file), ec);
}

std::expected<void, core::DeoxError> CheckpointIO::save(
    const std::filesystem::path& target_file,
    const std::string& url,
    uint64_t total_size,
    const std::string& etag,
    const std::string& last_modified,
    std::span<const core::ChunkState> chunks
) noexcept {
    const auto final_path = checkpoint_path_for(target_file);
    const auto tmp_path = std::filesystem::path(final_path.string() + ".tmp");

    const std::string content = core::serialize_checkpoint(
        url, total_size, etag, last_modified, target_file.filename().string(), chunks
    );

    {
        std::ofstream ofs(tmp_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return std::unexpected(core::DeoxError{
                core::ErrorCode::FileSystemError,
                std::format("Failed to open temporary checkpoint file '{}' for writing", tmp_path.string())
            });
        }
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.flush();
        if (!ofs.good()) {
            return std::unexpected(core::DeoxError{
                core::ErrorCode::FileSystemError,
                std::format("Failed to write checkpoint data to '{}'", tmp_path.string())
            });
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        return std::unexpected(core::DeoxError{
            core::ErrorCode::FileSystemError,
            std::format("Failed to atomically rename checkpoint '{}' to '{}': {}", tmp_path.string(), final_path.string(), ec.message())
        });
    }

    return {};
}

std::expected<core::ResumeData, core::DeoxError> CheckpointIO::load(
    const std::filesystem::path& target_file
) noexcept {
    const auto cp_path = checkpoint_path_for(target_file);

    std::ifstream ifs(cp_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return std::unexpected(core::DeoxError{
            core::ErrorCode::FileSystemError,
            std::format("Failed to open checkpoint file '{}'", cp_path.string())
        });
    }

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return core::deserialize_checkpoint(content);
}

void CheckpointIO::remove(const std::filesystem::path& target_file) noexcept {
    std::error_code ec;
    std::filesystem::remove(checkpoint_path_for(target_file), ec);
    std::filesystem::remove(std::filesystem::path(checkpoint_path_for(target_file).string() + ".tmp"), ec);
}

} // namespace deox::shell
