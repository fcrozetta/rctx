#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>

#include "registry.hpp"

using rctx::normalize_url;

TEST_CASE("normalize_url lowercases and strips a trailing .git") {
  CHECK(normalize_url("https://GitHub.com/Fer/Repo.git") == "https://github.com/fer/repo");
  CHECK(normalize_url("git@github.com:Fer/Repo.git") == "git@github.com:fer/repo");
}

TEST_CASE("normalize_url strips a trailing slash") {
  CHECK(normalize_url("https://github.com/fer/repo/") == "https://github.com/fer/repo");
}

TEST_CASE("normalize_url leaves an already-canonical url alone") {
  CHECK(normalize_url("https://github.com/fer/repo") == "https://github.com/fer/repo");
  CHECK(normalize_url("") == "");
}

TEST_CASE("registry_path honors RCTX_HOME") {
  setenv("RCTX_HOME", "/tmp/rctx-home-under-test", 1);
  CHECK(rctx::registry_path() ==
        std::filesystem::path("/tmp/rctx-home-under-test") / "registry.db");
  unsetenv("RCTX_HOME");
}
