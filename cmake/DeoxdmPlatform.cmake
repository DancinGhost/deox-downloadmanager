# Selects the operating-system backend for the platform layer
# (include/deoxdm/platform/, src/platform/<backend>/).
#
# Sets DEOXDM_PLATFORM_BACKEND to the name of the backend directory.

if(WIN32)
    set(DEOXDM_PLATFORM_BACKEND windows)
    message(WARNING
        "Windows support is experimental: the platform backend exists but is not tested. "
        "See docs/PLATFORMS.md.")
elseif(UNIX)
    set(DEOXDM_PLATFORM_BACKEND posix)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING
            "${CMAKE_SYSTEM_NAME} uses the POSIX backend but only Linux is tested. "
            "See docs/PLATFORMS.md.")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}'. See docs/PLATFORMS.md for porting notes.")
endif()
