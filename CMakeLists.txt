cmake_minimum_required(VERSION 3.20)
project(deox 
    VERSION 1.0.0 
    DESCRIPTION "High-performance, memory-safe download manager in modern C++23 (Functional Core, Imperative Shell)"
    HOMEPAGE_URL "https://github.com/DancinGhost/deox-downloadmanager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BUILD_TESTING "Build the test suite" ON)

# Compiler warning flags
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(
        -Wall 
        -Wextra 
        -Wpedantic 
        -Wshadow 
        -Wconversion 
        -Wnon-virtual-dtor 
        -Wcast-align 
        -Wunused 
        -Woverloaded-virtual
    )
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Find libcurl or link directly against libcurl.so.4 on Linux
find_package(CURL QUIET)

# Functional Core Library (Zero external dependencies, pure domain logic)
add_library(deox_core STATIC
    src/core/url.cpp
    src/core/chunk_planner.cpp
    src/core/progress.cpp
    src/core/state_machine.cpp
    src/core/checkpoint.cpp
    src/core/checksum.cpp
    src/core/formatting.cpp
)

target_include_directories(deox_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Imperative Shell Library (I/O, OS, Network, Disk, Terminal)
add_library(deox_shell STATIC
    src/shell/file_writer.cpp
    src/shell/checkpoint_io.cpp
    src/shell/curl_wrapper.cpp
    src/shell/network_client.cpp
    src/shell/engine.cpp
    src/shell/terminal_ui.cpp
)

target_include_directories(deox_shell PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(deox_shell PUBLIC deox_core)

# Link curl: use CMake target if found, otherwise link libcurl.so.4
if(TARGET CURL::libcurl)
    target_link_libraries(deox_shell PRIVATE CURL::libcurl)
else()
    find_library(CURL_LIB NAMES curl libcurl libcurl.so.4 PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib)
    if(CURL_LIB)
        target_link_libraries(deox_shell PRIVATE ${CURL_LIB})
    else()
        target_link_libraries(deox_shell PRIVATE -l:libcurl.so.4)
    endif()
endif()

# Main CLI Executable
add_executable(deox src/main.cpp)
target_link_libraries(deox PRIVATE deox_shell)

# Tests
if(BUILD_TESTING)
    enable_testing()
    add_executable(deox_tests
        tests/test_main.cpp
        tests/test_url.cpp
        tests/test_chunk_planner.cpp
        tests/test_progress.cpp
        tests/test_state_machine.cpp
        tests/test_checkpoint.cpp
        tests/test_checksum.cpp
        tests/test_formatting.cpp
        tests/test_file_writer.cpp
        tests/test_integration.cpp
    )
    target_link_libraries(deox_tests PRIVATE deox_shell)
    add_test(NAME deox_all_tests COMMAND deox_tests)
endif()

# Install rules
include(GNUInstallDirs)
install(TARGETS deox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
