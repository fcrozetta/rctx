// Host-global registry of repos rctx has seen, so a query in one repo can find
// claims in the others cloned on this machine. Lives at $RCTX_HOME/registry.db
// (default ~/.rctx), outside any repo. Keyed by absolute clone path, so git
// worktrees of one repo register as separate entries.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rctx {

struct RepoEntry {
  std::string url;
  std::string path;
  std::string branch;
  std::string default_branch;
};

std::filesystem::path registry_path();

// Upsert a repo by its absolute path.
void register_repo(const RepoEntry& entry);

std::vector<RepoEntry> list_repos();

}  // namespace rctx
