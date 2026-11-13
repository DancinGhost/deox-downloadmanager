# SPDX-License-Identifier: MIT
# Compiler warnings, hardening and sanitizers for Deox Download Manager's own
# targets.
#
# Provides the interface target `deoxdm_warnings`; link it PRIVATE into every
# first-party target. Generated sources (Qt moc) are not affected because
# they inherit no flags from it. Besides the warnings it carries the
# exploit mitigations (DEOXDM_ENABLE_HARDENING).

include(CheckCXXCompilerFlag)
include(CheckPIESupported)

if(DEOXDM_ENABLE_ASAN AND DEOXDM_ENABLE_TSAN)
    message(FATAL_ERROR "DEOXDM_ENABLE_ASAN and DEOXDM_ENABLE_TSAN cannot be combined")
endif()

if(MSVC)
    if(DEOXDM_ENABLE_ASAN)
        add_compile_options(/fsanitize=address)
    endif()
    if(DEOXDM_ENABLE_TSAN)
        message(FATAL_ERROR "ThreadSanitizer is not available with MSVC")
    endif()
else()
    if(DEOXDM_ENABLE_ASAN)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    elseif(DEOXDM_ENABLE_TSAN)
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    endif()
endif()

add_library(deoxdm_warnings INTERFACE)
if(MSVC)
    target_compile_options(deoxdm_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(deoxdm_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wnon-virtual-dtor
        -Wcast-align
        -Woverloaded-virtual
    )
endif()

# Exploit mitigations. A download manager parses data from arbitrary servers,
# so a memory-safety bug that slipped through should be as hard as possible to
# turn into code execution.
if(DEOXDM_ENABLE_HARDENING)
    if(MSVC)
        target_compile_options(deoxdm_warnings INTERFACE /GS /guard:cf)
        target_link_options(deoxdm_warnings INTERFACE /guard:cf /DYNAMICBASE /NXCOMPAT)
    else()
        # Position-independent executables, so that ASLR covers the program too.
        check_pie_supported(LANGUAGES CXX)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)

        target_compile_options(deoxdm_warnings INTERFACE -fstack-protector-strong)
        foreach(flag -fstack-clash-protection -fcf-protection=full)
            string(MAKE_C_IDENTIFIER "DEOXDM_HAS${flag}" flag_var)
            check_cxx_compiler_flag(${flag} ${flag_var})
            if(${flag_var})
                target_compile_options(deoxdm_warnings INTERFACE ${flag})
            endif()
        endforeach()

        # Fortified libc calls need optimization, and the sanitizers do their
        # own (better) checking of the same calls.
        if(NOT DEOXDM_ENABLE_ASAN AND NOT DEOXDM_ENABLE_TSAN)
            target_compile_options(deoxdm_warnings INTERFACE
                "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=3>")
        endif()

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # Full RELRO: the GOT is read-only after start-up.
            target_link_options(deoxdm_warnings INTERFACE
                -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
        endif()
    endif()
endif()
