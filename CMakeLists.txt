# SPDX-License-Identifier: MIT
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
option(DEOXDM_ENABLE_HARDENING "Build with exploit mitigations (stack protector, fortified libc calls, PIE, full RELRO)" ON)
option(DEOXDM_ALLOW_STATIC_COPYLEFT "Allow linking copyleft (LGPL) libraries such as Qt 6 statically. Only enable this if you will meet the LGPL relinking obligations." OFF)

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
include(DeoxdmDependencies)

# ---------------------------------------------------------------------------
# Layout
#
#   src/        libraries, one directory per layer (see src/CMakeLists.txt)
#   apps/       programs: cli/ (deox-dm), gui/ (deox-dm-gui)
#   tests/      test suites
#   packaging/  installation of data files, per platform
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(DEOXDM_BUILD_CLI OR BUILD_TESTING)
    add_subdirectory(apps/cli)
endif()

if(DEOXDM_GUI_ENABLED)
    add_subdirectory(apps/gui)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(packaging)

message(STATUS "Deox Download Manager ${PROJECT_VERSION}: platform backend '${DEOXDM_PLATFORM_BACKEND}', "
               "CLI ${DEOXDM_BUILD_CLI}, GUI ${DEOXDM_GUI_ENABLED}, tests ${BUILD_TESTING}")
