# SPDX-License-Identifier: MIT
# deoxdm_engine: download orchestration (worker threads, retries, rate
# limiting, checkpoints). Talks to the operating system only through
# deoxdm_platform and to the network only through deoxdm_net.

add_library(deoxdm_engine STATIC
    checkpoint_store.cpp
    download_engine.cpp
    output_file.cpp
    rate_limiter.cpp
)
target_link_libraries(deoxdm_engine
    PUBLIC deoxdm_core deoxdm_platform deoxdm_net Threads::Threads
    PRIVATE deoxdm_warnings
)
