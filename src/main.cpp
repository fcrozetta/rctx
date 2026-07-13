// rctx PoC - repo context CLI.
//   new      - create a new claim file from a template
//   list     - walk .rctx/claims/** and emit parsed claims as JSON
//   index    - build the derived FTS index from claims
//   query    - full-text search the index
//   status   - current branch + files changed vs a base ref
//   drift    - claims whose watched paths changed vs the base ref
//   register - record this repo in the host-global registry
//   forget   - remove this repo's entry from the registry
//   repos    - list registered repos (--prune drops entries whose path is gone)
//   impact   - inbound: claims in other repos that impact this one
//              (--outbound: claims in this repo that impact others)
//   setup    - first-time setup: register this repo and install git hooks
//              (alias: hook)

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

#include <CLI/CLI.hpp>
#include <git2.h>
#include <nlohmann/json.hpp>

#include "claim.hpp"
#include "claim_id.hpp"
#include "git.hpp"
#include "index.hpp"
#include "registry.hpp"
#include "watch.hpp"

#ifndef RCTX_VERSION
#define RCTX_VERSION "0.0.0-dev"
#endif

namespace fs = std::filesystem;

namespace {

std::string canonical_path(const std::string& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  return ec ? fs::absolute(p).string() : c.string();
}

// Gather this repo's identity and record it in the registry. Only a real git
// work-tree is written, so callers like `impact` that self-register a path from
// the command line can't leave a junk entry (empty url/branch) behind.
rctx::RepoEntry self_register(const std::string& repo_path) {
  rctx::RepoEntry e;
  e.path = canonical_path(repo_path);
  e.url = rctx::remote_url(repo_path);
  e.branch = rctx::current_branch(repo_path);
  e.default_branch = rctx::default_branch_ref(repo_path);
  if (rctx::is_git_repo(repo_path)) rctx::register_repo(e);
  return e;
}

// Write a hook that refreshes rctx registration. Skips a pre-existing hook that
// rctx did not write, so we never clobber a user's own hook.
std::string install_hook(const fs::path& dir, const std::string& name) {
  const fs::path file = dir / name;
  if (fs::exists(file)) {
    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("rctx-managed") == std::string::npos) return "skipped (existing hook)";
  }
  std::ofstream out(file, std::ios::trunc);
  // Re-register (branch/path may have changed) and rebuild the index so search
  // stays fresh after checkout/merge/commit. Both write only outside the repo
  // (registry + ~/.cache), both best-effort so a git op never fails on rctx.
  out << "#!/bin/sh\n# rctx-managed\n"
         "rctx register >/dev/null 2>&1 || true\n"
         "rctx index >/dev/null 2>&1 || true\n";
  out.close();
  fs::permissions(file,
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  fs::perm_options::replace);
  return "installed";
}

// Ensure the repo's .gitignore force-tracks the whole .rctx/ tree. .rctx/ is
// committed source of truth (claim files); nothing disposable lives there — the
// FTS index is a cache under ~/.cache/rctx. A claim's scope is its folder name,
// so without this a scope matching a common ignore rule (build/, bin/, out/...)
// would be silently dropped and never travel in a PR. Idempotent.
std::string ensure_gitignore(const fs::path& repo_root) {
  const fs::path gi = repo_root / ".gitignore";
  const bool existed = fs::exists(gi);
  std::string content;
  if (existed) {
    std::ifstream in(gi);
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (content.find("!.rctx/**") != std::string::npos) return "present";
  }
  std::ofstream out(gi, std::ios::app);
  if (!out) return "error";
  if (!content.empty() && content.back() != '\n') out << '\n';
  // `!.rctx/` must come first: if an earlier rule excludes .rctx/ itself, Git
  // won't descend into it, so the child negations alone would never re-include
  // anything. Re-include the dir, then its subdirs, then its files.
  out << "\n# rctx: .rctx/ is committed source of truth. Keep the whole tree\n"
         "# tracked so a claim scope folder matching an ignore rule (build/, bin/,\n"
         "# out/, ...) can't be silently dropped. Must come after those rules.\n"
         "!.rctx/\n"
         "!.rctx/**/\n"
         "!.rctx/**\n"
         "# The FTS index is a cache under ~/.cache/rctx. Re-ignore its old in-repo\n"
         "# location (last match wins) so a leftover DB can't be committed.\n"
         ".rctx/cache/\n";
  return existed ? "added" : "created";
}

struct GitRuntime {
  GitRuntime() { git_libgit2_init(); }
  ~GitRuntime() { git_libgit2_shutdown(); }
};

// The repo a local index/query targets: the git work-tree root containing the
// cwd (so it works from a subdir and keys per worktree), or the canonical cwd
// when that is not a git work tree. Used to locate this repo's cache dir.
fs::path index_root() {
  const std::string root = rctx::repo_root(".");
  if (!root.empty()) return fs::path{root};
  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  return ec ? fs::path{"."} : cwd;
}

}  // namespace

int run_cli(int argc, char** argv) {
  CLI::App app{"rctx - repository-bounded context and claims"};
  app.set_version_flag("--version", std::string{RCTX_VERSION});
  app.require_subcommand(1);

  std::string claims_dir = ".rctx/claims";
  std::string db_path;  // empty -> computed per repo under the user cache dir
  std::string repo_path = ".";
  std::string base_ref = "main";

  std::string new_id, new_volatility = "stable", template_path;
  std::vector<std::string> new_watches;
  bool force = false;
  auto* newcmd = app.add_subcommand("new", "create a new claim from a template");
  newcmd->add_option("path", new_id,
                      "claim path under the claims dir, e.g. build/requires-env; the top "
                      "folder is the claim's scope (defaults to a UTC timestamp id at the "
                      "claims-dir root, e.g. 20260709-153245-a1b2c3)");
  newcmd->add_option("-C,--claims-dir", claims_dir, "claims directory")->capture_default_str();
  newcmd->add_option("--volatility", new_volatility, "\"stable\" or \"volatile\"")
      ->capture_default_str();
  // allow_extra_args(false): each --watch takes exactly one value, so a
  // positional id following a --watch is not swallowed into the watch list.
  newcmd->add_option("--watch", new_watches, "watched path (repeatable)")
      ->allow_extra_args(false);
  newcmd->add_option("--template", template_path,
                      "path to a claim template file (defaults to the built-in template)");
  newcmd->add_flag("--force", force, "overwrite the claim file if it already exists");

  auto* list = app.add_subcommand("list", "parse claim files and print them as JSON");
  list->add_option("-C,--claims-dir", claims_dir, "claims directory")->capture_default_str();

  const char* db_help =
      "index database path (default: a per-repo dir under $XDG_CACHE_HOME or ~/.cache; "
      "the index is a disposable cache and is never stored inside the repo)";
  auto* index = app.add_subcommand("index", "build the derived FTS index from claims");
  auto* index_claims_opt =
      index
          ->add_option("-C,--claims-dir", claims_dir,
                       "claims directory; pair with --db when it points outside this repo, "
                       "or a later `query` (which has no -C) rebuilds the default cache from "
                       ".rctx/claims and overwrites it")
          ->capture_default_str();
  index->add_option("--db", db_path, db_help);

  std::string query_expr;
  auto* query = app.add_subcommand("query", "full-text search the index");
  query->add_option("expr", query_expr, "FTS5 match expression")->required();
  query->add_option("--db", db_path, db_help);

  auto* status = app.add_subcommand("status", "current branch + files changed vs base ref");
  status->add_option("--repo", repo_path, "repository path")->capture_default_str();
  status->add_option("--base", base_ref, "base ref to compare against")->capture_default_str();

  auto* drift = app.add_subcommand("drift", "claims whose watched paths changed vs base ref");
  drift->add_option("-C,--claims-dir", claims_dir, "claims directory")->capture_default_str();
  drift->add_option("--repo", repo_path, "repository path")->capture_default_str();
  drift->add_option("--base", base_ref, "base ref to compare against")->capture_default_str();

  auto* reg = app.add_subcommand("register", "record this repo in the host-global registry");
  reg->add_option("--repo", repo_path, "repository path")->capture_default_str();

  auto* forget = app.add_subcommand("forget", "remove this repo's entry from the registry");
  forget->add_option("--repo", repo_path, "repository path")->capture_default_str();

  bool prune = false;
  auto* repos = app.add_subcommand("repos", "list registered repos");
  repos->add_flag("--prune", prune, "drop entries whose path no longer exists on disk");

  bool outbound = false;
  auto* impact = app.add_subcommand("impact", "cross-repo impact for this repo");
  impact->add_option("--repo", repo_path, "repository path")->capture_default_str();
  impact->add_flag("--outbound", outbound, "claims in THIS repo that impact others");

  auto* setup = app.add_subcommand(
      "setup", "first-time setup: register this repo and install git hooks");
  setup->alias("hook");
  setup->add_option("--repo", repo_path, "repository path")->capture_default_str();

  CLI11_PARSE(app, argc, argv);

  GitRuntime git_runtime;

  if (*newcmd) {
    if (new_id.empty()) new_id = rctx::new_claim_id();
    if (new_id.find("..") != std::string::npos || fs::path(new_id).is_absolute()) {
      std::cerr << "error: invalid claim id: " << new_id << std::endl;
      return 1;
    }
    if (new_volatility != "stable" && new_volatility != "volatile") {
      std::cerr << "error: --volatility must be \"stable\" or \"volatile\"" << std::endl;
      return 1;
    }

    std::string tmpl;
    if (template_path.empty()) {
      tmpl = rctx::default_claim_template();
    } else {
      std::ifstream in(template_path);
      if (!in) {
        std::cerr << "error: template not found: " << template_path << std::endl;
        return 1;
      }
      std::stringstream buf;
      buf << in.rdbuf();
      tmpl = buf.str();
    }

    const fs::path out_path = fs::path(claims_dir) / (new_id + ".md");
    if (fs::exists(out_path) && !force) {
      std::cerr << "error: claim already exists: " << out_path.string()
                << " (use --force to overwrite)" << std::endl;
      return 1;
    }

    const std::string rendered = rctx::render_claim_template(tmpl, new_volatility, new_watches);
    auto parsed = rctx::parse_claim_text(rendered, out_path.string());
    if (!parsed) {
      std::cerr << "error: rendered claim has no valid frontmatter; check --template"
                << std::endl;
      return 1;
    }
    // id and scope come from the file's path, not the template.
    const auto ident = rctx::derive_identity(new_id + ".md");
    parsed->scope = ident.scope;
    parsed->id = ident.id;

    std::error_code ec;
    fs::create_directories(out_path.parent_path(), ec);
    if (ec) {
      std::cerr << "error: could not create " << out_path.parent_path().string() << ": "
                << ec.message() << std::endl;
      return 1;
    }

    std::ofstream out(out_path, std::ios::trunc);
    out << rendered;
    out.close();
    if (!out) {
      std::cerr << "error: could not write claim: " << out_path.string() << std::endl;
      return 1;
    }

    std::cerr << "created " << out_path.string() << std::endl;
    nlohmann::json j;
    rctx::to_json(j, *parsed);
    std::cout << j.dump(2) << std::endl;
  } else if (*list) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& c : rctx::load_claims(claims_dir)) {
      nlohmann::json j;
      rctx::to_json(j, c);
      out.push_back(std::move(j));
    }
    std::cout << out.dump(2) << std::endl;
  } else if (*index) {
    const fs::path root = index_root();
    if (db_path.empty()) db_path = rctx::default_index_path(root).string();
    // Load claims from the same work-tree root the cache is keyed on. Otherwise
    // `index` from a subdir loads an empty set (claims_dir is cwd-relative) and
    // overwrites the shared root cache with nothing. An explicit -C still wins.
    if (index_claims_opt->count() == 0) claims_dir = (root / ".rctx" / "claims").string();
    const auto n = rctx::refresh_index(db_path, claims_dir);
    std::cerr << "indexed " << n << " claim(s) -> " << db_path << std::endl;
  } else if (*query) {
    const bool db_defaulted = db_path.empty();
    const fs::path root = index_root();
    if (db_defaulted) db_path = rctx::default_index_path(root).string();
    // Lazily refresh the default cache so a query is never stale even if no hook
    // ran and the user never called `index` (ADR: lazy checks at invocation).
    // An explicit --db is left untouched: the caller pointed at that index.
    if (db_defaulted) {
      const fs::path cdir = root / ".rctx" / "claims";
      if (rctx::index_stale(db_path, cdir)) rctx::refresh_index(db_path, cdir);
    }
    nlohmann::json out = nlohmann::json::array();
    // With a default db it now always exists (just built above). A missing
    // explicit --db means nothing to search, so report empty rather than
    // creating a stray file that fails on the absent FTS table.
    if (fs::exists(db_path)) {
      for (const auto& h : rctx::search_index(db_path, query_expr)) {
        out.push_back({{"id", h.id}, {"source_path", h.source_path}, {"snippet", h.snippet}});
      }
    }
    std::cout << out.dump(2) << std::endl;
  } else if (*status) {
    nlohmann::json out;
    out["branch"] = rctx::current_branch(repo_path);
    out["base"] = base_ref;
    out["changed"] = rctx::changed_files_vs(repo_path, base_ref);
    std::cout << out.dump(2) << std::endl;
  } else if (*drift) {
    const auto changed = rctx::changed_files_vs(repo_path, base_ref);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& c : rctx::load_claims(claims_dir)) {
      for (const auto& w : c.watches) {
        for (const auto& f : changed) {
          if (rctx::watch_matches(w, f)) {
            out.push_back({{"claim", c.id},
                           {"watched", w},
                           {"changed_file", f},
                           {"base", base_ref},
                           {"reverify", c.reverify}});
          }
        }
      }
    }
    std::cout << out.dump(2) << std::endl;
  } else if (*reg) {
    if (!rctx::is_git_repo(repo_path)) {
      std::cerr << "error: not a git repository: " << repo_path << std::endl;
      return 1;
    }
    const auto e = self_register(repo_path);
    std::cerr << "registered " << e.path << std::endl;
    std::cout << nlohmann::json{{"url", e.url},
                                {"path", e.path},
                                {"branch", e.branch},
                                {"default_branch", e.default_branch}}
                     .dump(2)
              << std::endl;
  } else if (*forget) {
    const std::string path = canonical_path(repo_path);
    const bool removed = rctx::forget_repo(path);
    std::cerr << (removed ? "forgot " : "no entry for ") << path << std::endl;
    std::cout << nlohmann::json{{"path", path}, {"removed", removed}}.dump(2) << std::endl;
  } else if (*repos) {
    if (prune) {
      nlohmann::json pruned = nlohmann::json::array();
      for (const auto& p : rctx::prune_missing_repos()) pruned.push_back(p);
      std::cout << nlohmann::json{{"pruned", pruned}}.dump(2) << std::endl;
    } else {
      nlohmann::json out = nlohmann::json::array();
      for (const auto& r : rctx::list_repos()) {
        out.push_back({{"url", r.url},
                       {"path", r.path},
                       {"branch", r.branch},
                       {"default_branch", r.default_branch}});
      }
      std::cout << out.dump(2) << std::endl;
    }
  } else if (*impact) {
    const auto me = self_register(repo_path);
    const std::string my_url = rctx::normalize_url(me.url);
    nlohmann::json out = nlohmann::json::array();

    if (outbound) {
      // Which repos are cloned here (by normalized URL that resolves to a path).
      std::vector<std::string> cloned;
      for (const auto& r : rctx::list_repos()) {
        if (fs::exists(r.path)) cloned.push_back(rctx::normalize_url(r.url));
      }
      const fs::path my_claims = fs::path{repo_path} / ".rctx" / "claims";
      for (const auto& c : rctx::load_claims(my_claims)) {
        for (const auto& imp : c.impacts) {
          const bool is_cloned =
              std::find(cloned.begin(), cloned.end(), rctx::normalize_url(imp.url)) != cloned.end();
          out.push_back({{"claim", c.id},
                         {"impacts", imp.url},
                         {"terms", imp.terms},
                         {"target_cloned", is_cloned}});
        }
      }
    } else if (my_url.empty()) {
      std::cerr << "warning: this repo has no origin URL; cross-repo impact needs one" << std::endl;
    } else {
      for (const auto& r : rctx::list_repos()) {
        if (r.path == me.path) continue;
        if (!fs::exists(r.path)) {
          out.push_back({{"note", "referenced repo not cloned on host"}, {"repo", r.url}});
          continue;
        }
        for (const auto& [rel, content] :
             rctx::read_files_at_ref(r.path, r.default_branch, ".rctx/claims")) {
          auto claim = rctx::parse_claim_text(content, r.url + "@" + r.default_branch + ":" + rel);
          if (!claim) continue;
          // id/scope derive from the file's path within the other repo's claims dir.
          const auto ident = rctx::derive_identity(fs::path(rel).lexically_relative(".rctx/claims"));
          claim->scope = ident.scope;
          claim->id = ident.id;
          for (const auto& imp : claim->impacts) {
            if (rctx::normalize_url(imp.url) == my_url) {
              out.push_back({{"from_repo", r.url},
                             {"from_ref", r.default_branch},
                             {"claim", claim->id},
                             {"source", claim->source_path},
                             {"terms", imp.terms}});
            }
          }
        }
      }
    }
    std::cout << out.dump(2) << std::endl;
  } else if (*setup) {
    const std::string dir = rctx::hooks_dir(repo_path);
    if (dir.empty()) {
      std::cerr << "error: not a git repository: " << repo_path << std::endl;
      return 1;
    }
    // Register now so cross-repo queries see this repo immediately; the hooks
    // below keep it fresh on every checkout/merge/commit.
    const auto e = self_register(repo_path);
    std::cerr << "registered " << e.path << std::endl;
    fs::create_directories(dir);
    nlohmann::json hooks;
    for (const char* name : {"post-checkout", "post-merge", "post-commit"}) {
      hooks[name] = install_hook(dir, name);
    }
    // Keep .rctx/ committable no matter what the repo's ignore rules are.
    const std::string root = rctx::repo_root(repo_path);
    const std::string gitignore = ensure_gitignore(root.empty() ? fs::path{repo_path} : fs::path{root});
    if (gitignore == "added" || gitignore == "created") {
      std::cerr << "updated .gitignore to keep .rctx/ tracked — review and commit it" << std::endl;
    }
    std::cout << nlohmann::json{{"registered",
                                 {{"url", e.url},
                                  {"path", e.path},
                                  {"branch", e.branch},
                                  {"default_branch", e.default_branch}}},
                                {"hooks", hooks},
                                {"gitignore", gitignore}}
                     .dump(2)
              << std::endl;
  }

  return 0;
}

int main(int argc, char** argv) {
  // Single catch-all so any handler failure (a locked or corrupt db, a bad
  // path, a git error) exits cleanly with a message instead of aborting on an
  // uncaught exception. Matters more now that git hooks invoke rctx for you.
  try {
    return run_cli(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }
}
