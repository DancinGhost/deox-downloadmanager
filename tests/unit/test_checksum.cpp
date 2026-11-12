// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/checksum.hpp"

#include <string>
#include <vector>

using namespace deoxdm::core;

DEOXDM_TEST(checksum_sha256_nist_vectors) {
    ASSERT_EQ(compute_sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ASSERT_EQ(compute_sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ASSERT_EQ(
        compute_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    );
    // One million 'a' characters
    ASSERT_EQ(compute_sha256(std::string(1'000'000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

DEOXDM_TEST(checksum_streaming_matches_one_shot) {
    std::string data;
    for (int i = 0; i < 5000; ++i) data.push_back(static_cast<char>(i * 31 + 7));
    const std::string expected = compute_sha256(data);

    // Feed the same data in pieces of every awkward size around the block length.
    for (const size_t piece : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 119u, 1000u}) {
        Sha256 hasher;
        for (size_t pos = 0; pos < data.size(); pos += piece) {
            hasher.update(std::string_view(data).substr(pos, piece));
        }
        hasher.update(std::string_view{});
        ASSERT_EQ(hasher.finalize(), expected);
    }
}

DEOXDM_TEST(checksum_hasher_is_reusable) {
    Sha256 hasher;
    hasher.update("abc");
    ASSERT_EQ(hasher.finalize(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ASSERT_EQ(hasher.finalize(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

DEOXDM_TEST(checksum_verification) {
    const std::string lower = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string upper = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    ASSERT_TRUE(verify_sha256(lower, upper));
    ASSERT_TRUE(verify_sha256(lower, "  " + upper + "  \n"));
    ASSERT_FALSE(verify_sha256(lower, "0000000000000000000000000000000000000000000000000000000000000000"));
    ASSERT_FALSE(verify_sha256(lower, lower.substr(0, 63)));
    ASSERT_FALSE(verify_sha256(lower, ""));
}

DEOXDM_TEST(checksum_hex_validation) {
    ASSERT_TRUE(is_valid_sha256_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    ASSERT_TRUE(is_valid_sha256_hex(" BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\n"));
    ASSERT_FALSE(is_valid_sha256_hex(""));
    ASSERT_FALSE(is_valid_sha256_hex("abc"));
    ASSERT_FALSE(is_valid_sha256_hex("zz7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    ASSERT_FALSE(is_valid_sha256_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad00"));
}
