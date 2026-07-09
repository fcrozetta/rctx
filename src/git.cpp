#include "git.hpp"

#include <memory>

#include <git2.h>

namespace fs = std::filesystem;

namespace rctx {

namespace {

// unique_ptr wrappers so libgit2's C objects free on every exit path.
template <auto Fn>
struct Deleter {
  template <class T>
  void operator()(T* p) const {
    Fn(p);
  }
};
using Repo = std::unique_ptr<git_repository, Deleter<git_repository_free>>;
using Ref = std::unique_ptr<git_reference, Deleter<git_reference_free>>;
using Obj = std::unique_ptr<git_object, Deleter<git_object_free>>;
using Diff = std::unique_ptr<git_diff, Deleter<git_diff_free>>;

Repo open_repo(const fs::path& path) {
  git_repository* r = nullptr;
  if (git_repository_open(&r, path.string().c_str()) != 0) return Repo{nullptr};
  return Repo{r};
}

}  // namespace

std::string current_branch(const fs::path& repo_path) {
  Repo repo = open_repo(repo_path);
  if (!repo) return "";

  git_reference* head = nullptr;
  if (git_repository_head(&head, repo.get()) != 0) return "";
  Ref h{head};

  const char* name = git_reference_shorthand(h.get());
  return name ? std::string{name} : "";
}

std::vector<std::string> changed_files_vs(const fs::path& repo_path,
                                          const std::string& base_ref) {
  std::vector<std::string> out;
  Repo repo = open_repo(repo_path);
  if (!repo) return out;

  git_object* rev = nullptr;
  if (git_revparse_single(&rev, repo.get(), base_ref.c_str()) != 0) return out;
  Obj rev_obj{rev};

  git_object* tree = nullptr;
  if (git_object_peel(&tree, rev_obj.get(), GIT_OBJECT_TREE) != 0) return out;
  Obj tree_obj{tree};

  git_diff_options opts;
  git_diff_options_init(&opts, GIT_DIFF_OPTIONS_VERSION);
  // Drift cares about newly-appeared watched files too, not just tracked edits.
  opts.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS;

  git_diff* diff = nullptr;
  if (git_diff_tree_to_workdir_with_index(
          &diff, repo.get(), reinterpret_cast<git_tree*>(tree_obj.get()), &opts) != 0) {
    return out;
  }
  Diff diff_obj{diff};

  const size_t n = git_diff_num_deltas(diff_obj.get());
  for (size_t i = 0; i < n; ++i) {
    const git_diff_delta* delta = git_diff_get_delta(diff_obj.get(), i);
    if (delta && delta->new_file.path) out.emplace_back(delta->new_file.path);
  }
  return out;
}

}  // namespace rctx
