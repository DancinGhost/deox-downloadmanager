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

    // Oversized header lines are bounded so a misbehaving server cannot exhaust memory.
    headers.feed("ETag: " + std::string(20000, 'x'));
    ASSERT_TRUE(headers.etag.size() <= 8192u);
}

#if __has_include(<curl/curl.h>)
#define DEOXDM_CURL_ABI_CONSTANTS_ONLY
#include "../../src/net/curl_abi.hpp"
namespace {
constexpr auto kAbiProtoHttp = deoxdm::net::abi::CURLPROTO_HTTP;
constexpr auto kAbiProtoHttps = deoxdm::net::abi::CURLPROTO_HTTPS;
constexpr auto kAbiGlobalDefault = deoxdm::net::abi::CURL_GLOBAL_DEFAULT;
constexpr auto kAbiErrorSize = deoxdm::net::abi::CURL_ERROR_SIZE;
} // namespace
#include <curl/curl.h>
static_assert(deoxdm::net::abi::CURLE_OK == static_cast<int>(::CURLE_OK));
static_assert(deoxdm::net::abi::CURLE_UNSUPPORTED_PROTOCOL == static_cast<int>(::CURLE_UNSUPPORTED_PROTOCOL));
static_assert(deoxdm::net::abi::CURLE_URL_MALFORMAT == static_cast<int>(::CURLE_URL_MALFORMAT));
static_assert(deoxdm::net::abi::CURLE_HTTP_RETURNED_ERROR == static_cast<int>(::CURLE_HTTP_RETURNED_ERROR));
static_assert(deoxdm::net::abi::CURLE_WRITE_ERROR == static_cast<int>(::CURLE_WRITE_ERROR));
static_assert(deoxdm::net::abi::CURLE_RANGE_ERROR == static_cast<int>(::CURLE_RANGE_ERROR));
static_assert(deoxdm::net::abi::CURLE_ABORTED_BY_CALLBACK == static_cast<int>(::CURLE_ABORTED_BY_CALLBACK));
static_assert(deoxdm::net::abi::CURLE_UNKNOWN_OPTION == static_cast<int>(::CURLE_UNKNOWN_OPTION));
static_assert(deoxdm::net::abi::CURLE_PEER_FAILED_VERIFICATION == static_cast<int>(::CURLE_PEER_FAILED_VERIFICATION));
static_assert(deoxdm::net::abi::CURLE_SSL_CACERT_BADFILE == static_cast<int>(::CURLE_SSL_CACERT_BADFILE));
static_assert(deoxdm::net::abi::CURLOPT_LOW_SPEED_LIMIT == static_cast<int>(::CURLOPT_LOW_SPEED_LIMIT));
static_assert(deoxdm::net::abi::CURLOPT_LOW_SPEED_TIME == static_cast<int>(::CURLOPT_LOW_SPEED_TIME));
static_assert(deoxdm::net::abi::CURLOPT_TIMEOUT == static_cast<int>(::CURLOPT_TIMEOUT));
static_assert(deoxdm::net::abi::CURLOPT_NOPROGRESS == static_cast<int>(::CURLOPT_NOPROGRESS));
static_assert(deoxdm::net::abi::CURLOPT_NOBODY == static_cast<int>(::CURLOPT_NOBODY));
static_assert(deoxdm::net::abi::CURLOPT_FAILONERROR == static_cast<int>(::CURLOPT_FAILONERROR));
static_assert(deoxdm::net::abi::CURLOPT_FOLLOWLOCATION == static_cast<int>(::CURLOPT_FOLLOWLOCATION));
static_assert(deoxdm::net::abi::CURLOPT_SSL_VERIFYPEER == static_cast<int>(::CURLOPT_SSL_VERIFYPEER));
static_assert(deoxdm::net::abi::CURLOPT_MAXREDIRS == static_cast<int>(::CURLOPT_MAXREDIRS));
static_assert(deoxdm::net::abi::CURLOPT_CONNECTTIMEOUT == static_cast<int>(::CURLOPT_CONNECTTIMEOUT));
static_assert(deoxdm::net::abi::CURLOPT_SSL_VERIFYHOST == static_cast<int>(::CURLOPT_SSL_VERIFYHOST));
static_assert(deoxdm::net::abi::CURLOPT_BUFFERSIZE == static_cast<int>(::CURLOPT_BUFFERSIZE));
static_assert(deoxdm::net::abi::CURLOPT_NOSIGNAL == static_cast<int>(::CURLOPT_NOSIGNAL));
static_assert(deoxdm::net::abi::CURLOPT_WRITEDATA == static_cast<int>(::CURLOPT_WRITEDATA));
static_assert(deoxdm::net::abi::CURLOPT_URL == static_cast<int>(::CURLOPT_URL));
static_assert(deoxdm::net::abi::CURLOPT_RANGE == static_cast<int>(::CURLOPT_RANGE));
static_assert(deoxdm::net::abi::CURLOPT_ERRORBUFFER == static_cast<int>(::CURLOPT_ERRORBUFFER));
static_assert(deoxdm::net::abi::CURLOPT_USERAGENT == static_cast<int>(::CURLOPT_USERAGENT));
static_assert(deoxdm::net::abi::CURLOPT_HTTPHEADER == static_cast<int>(::CURLOPT_HTTPHEADER));
static_assert(deoxdm::net::abi::CURLOPT_HEADERDATA == static_cast<int>(::CURLOPT_HEADERDATA));
static_assert(deoxdm::net::abi::CURLOPT_XFERINFODATA == static_cast<int>(::CURLOPT_XFERINFODATA));
static_assert(deoxdm::net::abi::CURLOPT_WRITEFUNCTION == static_cast<int>(::CURLOPT_WRITEFUNCTION));
static_assert(deoxdm::net::abi::CURLOPT_HEADERFUNCTION == static_cast<int>(::CURLOPT_HEADERFUNCTION));
static_assert(deoxdm::net::abi::CURLOPT_XFERINFOFUNCTION == static_cast<int>(::CURLOPT_XFERINFOFUNCTION));
static_assert(deoxdm::net::abi::CURLINFO_EFFECTIVE_URL == static_cast<int>(::CURLINFO_EFFECTIVE_URL));
static_assert(deoxdm::net::abi::CURLINFO_REDIRECT_URL == static_cast<int>(::CURLINFO_REDIRECT_URL));
static_assert(deoxdm::net::abi::CURLINFO_RESPONSE_CODE == static_cast<int>(::CURLINFO_RESPONSE_CODE));
static_assert(deoxdm::net::abi::CURLINFO_CONTENT_LENGTH_DOWNLOAD_T == static_cast<int>(::CURLINFO_CONTENT_LENGTH_DOWNLOAD_T));
static_assert(kAbiProtoHttp == ::CURLPROTO_HTTP);
static_assert(kAbiProtoHttps == ::CURLPROTO_HTTPS);
static_assert(kAbiGlobalDefault == ::CURL_GLOBAL_DEFAULT);
static_assert(kAbiErrorSize == ::CURL_ERROR_SIZE);
#endif
