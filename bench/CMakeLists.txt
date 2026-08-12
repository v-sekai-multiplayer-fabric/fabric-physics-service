# The one-core benchmark, buildable on its own.
#
#   cmake -S bench -B build-bench -DCMAKE_BUILD_TYPE=Release && cmake --build build-bench -j
#
# It is its own project, not only a subdirectory, because the physics needs neither FoundationDB
# nor SQLite and the root `CMakeLists.txt` requires both to configure at all. The machine this is
# measured on is the machine the headset is plugged into — `fabric-godot-service` drives an HMD
# from the Windows binary `fabric-godot-core` builds — and that machine has FoundationDB but no
# SQLite development headers. A benchmark that could only be configured where the whole store
# builds could not be run beside the client it is for.
#
# Included from the root as a subdirectory too, so a full Linux build still gets it.
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(fabric_physics_bench C CXX)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(PHYSICS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
  include(${PHYSICS_ROOT}/cmake/mujoco.cmake)
endif()

# The generated headers, where the fan-out edge expects to find them.
#
# `fanout.cpp` includes "gen/xr_grid_entity_packet.h", and `WEFT_GEN_DIR` is the knob its own
# CMakeLists offers for saying where that directory is. Copying the emitted header into a `gen/`
# under the build tree is what that knob is for: the header stays owned by `lean-entity-packet`,
# and this repository's checked-in copy of it is the subtree, not a second edit of the file.
set(WEFT_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen-include)
configure_file(${PHYSICS_ROOT}/thirdparty/entity-packet/xr_grid_entity_packet.h
               ${WEFT_GEN_DIR}/gen/xr_grid_entity_packet.h COPYONLY)
configure_file(${PHYSICS_ROOT}/thirdparty/spatial-oracle/predictive_bvh.h
               ${WEFT_GEN_DIR}/predictive_bvh.h COPYONLY)
# `predictive_bvh.h` includes <thirdparty/misc/r128.h>, so the include root has to be a place
# where that path resolves. It is the fixed-point type the BVH's own arithmetic is written in.
configure_file(${PHYSICS_ROOT}/thirdparty/spatial-oracle/thirdparty/misc/r128.h
               ${WEFT_GEN_DIR}/thirdparty/misc/r128.h COPYONLY)

# The interest filter and the packing, unmodified, from `fabric-fanout-edge`. `pbvh_adapter.cpp`
# supplies the one helper `predictive_bvh.h` declares and leaves to its caller.
add_library(fanout_edge STATIC
  ${PHYSICS_ROOT}/thirdparty/fanout-edge/src/fanout.cpp
  pbvh_adapter.cpp
  fanout_bridge.cpp)
target_include_directories(fanout_edge PUBLIC
  ${WEFT_GEN_DIR} ${PHYSICS_ROOT}/thirdparty/fanout-edge/src ${CMAKE_CURRENT_SOURCE_DIR})

# Does the simulation replay identically? The question deterministic lockstep rests on, asked
# before anything is built on the answer. It links the scene and the physics and nothing else —
# no packet, no fan-out, no interest — because a probe that carried the wire would be asking
# whether the wire is deterministic, which is a different and much easier question.
add_executable(determinism_probe determinism_probe.c scene.c)
target_include_directories(determinism_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(determinism_probe PRIVATE mj_physics)
# `fopen` and `sscanf` are standard C and MSVC's deprecation of them is not. The `_s` variants
# do not exist elsewhere, so taking the warning at its word would make this file Windows-only
# to silence a warning about portability.
if(MSVC)
  target_compile_definitions(determinism_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(bench_players bench_players.c scene.c)
target_include_directories(bench_players PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${PHYSICS_ROOT}/src)
target_link_libraries(bench_players PRIVATE mj_physics fanout_edge)

# The one MuJoCo test `mujoco-riscv64` carried, which its own README says was never built or run.
# Building the engine here is what makes running it free, and a test that has never run is not a
# test.
enable_testing()
add_executable(mj_freefall ${PHYSICS_ROOT}/thirdparty/mujoco-riscv64/test/unit/test_mj_physics_freefall.c)
target_link_libraries(mj_freefall PRIVATE mj_physics)
add_test(NAME mj_freefall COMMAND mj_freefall)

# `m` is a separate library on Unix and part of the runtime on Windows, where asking for it is
# an error rather than a no-op.
if(NOT WIN32)
  target_link_libraries(bench_players PRIVATE m)
  target_link_libraries(mj_freefall PRIVATE m)
  target_link_libraries(determinism_probe PRIVATE m)
endif()

# MuJoCo builds as a shared library, and Windows looks for a DLL beside the executable rather
# than along a build-tree path. Without this the tests fail with 0xc0000135 — "the dependency
# was not found" — which reads like a broken binary and is a missing copy.
foreach(t bench_players mj_freefall determinism_probe)
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${t}> $<TARGET_RUNTIME_DLLS:${t}>
    COMMAND_EXPAND_LISTS)
endforeach()

foreach(t bench_players mj_freefall fanout_edge determinism_probe)
  # Target-scoped, never global: a global `-Wextra` is what made MuJoCo's own warnings fatal the
  # last time this was built, which the subtree's cmake comment records.
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
endforeach()
