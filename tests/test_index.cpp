#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "claim.hpp"
#include "index.hpp"

namespace fs = std::filesystem;
using namespace rctx;

namespace {

// A fresh, empty temp directory unique to this process and call.
fs::path uniq_dir(const char* label) {
  static int counter = 0;
  const fs::path p = fs::temp_directory_path() /
                     ("rctx_test_" + std::string(label) + "_" + std::to_string(::getpid()) + "_" +
                      std::to_string(counter++));
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

Claim mk(std::string id, std::string body) {
  Claim c;
  c.id = std::move(id);
  c.volatility = "stable";
  c.body = std::move(body);
  c.source_path = c.id + ".md";
  return c;
}

}  // namespace

TEST_CASE("build_index then search_index round-trips claims") {
  const fs::path dir = uniq_dir("idx");
  const fs::path db = dir / "index.db";

  std::vector<Claim> claims;
  claims.push_back(mk("build/requires-env", "the build needs environment variables"));
  claims.push_back(mk("api/contract", "openapi schema for downstream clients"));
  build_index(db, claims);

  auto hits = search_index(db, "environment");
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].id == "build/requires-env");
  CHECK(hits[0].source_path == "build/requires-env.md");

  CHECK(search_index(db, "nonexistentterm").empty());
  fs::remove_all(dir);
}

TEST_CASE("index_stale sees missing db, edited claims, and a removed tree") {
  const fs::path dir = uniq_dir("stale");
  const fs::path claims_root = dir / ".rctx" / "claims";
  const fs::path claim = claims_root / "api" / "c.md";
  fs::create_directories(claim.parent_path());
  { std::ofstream(claim) << "---\nvolatility: stable\n---\nbody\n"; }
  const fs::path db = dir / "index.db";
  const auto sec = [](int n) { return std::chrono::seconds(n); };

  // No db yet: stale.
  CHECK(index_stale(db, claims_root));

  // Build the db, then drive mtimes explicitly rather than racing the wall
  // clock: pin the whole claims tree to a base time and stamp the index after
  // it, so the index is unambiguously fresh.
  build_index(db, load_claims(claims_root));
  const auto base = fs::last_write_time(claim);
  fs::last_write_time(claim, base);
  fs::last_write_time(claim.parent_path(), base);
  fs::last_write_time(claims_root, base);
  fs::last_write_time(db, base + sec(5));
  CHECK_FALSE(index_stale(db, claims_root));

  // A claim edited after the index: stale.
  fs::last_write_time(claim, base + sec(10));
  CHECK(index_stale(db, claims_root));

  // The whole claims tree removed: stale regardless of mtimes (rebuild to empty).
  fs::remove_all(claims_root);
  CHECK(index_stale(db, claims_root));

  fs::remove_all(dir);
}

TEST_CASE("refresh_index stamps the index with the source snapshot time") {
  const fs::path dir = uniq_dir("stamp");
  const fs::path claims_root = dir / ".rctx" / "claims";
  const fs::path claim = claims_root / "api" / "c.md";
  fs::create_directories(claim.parent_path());
  { std::ofstream(claim) << "---\nvolatility: stable\n---\nbody\n"; }
  const fs::path db = dir / "index.db";

  // Backdate the whole source tree an hour into the past.
  const auto past = fs::file_time_type::clock::now() - std::chrono::hours(1);
  fs::last_write_time(claim, past);
  fs::last_write_time(claim.parent_path(), past);
  fs::last_write_time(claims_root, past);

  CHECK(refresh_index(db, claims_root) == 1);

  // The index carries the source snapshot time (~1h ago), not the build-finish
  // time (~now); that is what stops an edit-during-rebuild from being masked.
  CHECK(fs::last_write_time(db) < fs::file_time_type::clock::now() - std::chrono::minutes(1));
  CHECK_FALSE(index_stale(db, claims_root));

  fs::remove_all(dir);
}
