#include "test_framework.hpp"
#include "deox/shell/file_writer.hpp"
#include "deox/core/checksum.hpp"
#include <fstream>
#include <vector>

using namespace deox::shell;

DEOX_TEST(file_writer_out_of_order_writes) {
    const std::filesystem::path test_path = "/tmp/deox_test_file.bin";
    std::filesystem::remove(test_path);

    constexpr uint64_t total_size = 30; // 3 chunks of 10 bytes each
    auto writer_res = FileWriter::open(test_path, total_size, true);
    ASSERT_TRUE(writer_res.has_value());

    auto writer = std::move(*writer_res);

    // Write chunk 2 first (offset 20)
    std::vector<uint8_t> chunk2 = {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C'};
    auto w2 = writer.write_at(20, chunk2);
    ASSERT_TRUE(w2.has_value());
    ASSERT_EQ(*w2, 10);

    // Write chunk 0 (offset 0)
    std::vector<uint8_t> chunk0 = {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A'};
    auto w0 = writer.write_at(0, chunk0);
    ASSERT_TRUE(w0.has_value());
    ASSERT_EQ(*w0, 10);

    // Write chunk 1 (offset 10)
    std::vector<uint8_t> chunk1 = {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'};
    auto w1 = writer.write_at(10, chunk1);
    ASSERT_TRUE(w1.has_value());
    ASSERT_EQ(*w1, 10);

    // Verify on-disk SHA-256
    auto sha_res = writer.compute_file_sha256();
    ASSERT_TRUE(sha_res.has_value());

    // Expected string is "AAAAAAAAAABBBBBBBBBBCCCCCCCCCC"
    std::string expected_str = "AAAAAAAAAABBBBBBBBBBCCCCCCCCCC";
    std::string expected_hash = deox::core::compute_sha256(expected_str);

    ASSERT_EQ(*sha_res, expected_hash);

    writer.close();
    std::filesystem::remove(test_path);
}
