// Thin libgit2 helpers for the drift check. Requires git_libgit2_init() to have
// been called (once, in main) before use.
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rctx {

// Short name of the currently checked-out branch (empty if detached/unavailable).
std::string current_branch(const std::filesystem::path& repo_path);

// Paths that differ between `base_ref` (e.g. "main") and the working tree.
std::vector<std::string> changed_files_vs(const std::filesystem::path& repo_path,
                                          const std::string& base_ref);

// URL of the "origin" remote (empty if none).
std::string remote_url(const std::filesystem::path& repo_path);

// A revparse-able ref for the repo's default branch: the target of
// origin/HEAD when set, else "main", else "master".
std::string default_branch_ref(const std::filesystem::path& repo_path);

// (relative path, content) for every *.md file under `subdir` in `ref`'s tree.
// Reads from the object database, so it is independent of the checked-out branch.
std::vector<std::pair<std::string, std::string>> read_files_at_ref(
    const std::filesystem::path& repo_path, const std::string& ref, const std::string& subdir);

}  // namespace rctx
