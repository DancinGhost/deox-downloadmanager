# Compiler warnings and sanitizers for Deox Download Manager's own targets.
#
# Provides the interface target `deoxdm_warnings`; link it PRIVATE into every
# first-party target. Generated sources (Qt moc) are not affected because
# they inherit no flags from it.

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
