#include "test_framework.hpp"
#include "deox/core/checksum.hpp"

using namespace deox::core;

DEOX_TEST(checksum_sha256_nist_vectors) {
    // NIST Test Vector 1: Empty string
    // e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    ASSERT_EQ(compute_sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // NIST Test Vector 2: "abc"
    // ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    ASSERT_EQ(compute_sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // NIST Test Vector 3: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    // 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
    ASSERT_EQ(
        compute_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    );
}

DEOX_TEST(checksum_verification_case_insensitive) {
    const std::string lower = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string upper = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    ASSERT_TRUE(verify_sha256(lower, upper));
    ASSERT_TRUE(verify_sha256(lower, "  " + upper + "  \n"));
    ASSERT_FALSE(verify_sha256(lower, "0000000000000000000000000000000000000000000000000000000000000000"));
}
