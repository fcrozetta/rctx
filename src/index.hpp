// Derived FTS index over claims. This is a disposable accelerator: the DB can
// be deleted at any time and rebuilt from the committed claim files. Nothing
// load-bearing lives only here.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "claim.hpp"

namespace rctx {

struct SearchHit {
  std::string id;
  std::string source_path;
  std::string snippet;
};

// Default on-disk location for a repo's FTS index. A cache is never stored
// inside the repo: this returns "<cache>/rctx/<basename>-<hash>/index.db",
// where <cache> is $XDG_CACHE_HOME or ~/.cache and the hash keys off
// `repo_root` so each repo (and each git worktree) gets its own disposable,
// wipe-safe index.
std::filesystem::path default_index_path(const std::filesystem::path& repo_root);

// True if the index at db_path is missing, or older than anything in the
// claims tree (files and directories are both checked, so adds, edits and
// deletes all count). Used to lazily rebuild before a query so results are
// never stale without the user remembering to run `index`.
bool index_stale(const std::filesystem::path& db_path, const std::filesystem::path& claims_dir);

// Create or rebuild the FTS5 index at db_path from the given claims. If `stamp`
// is set, the index's mtime is set to it (clamped to no later than the actual
// build-finish time, so it never lands in the future) on the temp file *before*
// the atomic rename, so the stamp is published atomically with the new index.
void build_index(const std::filesystem::path& db_path, const std::vector<Claim>& claims,
                 std::optional<std::filesystem::file_time_type> stamp = std::nullopt);

// Load the claims under claims_dir and (re)build the index at db_path from them.
// Stamps the index's mtime with the newest source mtime observed *before* the
// load, not the build-finish time, so a claim edited during the rebuild (whose
// mtime lands after the snapshot) is seen as stale on the next check instead of
// being masked by a freshly-written index. Returns the number of claims indexed.
std::size_t refresh_index(const std::filesystem::path& db_path,
                          const std::filesystem::path& claims_dir);

// Full-text search the index; `query` is an FTS5 MATCH expression.
std::vector<SearchHit> search_index(const std::filesystem::path& db_path,
                                    const std::string& query);

}  // namespace rctx
