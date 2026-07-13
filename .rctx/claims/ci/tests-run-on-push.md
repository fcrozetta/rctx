---
volatility: stable
watches:
  - .github/workflows/ci.yml
  - CMakeLists.txt
reverify: "test -f .github/workflows/ci.yml"
---

CI builds rctx and runs the ctest suite on every push and pull request
(`.github/workflows/ci.yml`). `RCTX_BUILD_TESTS` defaults on, so an ordinary
build includes the tests; release builds pass `-DRCTX_BUILD_TESTS=OFF` to skip
the test binary. Keep the suite green before merging, and land new behavior
with a test.
