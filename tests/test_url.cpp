#include "test_framework.hpp"
#include "deox/core/url.hpp"

using namespace deox::core;

DEOX_TEST(url_valid_http) {
    auto res = parse_url("http://example.com/file.tar.gz");
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->scheme, "http");
    ASSERT_EQ(res->host, "example.com");
    ASSERT_EQ(res->port, 80);
    ASSERT_EQ(res->path, "/file.tar.gz");
    ASSERT_EQ(res->filename, "file.tar.gz");
}

DEOX_TEST(url_valid_https_with_custom_port) {
    auto res = parse_url("https://archive.org:8443/data/test.iso?token=123");
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->scheme, "https");
    ASSERT_EQ(res->host, "archive.org");
    ASSERT_EQ(res->port, 8443);
    ASSERT_EQ(res->path, "/data/test.iso");
    ASSERT_EQ(res->query, "token=123");
    ASSERT_EQ(res->filename, "test.iso");
}

DEOX_TEST(url_empty_or_invalid_scheme) {
    auto r1 = parse_url("");
    ASSERT_FALSE(r1.has_value());

    auto r2 = parse_url("ftp://ftp.example.com/file.txt");
    ASSERT_FALSE(r2.has_value());

    auto r3 = parse_url("not_a_url");
    ASSERT_FALSE(r3.has_value());
}

DEOX_TEST(url_filename_sanitization_defense) {
    // Defend against directory traversal attacks in URLs
    ASSERT_EQ(sanitize_filename("../../etc/shadow"), "shadow");
    ASSERT_EQ(sanitize_filename("..\\..\\windows\\system32\\cmd.exe"), "cmd.exe");
    ASSERT_EQ(sanitize_filename(""), "download.bin");
    ASSERT_EQ(sanitize_filename(".."), "download.bin");
    ASSERT_EQ(sanitize_filename("."), "download.bin");
    ASSERT_EQ(sanitize_filename("normal_file.zip"), "normal_file.zip");
}
