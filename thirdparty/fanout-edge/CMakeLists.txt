# The fan-out edge. Not buildable yet, and saying so beats a target that never runs.
#
# It needs three things it does not have: predictive_bvh.h (codegen, lean-spatial-oracle),
# the entity packet codec (codegen, lean-entity-packet), and a ring subscription to feed it
# a tick. Copying the generated headers here would put one decision in two places.
cmake_minimum_required(VERSION 3.22)
project(fabric_fanout_edge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WEFT_GEN_DIR "" CACHE PATH "directory holding predictive_bvh.h and the packet codec")

if(WEFT_GEN_DIR)
  add_library(fanout-edge STATIC src/fanout.cpp)
  target_include_directories(fanout-edge PRIVATE src ${WEFT_GEN_DIR})
  target_compile_options(fanout-edge PRIVATE -Wall -Wextra)
else()
  message(STATUS "fanout-edge not built: set -DWEFT_GEN_DIR. See #1.")
endif()
