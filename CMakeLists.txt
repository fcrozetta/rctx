cmake_minimum_required(VERSION 3.25)
project(rctx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(rctx
  src/main.cpp
  src/claim.cpp
  src/index.cpp
  src/git.cpp
  src/registry.cpp
  src/claim_id.cpp
)

target_link_libraries(rctx PRIVATE
  unofficial::sqlite3::sqlite3
  libgit2::libgit2package
  yaml-cpp::yaml-cpp
  CLI11::CLI11
  nlohmann_json::nlohmann_json
)

# Version string baked into the binary. Defaults to the project version;
# CI overrides it with the release tag (-DRCTX_VERSION=<tag>).
set(RCTX_VERSION "${PROJECT_VERSION}" CACHE STRING "rctx version string")
target_compile_definitions(rctx PRIVATE RCTX_VERSION="${RCTX_VERSION}")
