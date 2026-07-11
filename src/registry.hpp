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

// Remove the entry with this exact (canonical) path. Returns true if a row was
// deleted, false if nothing matched (or no registry exists yet).
bool forget_repo(const std::string& path);

// Remove every entry whose path no longer exists on disk. Returns the paths
// that were pruned.
std::vector<std::string> prune_missing_repos();

}  // namespace rctx
