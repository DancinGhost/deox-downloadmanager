# SPDX-License-Identifier: MIT
# deoxdm_platform: the operating-system abstraction.
#
#   common/      operating-system independent parts, built everywhere
#   <backend>/   one directory per OS family, chosen by cmake/DeoxdmPlatform.cmake
#   backend.hpp  what a backend must provide besides the public headers
#
# The only directory in the code base that may include OS headers.

add_library(deoxdm_platform STATIC
    common/file.cpp
    common/interrupt.cpp
    common/path.cpp
    common/terminal.cpp
    ${DEOXDM_PLATFORM_BACKEND}/file.cpp
    ${DEOXDM_PLATFORM_BACKEND}/interrupt.cpp
    ${DEOXDM_PLATFORM_BACKEND}/terminal.cpp
)
target_include_directories(deoxdm_platform
    PUBLIC ${DEOXDM_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(deoxdm_platform PRIVATE deoxdm_warnings)
