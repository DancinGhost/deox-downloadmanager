// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/url.hpp"

using namespace deoxdm::core;

DEOXDM_TEST(url_valid_http) {
    auto res = parse_url("http://example.com/file.tar.gz");
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->scheme, "http");
    ASSERT_EQ(res->host, "example.com");
    ASSERT_EQ(res->port, 80);
    ASSERT_EQ(res->path, "/file.tar.gz");
    ASSERT_EQ(res->filename, "file.tar.gz");
}

DEOXDM_TEST(url_valid_https_with_custom_port) {
    auto res = parse_url("  HTTPS://archive.org:8443/data/test.iso?token=123#section \n");
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->scheme, "https");
    ASSERT_EQ(res->host, "archive.org");
    ASSERT_EQ(res->port, 8443);
    ASSERT_EQ(res->path, "/data/test.iso");
    ASSERT_EQ(res->query, "token=123");
    ASSERT_EQ(res->filename, "test.iso");
    ASSERT_EQ(res->full_url(), "https://archive.org:8443/data/test.iso?token=123");
}

DEOXDM_TEST(url_userinfo_and_ipv6) {
    auto with_user = parse_url("https://user:secret@example.com/a.bin");
    ASSERT_TRUE(with_user.has_value());
    ASSERT_EQ(with_user->userinfo, "user:secret");
    ASSERT_EQ(with_user->host, "example.com");
    ASSERT_EQ(with_user->port, 443);

    auto ipv6 = parse_url("http://[::1]:8080/a.bin");
    ASSERT_TRUE(ipv6.has_value());
    ASSERT_EQ(ipv6->host, "[::1]");
    ASSERT_EQ(ipv6->port, 8080);

    auto ipv6_default_port = parse_url("http://[2001:db8::1]/a.bin");
    ASSERT_TRUE(ipv6_default_port.has_value());
    ASSERT_EQ(ipv6_default_port->port, 80);
}

DEOXDM_TEST(url_without_path) {
    auto bare = parse_url("http://example.com");
    ASSERT_TRUE(bare.has_value());
    ASSERT_EQ(bare->path, "/");
    ASSERT_EQ(bare->filename, "download.bin");

    auto query_only = parse_url("http://example.com?id=7");
    ASSERT_TRUE(query_only.has_value());
    ASSERT_EQ(query_only->host, "example.com");
    ASSERT_EQ(query_only->path, "/");
    ASSERT_EQ(query_only->query, "id=7");
}

DEOXDM_TEST(url_rejects_invalid_input) {
    ASSERT_FALSE(parse_url("").has_value());
    ASSERT_FALSE(parse_url("ftp://ftp.example.com/file.txt").has_value());
    ASSERT_FALSE(parse_url("not_a_url").has_value());
    ASSERT_FALSE(parse_url("http://").has_value());
    ASSERT_FALSE(parse_url("http://:80/file").has_value());
    ASSERT_FALSE(parse_url("http://example.com:99999/file").has_value());
    ASSERT_FALSE(parse_url("http://example.com:abc/file").has_value());
    ASSERT_FALSE(parse_url("http://[::1/file").has_value());
    ASSERT_FALSE(parse_url("http://example.com/a b").has_value());
    ASSERT_FALSE(parse_url("http://example.com/a\r\nX-Injected: 1").has_value());
    ASSERT_EQ(parse_url("gopher://x/").error().code, ErrorCode::InvalidUrl);
}

DEOXDM_TEST(url_filename_is_percent_decoded) {
    ASSERT_EQ(parse_url("http://example.com/My%20Report%20(final).pdf")->filename, "My Report (final).pdf");
    ASSERT_EQ(parse_url("http://example.com/caf%C3%A9.txt")->filename, "caf\xC3\xA9.txt");
    ASSERT_EQ(percent_decode("100%25%zz%4"), "100%%zz%4");
}

DEOXDM_TEST(url_filename_sanitization_defense) {
    ASSERT_EQ(sanitize_filename("../../etc/shadow"), "shadow");
    ASSERT_EQ(sanitize_filename("..\\..\\windows\\system32\\cmd.exe"), "cmd.exe");
    ASSERT_EQ(sanitize_filename(""), "download.bin");
    ASSERT_EQ(sanitize_filename(".."), "download.bin");
    ASSERT_EQ(sanitize_filename("."), "download.bin");
    ASSERT_EQ(sanitize_filename("normal_file.zip"), "normal_file.zip");
    ASSERT_EQ(sanitize_filename("a:b*c?.txt"), "a_b_c_.txt");
    ASSERT_EQ(sanitize_filename("name.txt. . "), "name.txt");
    ASSERT_EQ(sanitize_filename(std::string_view("a\0b\n.bin", 8)), "a_b_.bin");

    // An encoded path separator must not smuggle a directory into the name.
    ASSERT_EQ(parse_url("http://example.com/..%2F..%2Fetc%2Fpasswd")->filename, "passwd");
    ASSERT_EQ(parse_url("http://example.com/dir/%2E%2E")->filename, "download.bin");
}

DEOXDM_TEST(url_filename_length_is_limited) {
    const std::string long_name = std::string(400, 'a') + ".tar.gz";
    const std::string sanitized = sanitize_filename(long_name);
    ASSERT_TRUE(sanitized.size() <= 200);
    ASSERT_TRUE(sanitized.ends_with(".tar.gz"));

    // Truncation never cuts a multi-byte character in half.
    std::string unicode;
    for (int i = 0; i < 150; ++i) unicode += "\xC3\xA9";
    const std::string cut = sanitize_filename(unicode);
    ASSERT_TRUE(cut.size() <= 200);
    ASSERT_EQ(cut.size() % 2, 0u);
}

DEOXDM_TEST(url_content_disposition_filename) {
    ASSERT_EQ(filename_from_content_disposition("attachment; filename=\"report 2026.pdf\""), "report 2026.pdf");
    ASSERT_EQ(filename_from_content_disposition("attachment; filename=plain.zip"), "plain.zip");
    ASSERT_EQ(filename_from_content_disposition("ATTACHMENT; FileName=\"a;b.txt\"; size=3"), "a;b.txt");
    ASSERT_EQ(filename_from_content_disposition("attachment; filename=\"fallback.txt\"; filename*=UTF-8''na%C3%AFve%20file.txt"),
              "na\xC3\xAFve file.txt");
    ASSERT_EQ(filename_from_content_disposition("attachment; filename=\"../../etc/passwd\""), "passwd");
    ASSERT_EQ(filename_from_content_disposition("inline"), "");
    ASSERT_EQ(filename_from_content_disposition(""), "");
}

DEOXDM_TEST(url_numbered_filename) {
    ASSERT_EQ(numbered_filename("file.zip", 1), "file.1.zip");
    ASSERT_EQ(numbered_filename("archive.tar.gz", 2), "archive.2.tar.gz");
    ASSERT_EQ(numbered_filename("my.report.v2.pdf", 3), "my.report.v2.3.pdf");
    ASSERT_EQ(numbered_filename("README", 1), "README.1");
    ASSERT_EQ(numbered_filename(".bashrc", 1), ".bashrc.1");
}
