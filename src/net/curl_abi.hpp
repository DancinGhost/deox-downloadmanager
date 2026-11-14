// SPDX-License-Identifier: MIT
#pragma once

// Minimal declarations of the libcurl "easy" C API.
//
// libcurl's ABI (symbol names, option numbers, calling convention) has been
// stable across the whole libcurl.so.4 series, so Deox Download Manager declares the handful of
// entry points it uses instead of requiring the libcurl development headers at
// build time. Only the runtime library is needed to build and run.
//
// Option numbers follow curl/curl.h: a type base (LONG = 0, OBJECTPOINT =
// 10000, FUNCTIONPOINT = 20000, OFF_T = 30000) plus the option's index.
//
// These names and values describe libcurl's public interface as defined in
// curl/curl.h (Copyright (c) Daniel Stenberg, <daniel@haxx.se>, and many
// contributors; curl license). No libcurl source is copied here. See
// THIRD_PARTY_NOTICES.md for the libcurl notice.

#include <cstddef>
#include <cstdint>

namespace deoxdm::net::abi {

using CURL = void;
using CURLcode = int;
using curl_off_t = int64_t;

struct curl_slist {
    char* data;
    curl_slist* next;
};

// Result codes
inline constexpr CURLcode CURLE_OK = 0;
inline constexpr CURLcode CURLE_UNSUPPORTED_PROTOCOL = 1;
inline constexpr CURLcode CURLE_URL_MALFORMAT = 3;
inline constexpr CURLcode CURLE_HTTP_RETURNED_ERROR = 22;
inline constexpr CURLcode CURLE_WRITE_ERROR = 23;
inline constexpr CURLcode CURLE_RANGE_ERROR = 33;
inline constexpr CURLcode CURLE_ABORTED_BY_CALLBACK = 42;
inline constexpr CURLcode CURLE_UNKNOWN_OPTION = 48;
inline constexpr CURLcode CURLE_PEER_FAILED_VERIFICATION = 60;
inline constexpr CURLcode CURLE_SSL_CACERT_BADFILE = 77;

// Options taking a long
inline constexpr int CURLOPT_LOW_SPEED_LIMIT = 19;
inline constexpr int CURLOPT_LOW_SPEED_TIME = 20;
inline constexpr int CURLOPT_TIMEOUT = 13;
inline constexpr int CURLOPT_NOPROGRESS = 43;
inline constexpr int CURLOPT_NOBODY = 44;
inline constexpr int CURLOPT_FAILONERROR = 45;
inline constexpr int CURLOPT_FOLLOWLOCATION = 52;
inline constexpr int CURLOPT_SSL_VERIFYPEER = 64;
inline constexpr int CURLOPT_MAXREDIRS = 68;
inline constexpr int CURLOPT_CONNECTTIMEOUT = 78;
inline constexpr int CURLOPT_SSL_VERIFYHOST = 81;
inline constexpr int CURLOPT_BUFFERSIZE = 98;
inline constexpr int CURLOPT_NOSIGNAL = 99;
inline constexpr int CURLOPT_PROTOCOLS = 181;       // deprecated in 7.85, kept for older libcurl
inline constexpr int CURLOPT_REDIR_PROTOCOLS = 182; // deprecated in 7.85, kept for older libcurl

// Bits for CURLOPT_PROTOCOLS / CURLOPT_REDIR_PROTOCOLS
inline constexpr long CURLPROTO_HTTP = 1L << 0;
inline constexpr long CURLPROTO_HTTPS = 1L << 1;

// Options taking a pointer
inline constexpr int CURLOPT_WRITEDATA = 10001;
inline constexpr int CURLOPT_URL = 10002;
inline constexpr int CURLOPT_RANGE = 10007;
inline constexpr int CURLOPT_ERRORBUFFER = 10010;
inline constexpr int CURLOPT_USERAGENT = 10018;
inline constexpr int CURLOPT_HTTPHEADER = 10023;
inline constexpr int CURLOPT_HEADERDATA = 10029;
inline constexpr int CURLOPT_XFERINFODATA = 10057;
inline constexpr int CURLOPT_PROTOCOLS_STR = 10318;       // libcurl >= 7.85
inline constexpr int CURLOPT_REDIR_PROTOCOLS_STR = 10319; // libcurl >= 7.85

// Options taking a function pointer
inline constexpr int CURLOPT_WRITEFUNCTION = 20011;
inline constexpr int CURLOPT_HEADERFUNCTION = 20079;
inline constexpr int CURLOPT_XFERINFOFUNCTION = 20219;

// curl_easy_getinfo selectors
inline constexpr int CURLINFO_EFFECTIVE_URL = 0x100000 + 1;
inline constexpr int CURLINFO_REDIRECT_URL = 0x100000 + 31;
inline constexpr int CURLINFO_RESPONSE_CODE = 0x200000 + 2;
inline constexpr int CURLINFO_CONTENT_LENGTH_DOWNLOAD_T = 0x600000 + 15;

inline constexpr long CURL_GLOBAL_DEFAULT = 3;
inline constexpr size_t CURL_ERROR_SIZE = 256;

#ifndef DEOXDM_CURL_ABI_CONSTANTS_ONLY
extern "C" {
    CURLcode curl_global_init(long flags);
    void curl_global_cleanup(void);

    CURL* curl_easy_init(void);
    void curl_easy_reset(CURL* curl);
    void curl_easy_cleanup(CURL* curl);
    CURLcode curl_easy_setopt(CURL* curl, int option, ...);
    CURLcode curl_easy_getinfo(CURL* curl, int info, ...);
    CURLcode curl_easy_perform(CURL* curl);
    const char* curl_easy_strerror(CURLcode code);

    curl_slist* curl_slist_append(curl_slist* list, const char* string);
    void curl_slist_free_all(curl_slist* list);
}
#endif

} // namespace deoxdm::net::abi
