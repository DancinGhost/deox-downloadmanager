// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "options.hpp"

#include <initializer_list>
#include <vector>

using namespace deoxdm::cli;

namespace {

std::expected<Options, std::string> parse(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list(args);
    return parse_arguments(list);
}

} // namespace

DEOXDM_TEST(cli_defaults) {
    const auto options = parse({"https://example.com/a.bin"});
    ASSERT_TRUE(options.has_value());
    ASSERT_EQ(options->urls.size(), 1u);
    ASSERT_EQ(options->urls[0], "https://example.com/a.bin");
    ASSERT_EQ(options->config.connections, 4u);
    ASSERT_EQ(options->config.timeout_seconds, 30u);
    ASSERT_EQ(options->config.max_retries, 3u);
    ASSERT_EQ(options->config.max_speed_bps, 0u);
    ASSERT_TRUE(options->config.resume_enabled);
    ASSERT_FALSE(options->config.overwrite);
    ASSERT_FALSE(options->config.allow_insecure_tls);
    ASSERT_FALSE(options->quiet);
    ASSERT_TRUE(options->config.output_path.empty());
}

DEOXDM_TEST(cli_all_options) {
    const auto options = parse({
        "-c", "16", "--timeout=5", "-r", "0", "-l", "2M", "--max-size", "100M", "-k", "--no-resume", "--overwrite", "-q",
        "-s", "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
        "-H", "Authorization: Bearer abc", "--header", "X-Test: 1", "-A", "agent/1.0",
        "-o", "out.bin", "https://example.com/a.bin"
    });
    ASSERT_TRUE(options.has_value());
    ASSERT_EQ(options->config.connections, 16u);
    ASSERT_EQ(options->config.timeout_seconds, 5u);
    ASSERT_EQ(options->config.max_retries, 0u);
    ASSERT_EQ(options->config.max_speed_bps, 2u * 1024 * 1024);
    ASSERT_EQ(options->config.max_file_size, 100u * 1024 * 1024);
    ASSERT_TRUE(options->config.allow_insecure_tls);
    ASSERT_FALSE(options->config.resume_enabled);
    ASSERT_TRUE(options->config.overwrite);
    ASSERT_TRUE(options->quiet);
    ASSERT_EQ(options->config.headers.size(), 2u);
    ASSERT_EQ(options->config.headers[0], "Authorization: Bearer abc");
    ASSERT_EQ(options->config.user_agent, "agent/1.0");
    ASSERT_EQ(options->config.output_path, std::filesystem::path("out.bin"));
}

DEOXDM_TEST(cli_output_and_directory) {
    const auto dir_only = parse({"-d", "downloads", "https://example.com/a.bin"});
    ASSERT_TRUE(dir_only.has_value());
    // No file name: the engine appends the name the server suggests.
    ASSERT_FALSE(dir_only->config.output_path.has_filename());
    ASSERT_EQ(dir_only->config.output_path.parent_path(), std::filesystem::path("downloads"));

    const auto both = parse({"-d", "downloads", "-o", "x.bin", "https://example.com/a.bin"});
    ASSERT_EQ(both->config.output_path, std::filesystem::path("downloads/x.bin"));

    const auto absolute = parse({"-d", "downloads", "-o", "/tmp/x.bin", "https://example.com/a.bin"});
    ASSERT_EQ(absolute->config.output_path, std::filesystem::path("/tmp/x.bin"));
}

DEOXDM_TEST(cli_multiple_urls) {
    const auto options = parse({"https://example.com/a", "-c", "2", "https://example.com/b", "--", "-odd-name"});
    ASSERT_TRUE(options.has_value());
    ASSERT_EQ(options->urls.size(), 3u);
    ASSERT_EQ(options->urls[2], "-odd-name");

    ASSERT_FALSE(parse({"-o", "x", "https://example.com/a", "https://example.com/b"}).has_value());
    ASSERT_FALSE(parse({"-s", std::string_view("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
                        "https://example.com/a", "https://example.com/b"}).has_value());
    ASSERT_TRUE(parse({"-d", "dir", "https://example.com/a", "https://example.com/b"}).has_value());

    const auto from_file = parse({"-i", "urls.txt"});
    ASSERT_TRUE(from_file.has_value());
    ASSERT_EQ(from_file->input_files.size(), 1u);
}

DEOXDM_TEST(cli_rejects_bad_input) {
    ASSERT_FALSE(parse({}).has_value());
    ASSERT_FALSE(parse({"-c", "8"}).has_value());                                  // no URL
    ASSERT_FALSE(parse({"--frobnicate", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"https://example.com/a", "-c"}).has_value());              // missing value
    ASSERT_FALSE(parse({"-c", "0", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-c", "65", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-c", "8x", "https://example.com/a"}).has_value());
    // Regression: unparsable numbers used to be silently replaced by the default.
    ASSERT_FALSE(parse({"-t", "soon", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-r", "many", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-l", "fast", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"--max-size", "huge", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"--max-size", "0", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-s", "abc123", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-H", "NoColonHere", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-H", "X: 1\r\nY: 2", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"--quiet=yes", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-o", "", "https://example.com/a"}).has_value());
}

DEOXDM_TEST(cli_help_and_version_need_no_url) {
    ASSERT_TRUE(parse({"--help"})->show_help);
    ASSERT_TRUE(parse({"-h"})->show_help);
    ASSERT_TRUE(parse({"--version"})->show_version);
    ASSERT_TRUE(parse({"-V"})->show_version);
    ASSERT_TRUE(help_text("deox-dm").find("--connections") != std::string::npos);
    ASSERT_TRUE(version_text().starts_with("deox-dm canary"));
}

DEOXDM_TEST(cli_url_list_parsing) {
    const auto urls = parse_url_list("# mirrors\nhttps://example.com/a\r\n\n   https://example.com/b  \n#https://example.com/c\nhttps://example.com/d");
    ASSERT_EQ(urls.size(), 3u);
    ASSERT_EQ(urls[0], "https://example.com/a");
    ASSERT_EQ(urls[1], "https://example.com/b");
    ASSERT_EQ(urls[2], "https://example.com/d");
    ASSERT_TRUE(parse_url_list("").empty());
}

DEOXDM_TEST(cli_headers_from_file) {
    const auto options = parse({"-H", "@secret-headers.txt", "-H", "X-Inline: 1", "https://example.com/a"});
    ASSERT_TRUE(options.has_value());
    ASSERT_EQ(options->header_files.size(), 1u);
    ASSERT_EQ(options->header_files[0], "secret-headers.txt");
    ASSERT_EQ(options->config.headers.size(), 1u);
    ASSERT_FALSE(parse({"-H", "@", "https://example.com/a"}).has_value());
    ASSERT_FALSE(parse({"-H", ": no name", "https://example.com/a"}).has_value());

    const auto headers = parse_header_list("# API access\r\nAuthorization: Bearer abc\r\n\r\n  X-Api-Key: k  \n");
    ASSERT_TRUE(headers.has_value());
    ASSERT_EQ(headers->size(), 2u);
    ASSERT_EQ((*headers)[0], "Authorization: Bearer abc");
    ASSERT_EQ((*headers)[1], "X-Api-Key: k");

    const auto bad = parse_header_list("Good: 1\nsecret-without-colon\n");
    ASSERT_FALSE(bad.has_value());
    ASSERT_TRUE(bad.error().find("line 2") != std::string::npos);
    ASSERT_TRUE(bad.error().find("secret") == std::string::npos); // never echoed
}
