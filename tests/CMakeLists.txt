# SPDX-License-Identifier: MIT
# Test suites
#
#   deoxdm_unit_tests         unit tests for core, platform, and engine (tests/unit)
#   deoxdm_integration_tests  end-to-end engine tests against a local mock HTTP server (tests/integration)
#   deoxdm_cli_tests          command-line option and front-end tests (tests/cli)
#   deoxdm_gui_tests          desktop application tests (tests/gui), headless
#
# Run them with `ctest --test-dir build --output-on-failure` or directly.

if(MSVC)
    set(DEOXDM_TEST_WARNINGS /W4 /utf-8)
else()
    # The tests configure the mock server with designated initializers that
    # name only the options they care about, so that one warning is switched
    # off. It has to come after -Wextra, which would otherwise turn it back on.
    set(DEOXDM_TEST_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-missing-field-initializers)
endif()

set(DEOXDM_UNIT_TESTS
    unit/test_checkpoint.cpp
    unit/test_checksum.cpp
    unit/test_chunk_planner.cpp
    unit/test_formatting.cpp
    unit/test_http_headers.cpp
    unit/test_output_file.cpp
    unit/test_platform.cpp
    unit/test_progress.cpp
    unit/test_rate_limiter.cpp
    unit/test_state_machine.cpp
    unit/test_url.cpp
)

add_executable(deoxdm_unit_tests support/test_main.cpp ${DEOXDM_UNIT_TESTS})
target_include_directories(deoxdm_unit_tests PRIVATE support)
target_link_libraries(deoxdm_unit_tests PRIVATE deoxdm_engine)
target_compile_options(deoxdm_unit_tests PRIVATE ${DEOXDM_TEST_WARNINGS})
add_test(NAME deoxdm_unit_tests COMMAND deoxdm_unit_tests)

if(DEOXDM_BUILD_CLI)
    add_executable(deoxdm_cli_tests support/test_main.cpp cli/test_cli.cpp)
    target_include_directories(deoxdm_cli_tests PRIVATE support)
    target_link_libraries(deoxdm_cli_tests PRIVATE deoxdm_cli_lib)
    target_compile_options(deoxdm_cli_tests PRIVATE ${DEOXDM_TEST_WARNINGS})
    add_test(NAME deoxdm_cli_tests COMMAND deoxdm_cli_tests)
endif()

# The mock HTTP server uses BSD sockets; it has not been ported to Winsock.
if(UNIX)
    add_library(deoxdm_test_support STATIC support/mock_http_server.cpp)
    target_include_directories(deoxdm_test_support PUBLIC support)
    target_compile_options(deoxdm_test_support PRIVATE ${DEOXDM_TEST_WARNINGS})

    set(DEOXDM_INTEGRATION_TESTS
        integration/test_redirects_and_filenames.cpp
        integration/test_resume.cpp
        integration/test_retries_and_errors.cpp
        integration/test_transfers.cpp
    )

    add_executable(deoxdm_integration_tests support/test_main.cpp ${DEOXDM_INTEGRATION_TESTS})
    target_link_libraries(deoxdm_integration_tests PRIVATE deoxdm_test_support deoxdm_engine)
    target_compile_options(deoxdm_integration_tests PRIVATE ${DEOXDM_TEST_WARNINGS})
    add_test(NAME deoxdm_integration_tests COMMAND deoxdm_integration_tests)
endif()

if(DEOXDM_GUI_ENABLED AND UNIX)
    add_executable(deoxdm_gui_tests gui/test_gui.cpp)
    target_link_libraries(deoxdm_gui_tests PRIVATE deoxdm_test_support deoxdm_gui_lib)
    target_compile_options(deoxdm_gui_tests PRIVATE ${DEOXDM_TEST_WARNINGS})
    add_test(NAME deoxdm_gui_tests COMMAND deoxdm_gui_tests)
    # Runs without a display server.
    set_tests_properties(deoxdm_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
