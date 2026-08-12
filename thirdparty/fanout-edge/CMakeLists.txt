# The fan-out edge.
#
# It needs two generated headers, and both now exist: predictive_bvh.h in lean-spatial-oracle,
# and xr_grid_entity_packet.h emitted by lean-entity-packet's `packet_emit` — the second of
# which this file used to say was missing, which is why nothing here had ever compiled.
#
# They are still not copied in. WEFT_GEN_DIR points at wherever the consumer put them, because
# copying a generated header here would put one decision in two places.
#
# What it still has not got is a ring subscription to feed it a tick, so this is a library and
# not a process.
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
