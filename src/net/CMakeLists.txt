# SPDX-License-Identifier: MIT
# deoxdm_net: HTTP(S) transfers on top of libcurl's easy interface.
# libcurl stays private to this layer: no libcurl type appears in
# include/deoxdm/net/.

add_library(deoxdm_net STATIC
    curl_easy.cpp
    http_client.cpp
)
target_link_libraries(deoxdm_net
    PUBLIC deoxdm_core
    PRIVATE ${DEOXDM_CURL_LIBRARY} deoxdm_warnings
)
