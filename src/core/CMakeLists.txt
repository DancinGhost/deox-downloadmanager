# SPDX-License-Identifier: MIT
# deoxdm_core: the functional core. Standard library only: no I/O, no
# operating-system calls, no third-party code. Must build unchanged on every
# platform.

add_library(deoxdm_core STATIC
    checkpoint.cpp
    checksum.cpp
    chunk_planner.cpp
    formatting.cpp
    http_headers.cpp
    progress.cpp
    state_machine.cpp
    url.cpp
)
target_include_directories(deoxdm_core PUBLIC ${DEOXDM_INCLUDE_DIR})
target_link_libraries(deoxdm_core PRIVATE deoxdm_warnings)
