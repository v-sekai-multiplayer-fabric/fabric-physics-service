# MuJoCo, from the `mujoco-riscv64` subtree.
#
# This file exists rather than `include()`ing the subtree's own `cmake/mujoco.cmake` because
# that one resolves `${CMAKE_SOURCE_DIR}/thirdparty/mujoco`, which is where MuJoCo sits when
# `mujoco-riscv64` is the top of the tree and not where it sits here. Patching the vendored
# copy to fix the path would break the byte-identical rule for a line of build glue, so the
# glue is ours and the subtree stays untouched.
#
# Everything else here is that file's content and its reasons, kept because they were paid for:
# MuJoCo's own tests, samples, GUI and Python bindings are off because nothing here needs them,
# and `-Wno-error` is on its target because `cmake/MujocoOptions.cmake` sets `-Werror`
# unconditionally and MuJoCo's own upstream warnings are not ours to patch.

# Where this repository's root is, which is not always where the including project's root is:
# `bench/` configures on its own so the benchmark can be built on a machine with no FoundationDB
# and no SQLite — the physics needs neither, and the Windows desk this is measured on has only
# one of them.
if(NOT DEFINED PHYSICS_ROOT)
  set(PHYSICS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
endif()

set(MUJOCO_ROOT ${PHYSICS_ROOT}/thirdparty/mujoco-riscv64/thirdparty/mujoco)

set(MUJOCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MUJOCO_TEST_PYTHON_UTIL OFF CACHE BOOL "" FORCE)
set(MUJOCO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MUJOCO_BUILD_SIMULATE OFF CACHE BOOL "" FORCE)
set(MUJOCO_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)

add_subdirectory(${MUJOCO_ROOT} ${CMAKE_BINARY_DIR}/mujoco-build)

# MSVC has no -Wno-error and rejects it as a numeric argument rather than ignoring it, and it
# does not set -Werror in the first place, so there is nothing to undo there.
if(NOT MSVC)
  target_compile_options(mujoco PRIVATE -Wno-error)
endif()

# The wiring that was moved here with MuJoCo: load an in-memory MJCF scene, step it, close it.
# It is a library and not a service — see `CLAUDE.md` on what it is not.
add_library(mj_physics STATIC
  ${PHYSICS_ROOT}/thirdparty/mujoco-riscv64/src/physics/mj_physics.c)
target_include_directories(mj_physics PUBLIC
  ${PHYSICS_ROOT}/thirdparty/mujoco-riscv64/src/physics)
target_link_libraries(mj_physics PUBLIC mujoco)
# Target-scoped, never global. A global `-Wextra` is what turned MuJoCo's own warnings into
# fatal errors the last time this was built, which the subtree's cmake comment records.
if(MSVC)
  target_compile_options(mj_physics PRIVATE /W4)
else()
  target_compile_options(mj_physics PRIVATE -Wall -Wextra)
endif()
