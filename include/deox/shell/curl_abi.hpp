#pragma once

#include <cstddef>
#include <cstdint>

// Standard C ABI declarations for libcurl (ABI compatible with libcurl 7.x and 8.x)
namespace deox::shell::abi {

typedef void CURL;
typedef int CURLcode;
typedef int CURLoption;
typedef int CURLINFO;

struct curl_slist {
    char* data;
    struct curl_slist* next;
};

// libcurl status codes
inline constexpr int CURLE_OK = 0;
inline constexpr int CURLE_UNSUPPORTED_PROTOCOL = 1;
inline constexpr int CURLE_COULDNT_RESOLVE_HOST = 6;
inline constexpr int CURLE_COULDNT_CONNECT = 7;
inline constexpr int CURLE_PARTIAL_FILE = 18;
inline constexpr int CURLE_OPERATION_TIMEDOUT = 28;
inline constexpr int CURLE_SSL_CONNECT_ERROR = 35;
inline constexpr int CURLE_RANGE_ERROR = 33;
inline constexpr int CURLE_ABORTED_BY_CALLBACK = 42;
inline constexpr int CURLE_TOO_MANY_REDIRECTS = 47;

// libcurl option constants (Option base + type offset)
inline constexpr int CURLOPT_WRITEDATA = 10001;
inline constexpr int CURLOPT_URL = 10002;
inline constexpr int CURLOPT_PORT = 3;
inline constexpr int CURLOPT_TIMEOUT = 13;
inline constexpr int CURLOPT_INFILESIZE = 14;
inline constexpr int CURLOPT_WRITEFUNCTION = 20011;
inline constexpr int CURLOPT_READFUNCTION = 20012;
inline constexpr int CURLOPT_USERAGENT = 10018;
inline constexpr int CURLOPT_RANGE = 10007;
inline constexpr int CURLOPT_FOLLOWLOCATION = 52;
inline constexpr int CURLOPT_MAXREDIRS = 68;
inline constexpr int CURLOPT_POST = 47;
inline constexpr int CURLOPT_NOBODY = 44;
inline constexpr int CURLOPT_FAILONERROR = 45;
inline constexpr int CURLOPT_HTTPHEADER = 10023;
inline constexpr int CURLOPT_HEADERFUNCTION = 20079;
inline constexpr int CURLOPT_HEADERDATA = 10029;
inline constexpr int CURLOPT_NOSIGNAL = 99;
inline constexpr int CURLOPT_CONNECTTIMEOUT = 78;
inline constexpr int CURLOPT_SSL_VERIFYPEER = 64;
inline constexpr int CURLOPT_SSL_VERIFYHOST = 81;
inline constexpr int CURLOPT_XFERINFOFUNCTION = 20219;
inline constexpr int CURLOPT_XFERINFODATA = 10219;
inline constexpr int CURLOPT_NOPROGRESS = 43;
inline constexpr int CURLOPT_LOW_SPEED_LIMIT = 19;
inline constexpr int CURLOPT_LOW_SPEED_TIME = 20;

// libcurl info constants
inline constexpr int CURLINFO_RESPONSE_CODE = 0x200000 + 2;
inline constexpr int CURLINFO_CONTENT_LENGTH_DOWNLOAD_T = 0x600000 + 15;
inline constexpr int CURLINFO_EFFECTIVE_URL = 0x100000 + 1;

extern "C" {
    CURL* curl_easy_init(void);
    CURLcode curl_easy_setopt(CURL* curl, int option, ...);
    CURLcode curl_easy_perform(CURL* curl);
    void curl_easy_cleanup(CURL* curl);
    CURLcode curl_easy_getinfo(CURL* curl, int info, ...);
    const char* curl_easy_strerror(CURLcode code);
    void curl_easy_reset(CURL* curl);

    struct curl_slist* curl_slist_append(struct curl_slist*, const char*);
    void curl_slist_free_all(struct curl_slist*);

    CURLcode curl_global_init(long flags);
    void curl_global_cleanup(void);
}

} // namespace deox::shell::abi
