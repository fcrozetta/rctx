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
using Remote = std::unique_ptr<git_remote, Deleter<git_remote_free>>;

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

std::string remote_url(const fs::path& repo_path) {
  Repo repo = open_repo(repo_path);
  if (!repo) return "";
  git_remote* remote = nullptr;
  if (git_remote_lookup(&remote, repo.get(), "origin") != 0) return "";
  Remote r{remote};
  const char* url = git_remote_url(r.get());
  return url ? std::string{url} : "";
}

std::string default_branch_ref(const fs::path& repo_path) {
  Repo repo = open_repo(repo_path);
  if (!repo) return "main";

  git_reference* head = nullptr;
  if (git_reference_lookup(&head, repo.get(), "refs/remotes/origin/HEAD") == 0) {
    Ref h{head};
    if (const char* target = git_reference_symbolic_target(h.get())) {
      const std::string prefix = "refs/remotes/";
      std::string t{target};
      if (t.rfind(prefix, 0) == 0) return t.substr(prefix.size());  // e.g. "origin/main"
    }
  }
  git_object* obj = nullptr;
  if (git_revparse_single(&obj, repo.get(), "refs/heads/main") == 0) {
    git_object_free(obj);
    return "main";
  }
  if (git_revparse_single(&obj, repo.get(), "refs/heads/master") == 0) {
    git_object_free(obj);
    return "master";
  }
  return "main";
}

std::vector<std::pair<std::string, std::string>> read_files_at_ref(
    const fs::path& repo_path, const std::string& ref, const std::string& subdir) {
  std::vector<std::pair<std::string, std::string>> out;
  Repo repo = open_repo(repo_path);
  if (!repo) return out;

  git_object* rev = nullptr;
  if (git_revparse_single(&rev, repo.get(), ref.c_str()) != 0) return out;
  Obj rev_obj{rev};

  git_object* tree = nullptr;
  if (git_object_peel(&tree, rev_obj.get(), GIT_OBJECT_TREE) != 0) return out;
  Obj tree_obj{tree};

  struct Ctx {
    git_repository* repo;
    std::string subdir;
    std::vector<std::pair<std::string, std::string>>* out;
  } ctx{repo.get(), subdir, &out};

  auto cb = [](const char* root, const git_tree_entry* entry, void* payload) -> int {
    auto* c = static_cast<Ctx*>(payload);
    if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB) return 0;
    std::string full = std::string(root) + git_tree_entry_name(entry);
    if (full.rfind(c->subdir, 0) != 0) return 0;
    if (full.size() < 3 || full.compare(full.size() - 3, 3, ".md") != 0) return 0;

    git_object* o = nullptr;
    if (git_tree_entry_to_object(&o, c->repo, entry) != 0) return 0;
    auto* blob = reinterpret_cast<git_blob*>(o);
    const char* data = static_cast<const char*>(git_blob_rawcontent(blob));
    const size_t n = static_cast<size_t>(git_blob_rawsize(blob));
    c->out->emplace_back(full, std::string(data ? data : "", data ? n : 0));
    git_object_free(o);
    return 0;
  };

  git_tree_walk(reinterpret_cast<git_tree*>(tree_obj.get()), GIT_TREEWALK_PRE, cb, &ctx);
  return out;
}

bool is_git_repo(const fs::path& repo_path) { return static_cast<bool>(open_repo(repo_path)); }

std::string hooks_dir(const fs::path& repo_path) {
  Repo repo = open_repo(repo_path);
  if (!repo) return "";
  const char* common = git_repository_commondir(repo.get());
  if (!common) return "";
  return (fs::path{common} / "hooks").string();
}

}  // namespace rctx
