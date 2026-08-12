# Unit tests, and only unit tests.
#
# The game is checked by playing it: `ci/inside.sh` runs `queen check` against a real
# FoundationDB and holds the result to its invariants. That is the integration test and there is
# no second one here. What this directory holds is the arithmetic underneath — wire ids, names,
# places, the RNG — where a property over every input says something an example cannot.
#
# So this builds without FoundationDB, without SQLite and without a cluster, and it can be
# configured on its own:
#
#   cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test
#
# FuzzTest is fetched rather than vendored. `thirdparty/` is for subtrees from a
# `v-sekai-multiplayer-fabric` repository, and Google's is not one, so a copy here could never
# be the byte-identical mirror that rule is about. It is also why the tests are opt-in: a build
# of `queen` itself still clones nothing.
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED PROJECT_NAME)
  project(queen_tests CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  fuzztest
  GIT_REPOSITORY https://github.com/google/fuzztest.git
  # Pinned, so a test that passes today is the same test tomorrow. The same reason the ward
  # replays from a seed.
  GIT_TAG 2026-06-29)
FetchContent_MakeAvailable(fuzztest)

enable_testing()
include(GoogleTest)

# In fuzzing mode a property runs until it is stopped, which is a thing to run deliberately and
# not a thing CI waits on. Left off, each property runs as a unit test over a sample of its
# domain, which is what `ctest` here means.
add_executable(wire_test wire_test.cc)
target_include_directories(wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(wire_test PRIVATE -Wall -Wextra)
link_fuzztest(wire_test)
target_link_libraries(wire_test PRIVATE GTest::gmock GTest::gtest_main)
gtest_discover_tests(wire_test)

add_executable(rng_test rng_test.cc)
target_include_directories(rng_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(rng_test PRIVATE -Wall -Wextra)
link_fuzztest(rng_test)
target_link_libraries(rng_test PRIVATE GTest::gmock GTest::gtest_main)
gtest_discover_tests(rng_test)
