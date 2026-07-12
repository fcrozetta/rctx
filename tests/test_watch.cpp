#include <doctest/doctest.h>

#include "watch.hpp"

using rctx::watch_matches;

TEST_CASE("watch_matches: an exact path matches") {
  CHECK(watch_matches(".env.example", ".env.example"));
  CHECK(watch_matches("docs/api/spec.md", "docs/api/spec.md"));
}

TEST_CASE("watch_matches: a pattern with no slash matches the file name anywhere") {
  CHECK(watch_matches("*.json", "openapi.json"));
  CHECK(watch_matches("*.json", "sub/dir/openapi.json"));
  CHECK_FALSE(watch_matches("*.json", "openapi.yaml"));
}

TEST_CASE("watch_matches: ** crosses directory separators") {
  CHECK(watch_matches("docs/**", "docs/x.md"));
  CHECK(watch_matches("docs/**", "docs/a/b/c.md"));
  CHECK_FALSE(watch_matches("docs/**", "src/x.md"));
}

TEST_CASE("watch_matches: a single * stays within one path segment") {
  CHECK(watch_matches("src/*.cpp", "src/main.cpp"));
  CHECK_FALSE(watch_matches("src/*.cpp", "src/sub/main.cpp"));
}
