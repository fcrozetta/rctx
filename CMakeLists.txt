cmake_minimum_required(VERSION 3.25)
project(rctx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RCTX_BUILD_TESTS "Build the rctx test suite" ON)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Core library: everything except the CLI entry point, so the binary and the
# test suite link the exact same code.
add_library(rctx_core STATIC
  src/claim.cpp
  src/index.cpp
  src/git.cpp
  src/registry.cpp
  src/claim_id.cpp
  src/watch.cpp
)
target_include_directories(rctx_core PUBLIC src)
target_link_libraries(rctx_core PUBLIC
  unofficial::sqlite3::sqlite3
  libgit2::libgit2package
  yaml-cpp::yaml-cpp
  nlohmann_json::nlohmann_json
)

add_executable(rctx src/main.cpp)
target_link_libraries(rctx PRIVATE rctx_core CLI11::CLI11)

# Version string baked into the binary. Defaults to the project version;
# CI overrides it with the release tag (-DRCTX_VERSION=<tag>).
set(RCTX_VERSION "${PROJECT_VERSION}" CACHE STRING "rctx version string")
target_compile_definitions(rctx PRIVATE RCTX_VERSION="${RCTX_VERSION}")

if(RCTX_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  enable_testing()
  add_executable(rctx_tests
    tests/test_main.cpp
    tests/test_claim.cpp
    tests/test_watch.cpp
    tests/test_registry.cpp
    tests/test_index.cpp
  )
  target_link_libraries(rctx_tests PRIVATE rctx_core doctest::doctest)
  add_test(NAME rctx_tests COMMAND rctx_tests)
endif()
