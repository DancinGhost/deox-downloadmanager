// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/checkpoint.hpp"
#include "deoxdm/core/checksum.hpp"
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/engine/output_file.hpp"

#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace deoxdm::engine;
using deoxdm::test::TempDir;

namespace {

std::span<const uint8_t> bytes(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

DEOXDM_TEST(file_writer_out_of_order_writes) {
    const TempDir dir;
    const auto path = dir / "out.bin";

    auto writer = OutputFile::create(path, 30);
    ASSERT_TRUE(writer.has_value());
    ASSERT_EQ(std::filesystem::file_size(path), 30u); // preallocated

    ASSERT_TRUE(writer->write_at(20, bytes("CCCCCCCCCC")).has_value());
    ASSERT_TRUE(writer->write_at(0, bytes("AAAAAAAAAA")).has_value());
    ASSERT_TRUE(writer->write_at(10, bytes("BBBBBBBBBB")).has_value());
    ASSERT_TRUE(writer->sync().has_value());

    const auto digest = writer->compute_sha256();
    ASSERT_TRUE(digest.has_value());
    ASSERT_EQ(*digest, deoxdm::core::compute_sha256("AAAAAAAAAABBBBBBBBBBCCCCCCCCCC"));

    writer->close();
    ASSERT_EQ(read_file(path), "AAAAAAAAAABBBBBBBBBBCCCCCCCCCC");
}

DEOXDM_TEST(file_writer_concurrent_writes) {
    const TempDir dir;
    const auto path = dir / "out.bin";
    constexpr size_t threads = 8;
    constexpr size_t block = 64 * 1024;

    auto writer = OutputFile::create(path, threads * block);
    ASSERT_TRUE(writer.has_value());

    {
        std::vector<std::jthread> workers;
        for (size_t t = 0; t < threads; ++t) {
            workers.emplace_back([&writer, t] {
                const std::vector<uint8_t> data(block, static_cast<uint8_t>('a' + t));
                // Many small writes so that the threads really interleave.
                for (size_t off = 0; off < block; off += 4096) {
                    (void)writer->write_at(t * block + off, std::span(data).subspan(off, 4096));
                }
            });
        }
    }
    writer->close();

    const std::string content = read_file(path);
    ASSERT_EQ(content.size(), threads * block);
    for (size_t t = 0; t < threads; ++t) {
        ASSERT_EQ(content.substr(t * block, block), std::string(block, static_cast<char>('a' + t)));
    }
}

DEOXDM_TEST(file_writer_create_discards_previous_content) {
    // Regression: downloading over a larger existing file used to leave the
    // old file's tail behind the new content.
    const TempDir dir;
    const auto path = dir / "out.bin";
    write_file(path, std::string(1000, 'x'));

    auto writer = OutputFile::create(path, 10);
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer->write_at(0, bytes("0123456789")).has_value());
    writer->close();

    ASSERT_EQ(read_file(path), "0123456789");
}

DEOXDM_TEST(file_writer_creates_parent_directories) {
    const TempDir dir;
    const auto path = dir / "a/b/c/out.bin";
    auto writer = OutputFile::create(path, std::nullopt);
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_EQ(std::filesystem::file_size(path), 0u);
}

DEOXDM_TEST(file_writer_truncate) {
    const TempDir dir;
    const auto path = dir / "out.bin";
    auto writer = OutputFile::create(path, std::nullopt);
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer->write_at(0, bytes("0123456789")).has_value());
    ASSERT_TRUE(writer->truncate(4).has_value());
    writer->close();
    ASSERT_EQ(read_file(path), "0123");
}

DEOXDM_TEST(file_writer_open_existing_checks_the_file) {
    const TempDir dir;
    const auto path = dir / "partial.bin";

    ASSERT_FALSE(OutputFile::open_existing(path, 10).has_value()); // missing

    write_file(path, "0123456789");
    ASSERT_FALSE(OutputFile::open_existing(path, 11).has_value()); // wrong size
    ASSERT_FALSE(OutputFile::open_existing(dir.path(), 10).has_value()); // a directory

    auto writer = OutputFile::open_existing(path, 10);
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer->write_at(5, bytes("ABCDE")).has_value());
    writer->close();
    ASSERT_EQ(read_file(path), "01234ABCDE"); // existing content was kept
}

DEOXDM_TEST(file_writer_reports_errors) {
    const TempDir dir;
    ASSERT_FALSE(OutputFile::create(dir.path(), 10).has_value()); // a directory, not a file

    OutputFile closed;
    ASSERT_FALSE(closed.is_open());
    ASSERT_FALSE(closed.write_at(0, bytes("x")).has_value());
    ASSERT_FALSE(closed.sync().has_value());
    ASSERT_FALSE(closed.compute_sha256().has_value());
}

DEOXDM_TEST(checkpoint_io_round_trip) {
    const TempDir dir;
    const auto target = dir / "movie.mkv";

    ASSERT_FALSE(CheckpointStore::exists(target));
    ASSERT_FALSE(CheckpointStore::load(target).has_value());

    const deoxdm::core::ResumeData data{
        .url = "https://example.com/movie.mkv",
        .total_size = 200,
        .etag = "\"v1\"",
        .last_modified = "",
        .output_file = "movie.mkv",
        .chunks = {
            {.index = 0, .range = {0, 99}, .bytes_downloaded = 100, .status = deoxdm::core::ChunkStatus::Completed},
            {.index = 1, .range = {100, 199}, .bytes_downloaded = 40, .status = deoxdm::core::ChunkStatus::Downloading}
        }
    };

    ASSERT_TRUE(CheckpointStore::save(target, data).has_value());
    ASSERT_TRUE(CheckpointStore::exists(target));
    ASSERT_EQ(CheckpointStore::checkpoint_path_for(target), dir / "movie.mkv.deoxdm");
    ASSERT_FALSE(std::filesystem::exists(dir / "movie.mkv.deoxdm.tmp")); // no temporary file left behind

    const auto loaded = CheckpointStore::load(target);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->url, data.url);
    ASSERT_EQ(loaded->chunks.size(), 2u);
    ASSERT_EQ(loaded->chunks[1].bytes_downloaded, 40u);

    CheckpointStore::remove(target);
    ASSERT_FALSE(CheckpointStore::exists(target));
    CheckpointStore::remove(target); // removing a missing checkpoint is fine
}
