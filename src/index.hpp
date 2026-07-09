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

// Create or rebuild the FTS5 index at db_path from the given claims.
void build_index(const std::filesystem::path& db_path, const std::vector<Claim>& claims);

// Full-text search the index; `query` is an FTS5 MATCH expression.
std::vector<SearchHit> search_index(const std::filesystem::path& db_path,
                                    const std::string& query);

}  // namespace rctx
