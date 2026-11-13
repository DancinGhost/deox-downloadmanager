# SPDX-License-Identifier: MIT
# Third-party dependencies of Deox Download Manager.
#
# Sets:
#   DEOXDM_CURL_LIBRARY  what deoxdm_net links for libcurl
#   DEOXDM_GUI_ENABLED   ON when Qt 6 Widgets was found and the GUI will be built
#
# See THIRD_PARTY_NOTICES.md for the licensing policy enforced here: copyleft
# (LGPL) libraries are only linked dynamically unless
# DEOXDM_ALLOW_STATIC_COPYLEFT is set.

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# libcurl: only the runtime library is required. The network layer declares
# the few libcurl functions it calls itself (src/net/curl_abi.hpp), so the
# development package is optional.
# ---------------------------------------------------------------------------
find_package(CURL QUIET)
if(TARGET CURL::libcurl)
    set(DEOXDM_CURL_LIBRARY CURL::libcurl)
else()
    # Without a CMake package, look for the runtime library under its usual
    # names: libcurl.so.4 on Linux, libcurl.4.dylib on macOS, libcurl.dll(.a)
    # / libcurl.lib on Windows.
    find_library(DEOXDM_CURL_LIBRARY NAMES curl libcurl libcurl.so.4 libcurl-gnutls.so.4 libcurl.4.dylib)
    if(NOT DEOXDM_CURL_LIBRARY)
        message(FATAL_ERROR "libcurl was not found. Install it, e.g. 'apt install libcurl4', 'dnf install libcurl', "
                            "'brew install curl' or 'vcpkg install curl', or point CMAKE_PREFIX_PATH at it.")
    endif()
endif()
message(STATUS "Using libcurl: ${DEOXDM_CURL_LIBRARY}")

# A static libcurl usually pulls in LGPL libraries (GnuTLS, libidn2, ...).
set(_deoxdm_curl_static OFF)
if(TARGET CURL::libcurl)
    get_target_property(_deoxdm_curl_type CURL::libcurl TYPE)
    if(_deoxdm_curl_type STREQUAL "STATIC_LIBRARY")
        set(_deoxdm_curl_static ON)
    endif()
elseif(DEOXDM_CURL_LIBRARY MATCHES "\\.a$")
    set(_deoxdm_curl_static ON)
endif()
if(_deoxdm_curl_static AND NOT DEOXDM_ALLOW_STATIC_COPYLEFT)
    message(WARNING "libcurl is a static library. Its TLS and IDN dependencies may be LGPL-licensed; "
                    "statically linking them adds relinking obligations. Prefer a shared libcurl.")
endif()

# ---------------------------------------------------------------------------
# Qt 6 Widgets, for the desktop application only
# ---------------------------------------------------------------------------
set(DEOXDM_GUI_ENABLED OFF)
if(NOT DEOXDM_BUILD_GUI STREQUAL "OFF")
    find_package(Qt6 6.4 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        # Qt 6 open-source edition is LGPL-3.0: only dynamic linking is allowed.
        get_target_property(_deoxdm_qt_type Qt6::Core TYPE)
        if(_deoxdm_qt_type STREQUAL "STATIC_LIBRARY" AND NOT DEOXDM_ALLOW_STATIC_COPYLEFT)
            message(FATAL_ERROR "Qt 6 is a static build. Qt is LGPL-3.0 licensed and this project only links "
                                "copyleft libraries dynamically. Use a shared Qt 6, or pass "
                                "-DDEOXDM_ALLOW_STATIC_COPYLEFT=ON if you hold a commercial Qt license or will meet "
                                "the LGPL relinking obligations.")
        endif()
        set(DEOXDM_GUI_ENABLED ON)
    elseif(DEOXDM_BUILD_GUI STREQUAL "ON")
        message(FATAL_ERROR "DEOXDM_BUILD_GUI=ON but Qt 6 Widgets was not found. Install it, e.g. "
                            "'apt install qt6-base-dev', or point CMAKE_PREFIX_PATH at your Qt installation.")
    else()
        message(STATUS "Qt 6 Widgets not found: the desktop application (deox-dm-gui) will not be built")
    endif()
endif()
