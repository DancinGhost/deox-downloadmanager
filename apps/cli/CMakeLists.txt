# deox-dm: command line front end

# Argument parsing and console output, kept in a library so the tests can use them.
add_library(deoxdm_cli_lib STATIC
    options.cpp
    options.hpp
    terminal_ui.cpp
    terminal_ui.hpp
)
target_include_directories(deoxdm_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(deoxdm_cli_lib
    PUBLIC deoxdm_engine
    PRIVATE deoxdm_warnings
)

if(DEOXDM_BUILD_CLI)
    add_executable(deox-dm main.cpp)
    target_link_libraries(deox-dm PRIVATE deoxdm_cli_lib deoxdm_warnings)
    install(TARGETS deox-dm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
