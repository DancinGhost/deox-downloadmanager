# SPDX-License-Identifier: MIT
# Libraries of Deox Download Manager, from the bottom layer up. Each layer may
# only depend on the layers listed before it:
#
#   deoxdm_core      pure logic, no I/O, standard library only
#   deoxdm_platform  operating-system abstraction (files, paths, terminal, interrupts)
#   deoxdm_net       HTTP transfers on top of libcurl
#   deoxdm_engine    download orchestration: threads, retries, checkpoints
#
# Public headers live in include/deoxdm/<layer>/, sources in src/<layer>/.
# Each layer's directory has its own CMakeLists.txt.

set(DEOXDM_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(net)
add_subdirectory(engine)
