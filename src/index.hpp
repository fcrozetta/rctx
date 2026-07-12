// Derived FTS index over claims. This is a disposable accelerator: the DB can
// be deleted at any time and rebuilt from the committed claim files. Nothing
// load-bearing lives only here.
#pragma once

#include <filesystem>
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

// Create or rebuild the FTS5 index at db_path from the given claims.
void build_index(const std::filesystem::path& db_path, const std::vector<Claim>& claims);

// Full-text search the index; `query` is an FTS5 MATCH expression.
std::vector<SearchHit> search_index(const std::filesystem::path& db_path,
                                    const std::string& query);

}  // namespace rctx
