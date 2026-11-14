// SPDX-License-Identifier: MIT
// End-to-end redirect, request header, filename derivation, and path security tests.

#include "integration_helpers.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace deoxdm;
using deoxdm::engine::DownloadEngine;
using deoxdm::test::kPayloadSize;
using deoxdm::test::make_config;
using deoxdm::test::make_payload;
using deoxdm::test::MockHttpServer;
using deoxdm::test::read_file;
using deoxdm::test::TempDir;
using deoxdm::test::write_file;

DEOXDM_TEST(integration_redirect_and_derived_filename) {
    const auto payload = make_payload(kPayloadSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    // The output is a directory: the name comes from the redirect target.
    const auto result = DownloadEngine(make_config(server.url("/redirect"), dir.path())).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "redirected.bin");
    ASSERT_EQ(read_file(dir / "redirected.bin"), payload);
}

DEOXDM_TEST(integration_content_disposition_filename) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload, .content_disposition = "attachment; filename=\"../../evil name.bin\""});
    const TempDir dir;

    // A directory that does not exist yet, marked by the trailing separator.
    const auto result = DownloadEngine(make_config(server.url("/download?id=42"), dir / "new" / "")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "new" / "evil name.bin"); // the directory part was stripped
    ASSERT_EQ(read_file(dir / "new" / "evil name.bin"), payload);
}

DEOXDM_TEST(integration_existing_file_is_not_overwritten_by_derived_name) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    const std::vector<uint8_t> precious = {'k', 'e', 'e', 'p'};
    write_file(dir / "file.bin", precious);

    const auto first = DownloadEngine(make_config(server.url("/file.bin"), dir.path())).run();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->output_path, dir / "file.1.bin");
    ASSERT_EQ(read_file(dir / "file.bin"), precious);

    const auto second = DownloadEngine(make_config(server.url("/file.bin"), dir.path())).run();
    ASSERT_EQ(second->output_path, dir / "file.2.bin");

    auto overwrite = make_config(server.url("/file.bin"), dir.path());
    overwrite.overwrite = true;
    const auto third = DownloadEngine(overwrite).run();
    ASSERT_EQ(third->output_path, dir / "file.bin");
    ASSERT_EQ(read_file(dir / "file.bin"), payload);
}

DEOXDM_TEST(integration_explicit_output_replaces_larger_file) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    write_file(dir / "out.bin", make_payload(100'000)); // older, larger file

    const auto result = DownloadEngine(make_config(server.url(), dir / "out.bin")).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);
}

DEOXDM_TEST(integration_custom_headers_and_user_agent) {
    MockHttpServer server({.payload = make_payload(1024)});
    const TempDir dir;

    auto config = make_config(server.url(), dir / "out.bin");
    config.headers = {"X-Api-Key: secret-token"};
    config.user_agent = "custom-agent/9.9";
    ASSERT_TRUE(DownloadEngine(config).run().has_value());

    for (const auto& request : server.requests()) {
        ASSERT_TRUE(request.raw.find("X-Api-Key: secret-token\r\n") != std::string::npos);
        ASSERT_TRUE(request.raw.find("User-Agent: custom-agent/9.9\r\n") != std::string::npos);
    }

    MockHttpServer plain_server({.payload = make_payload(1024)});
    ASSERT_TRUE(DownloadEngine(make_config(plain_server.url(), dir / "plain.bin")).run().has_value());
    ASSERT_TRUE(plain_server.requests().front().raw.find("User-Agent: deox-dm/") != std::string::npos);
}

DEOXDM_TEST(integration_custom_headers_stay_with_the_origin) {
    const auto payload = make_payload(64 * 1024);
    MockHttpServer target({.payload = payload});
    // Another port is another origin, just like another host.
    MockHttpServer origin({.payload = payload, .redirect_location = target.url("/file.bin")});
    const TempDir dir;

    auto config = make_config(origin.url("/redirect"), dir / "out.bin");
    config.headers = {"X-Api-Key: secret-token", "Authorization: Bearer secret"};
    const auto result = DownloadEngine(config).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(read_file(dir / "out.bin"), payload);

    ASSERT_FALSE(origin.requests().empty());
    for (const auto& request : origin.requests()) {
        ASSERT_TRUE(request.raw.find("X-Api-Key: secret-token\r\n") != std::string::npos);
    }
    ASSERT_FALSE(target.requests().empty());
    for (const auto& request : target.requests()) {
        ASSERT_TRUE(request.raw.find("secret") == std::string::npos);
    }
}

DEOXDM_TEST(integration_custom_headers_follow_same_origin_redirects) {
    const auto payload = make_payload(1024);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    auto config = make_config(server.url("/redirect"), dir / "out.bin");
    config.headers = {"X-Api-Key: secret-token"};
    ASSERT_TRUE(DownloadEngine(config).run().has_value());
    for (const auto& request : server.requests()) {
        ASSERT_TRUE(request.raw.find("X-Api-Key: secret-token\r\n") != std::string::npos);
    }
}

DEOXDM_TEST(integration_redirect_loop_is_stopped) {
    MockHttpServer server({.payload = make_payload(1024), .redirect_location = "/redirect"});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url("/redirect"), dir.path())).run();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().message.find("Too many redirects") != std::string::npos);
    ASSERT_TRUE(server.requests().size() <= 2 * 11u); // HEAD and ranged GET, 11 requests each
}

DEOXDM_TEST(integration_redirect_to_another_protocol_is_refused) {
    MockHttpServer server({.payload = make_payload(1024), .redirect_location = "ftp://127.0.0.1/file.bin"});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url("/redirect"), dir.path())).run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().code, core::ErrorCode::InvalidUrl);
}

DEOXDM_TEST(integration_server_cannot_choose_a_hidden_file_name) {
    MockHttpServer server({.payload = make_payload(1024), .content_disposition = "attachment; filename=\".bashrc\""});
    const TempDir dir;

    const auto result = DownloadEngine(make_config(server.url(), dir.path())).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "bashrc");
    ASSERT_FALSE(std::filesystem::exists(dir / ".bashrc"));
}

DEOXDM_TEST(integration_derived_name_skips_planted_links) {
    const auto payload = make_payload(1024);
    MockHttpServer server({.payload = payload});
    const TempDir dir;
    // A dangling link under the name the server will suggest.
    std::filesystem::create_symlink(dir / "elsewhere.bin", dir / "file.bin");

    const auto result = DownloadEngine(make_config(server.url("/file.bin"), dir.path())).run();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output_path, dir / "file.1.bin");
    ASSERT_FALSE(std::filesystem::exists(dir / "elsewhere.bin"));
}
