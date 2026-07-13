---
volatility: stable
watches:
  - vcpkg.json
  - CMakeLists.txt
  - CMakePresets.json
reverify: "test -f vcpkg.json && grep -q builtin-baseline vcpkg.json"
---

Building rctx needs CMake >= 3.25, Ninja, and vcpkg. Dependencies (sqlite3 with
fts5, libgit2, yaml-cpp, cli11, nlohmann-json, and doctest for the tests) are
pinned by the `builtin-baseline` in `vcpkg.json`, so a clean build is
reproducible. Changing the baseline or the dependency set changes what a fresh
checkout resolves; review it when these files move.
