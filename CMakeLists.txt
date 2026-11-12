cmake_minimum_required(VERSION 3.20)

# The version lives in one place: include/deoxdm/version.hpp
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/deoxdm/version.hpp" _deoxdm_version_line REGEX "VERSION_STRING = \"")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" DEOXDM_VERSION "${_deoxdm_version_line}")

project(deox_downloadmanager
    VERSION ${DEOXDM_VERSION}
    DESCRIPTION "Deox Download Manager: segmented, resumable HTTP(S) download manager"
    HOMEPAGE_URL "https://github.com/DancinGhost/deox-downloadmanager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All programs end up directly in the build directory:
# build/deox-dm, build/deox-dm-gui, build/deoxdm_tests, ...
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build the test suite" ON)
option(DEOXDM_BUILD_CLI "Build the command line program (deox-dm)" ON)
set(DEOXDM_BUILD_GUI "AUTO" CACHE STRING "Build the Qt 6 desktop application (deox-dm-gui): ON, OFF or AUTO (only if Qt 6 is found)")
set_property(CACHE DEOXDM_BUILD_GUI PROPERTY STRINGS AUTO ON OFF)
option(DEOXDM_ENABLE_ASAN "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(DEOXDM_ENABLE_TSAN "Build with ThreadSanitizer" OFF)

# Backward-compatibility option aliases
if(ENABLE_ASAN)
    set(DEOXDM_ENABLE_ASAN ON)
endif()
if(ENABLE_TSAN)
    set(DEOXDM_ENABLE_TSAN ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DeoxdmCompilerOptions)
include(DeoxdmPlatform)
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# libcurl: only the runtime library is required. The network layer declares
# the few libcurl functions it calls itself (src/net/curl_abi.hpp), so the
# development package is optional on Linux.
find_package(CURL QUIET)
if(TARGET CURL::libcurl)
    set(DEOXDM_CURL_LIBRARY CURL::libcurl)
else()
    find_library(DEOXDM_CURL_LIBRARY NAMES curl libcurl libcurl.so.4 libcurl-gnutls.so.4)
    if(NOT DEOXDM_CURL_LIBRARY)
        message(FATAL_ERROR "libcurl was not found. Install it, e.g. 'apt install libcurl4' or 'dnf install libcurl'.")
    endif()
endif()
message(STATUS "Using libcurl: ${DEOXDM_CURL_LIBRARY}")

# ---------------------------------------------------------------------------
# Libraries (src/) and programs (apps/)
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(DEOXDM_BUILD_CLI OR BUILD_TESTING)
    add_subdirectory(apps/cli)
endif()

set(DEOXDM_GUI_ENABLED OFF)
if(NOT DEOXDM_BUILD_GUI STREQUAL "OFF")
    find_package(Qt6 6.4 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        set(DEOXDM_GUI_ENABLED ON)
        add_subdirectory(apps/gui)
    elseif(DEOXDM_BUILD_GUI STREQUAL "ON")
        message(FATAL_ERROR "DEOXDM_BUILD_GUI=ON but Qt 6 Widgets was not found. Install it, e.g. 'apt install qt6-base-dev'.")
    else()
        message(STATUS "Qt 6 Widgets not found: the desktop application (deox-dm-gui) will not be built")
    endif()
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Desktop integration files (Linux/BSD desktops following freedesktop.org)
if(DEOXDM_GUI_ENABLED AND UNIX AND NOT APPLE)
    install(FILES packaging/linux/io.github.DancinGhost.DeoxDownloadManager.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/io.github.DancinGhost.DeoxDownloadManager.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

message(STATUS "Deox Download Manager ${PROJECT_VERSION}: platform backend '${DEOXDM_PLATFORM_BACKEND}', "
               "CLI ${DEOXDM_BUILD_CLI}, GUI ${DEOXDM_GUI_ENABLED}, tests ${BUILD_TESTING}")
