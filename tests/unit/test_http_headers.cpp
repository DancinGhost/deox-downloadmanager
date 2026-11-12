// SPDX-License-Identifier: MIT
#include "test_framework.hpp"
#include "deoxdm/core/http_headers.hpp"

using namespace deoxdm::core;

DEOXDM_TEST(http_content_range_parsing) {
    const auto full = parse_content_range("bytes 0-499/1234");
    ASSERT_TRUE(full.has_value());
    ASSERT_EQ(full->first_byte, 0u);
    ASSERT_EQ(full->last_byte, 499u);
    ASSERT_EQ(full->total_size, 1234u);

    const auto unknown_total = parse_content_range("Bytes 100-199/*");
    ASSERT_TRUE(unknown_total.has_value());
    ASSERT_EQ(unknown_total->first_byte, 100u);
    ASSERT_FALSE(unknown_total->total_size.has_value());

    ASSERT_FALSE(parse_content_range("").has_value());
    ASSERT_FALSE(parse_content_range("bytes */1234").has_value());    // unsatisfied range
    ASSERT_FALSE(parse_content_range("items 0-1/2").has_value());
    ASSERT_FALSE(parse_content_range("bytes 5-2/10").has_value());     // reversed
    ASSERT_FALSE(parse_content_range("bytes 0-99/50").has_value());    // beyond the total
    ASSERT_FALSE(parse_content_range("bytes 0-99").has_value());
    ASSERT_FALSE(parse_content_range("bytes a-b/c").has_value());
}

DEOXDM_TEST(http_headers_are_collected) {
    ResponseHeaders headers;
    headers.feed("HTTP/1.1 206 Partial Content\r\n");
    headers.feed("content-length: 500\r\n");
    headers.feed("Content-Range: bytes 0-499/1234\r\n");
    headers.feed("ACCEPT-RANGES:bytes\r\n");
    headers.feed("ETag: \"abc\"\r\n");
    headers.feed("Last-Modified: Thu, 01 Jan 2026 00:00:00 GMT\r\n");
    headers.feed("Content-Disposition: attachment; filename=\"x.bin\"\r\n");
    headers.feed("X-Unrelated: value\r\n");
    headers.feed("\r\n");

    ASSERT_EQ(headers.status, 206u);
    ASSERT_EQ(headers.content_length, 500u);
    ASSERT_TRUE(headers.content_range.has_value());
    ASSERT_EQ(headers.content_range->total_size, 1234u);
    ASSERT_TRUE(headers.accepts_ranges);
    ASSERT_EQ(headers.etag, "\"abc\"");
    ASSERT_EQ(headers.last_modified, "Thu, 01 Jan 2026 00:00:00 GMT");
    ASSERT_EQ(headers.content_disposition, "attachment; filename=\"x.bin\"");
}

DEOXDM_TEST(http_headers_reset_on_each_response) {
    // Regression: the Content-Length of a redirect response used to be taken
    // for the size of the file when the final response had none.
    ResponseHeaders headers;
    headers.feed("HTTP/1.1 302 Found\r\n");
    headers.feed("Content-Length: 154\r\n");
    headers.feed("Accept-Ranges: bytes\r\n");
    headers.feed("ETag: \"redirect\"\r\n");
    headers.feed("\r\n");
    headers.feed("HTTP/2 200\r\n");
    headers.feed("Transfer-Encoding: chunked\r\n");
    headers.feed("\r\n");

    ASSERT_EQ(headers.status, 200u);
    ASSERT_FALSE(headers.content_length.has_value());
    ASSERT_FALSE(headers.accepts_ranges);
    ASSERT_TRUE(headers.etag.empty());
}

DEOXDM_TEST(http_headers_ignore_malformed_values) {
    ResponseHeaders headers;
    headers.feed("HTTP/1.1 200 OK");
    headers.feed("Content-Length: lots");
    headers.feed("Accept-Ranges: none");
    headers.feed("no colon here");
    ASSERT_FALSE(headers.content_length.has_value());
    ASSERT_FALSE(headers.accepts_ranges);
}
