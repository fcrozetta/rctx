// Thin libgit2 helpers for the drift check. Requires git_libgit2_init() to have
// been called (once, in main) before use.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rctx {

// Short name of the currently checked-out branch (empty if detached/unavailable).
std::string current_branch(const std::filesystem::path& repo_path);

// Paths that differ between `base_ref` (e.g. "main") and the working tree.
std::vector<std::string> changed_files_vs(const std::filesystem::path& repo_path,
                                          const std::string& base_ref);

}  // namespace rctx
