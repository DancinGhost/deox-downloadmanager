# Libraries of Deox Download Manager, from the bottom layer up. Each layer may
# only depend on the layers listed before it:
#
#   deoxdm_core      pure logic, no I/O, standard library only
#   deoxdm_platform  operating-system abstraction (files, terminal)
#   deoxdm_net       HTTP transfers on top of libcurl
#   deoxdm_engine    download orchestration: threads, retries, checkpoints
#
# Public headers live in include/deoxdm/<layer>/.

set(DEOXDM_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

# Functional core
add_library(deoxdm_core STATIC
    core/checkpoint.cpp
    core/checksum.cpp
    core/chunk_planner.cpp
    core/formatting.cpp
    core/http_headers.cpp
    core/progress.cpp
    core/state_machine.cpp
    core/url.cpp
)
target_include_directories(deoxdm_core PUBLIC ${DEOXDM_INCLUDE_DIR})
target_link_libraries(deoxdm_core PRIVATE deoxdm_warnings)

# Platform abstraction: one backend per operating system family
add_library(deoxdm_platform STATIC
    platform/${DEOXDM_PLATFORM_BACKEND}/file.cpp
    platform/${DEOXDM_PLATFORM_BACKEND}/terminal.cpp
)
target_include_directories(deoxdm_platform PUBLIC ${DEOXDM_INCLUDE_DIR})
target_link_libraries(deoxdm_platform PRIVATE deoxdm_warnings)

# Network layer
add_library(deoxdm_net STATIC
    net/curl_easy.cpp
    net/http_client.cpp
)
target_link_libraries(deoxdm_net
    PUBLIC deoxdm_core
    PRIVATE ${DEOXDM_CURL_LIBRARY} deoxdm_warnings
)

# Download engine
add_library(deoxdm_engine STATIC
    engine/checkpoint_store.cpp
    engine/download_engine.cpp
    engine/output_file.cpp
    engine/rate_limiter.cpp
)
target_link_libraries(deoxdm_engine
    PUBLIC deoxdm_core deoxdm_platform deoxdm_net Threads::Threads
    PRIVATE deoxdm_warnings
)
