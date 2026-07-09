// rctx PoC - repo context CLI.
//   list     - walk .rctx/claims/** and emit parsed claims as JSON
//   index    - build the derived FTS index from claims
//   query    - full-text search the index
//   status   - current branch + files changed vs a base ref
//   drift    - claims whose watched paths changed vs the base ref
//   register - record this repo in the host-global registry
//   repos    - list registered repos
//   impact   - inbound: claims in other repos that impact this one
//              (--outbound: claims in this repo that impact others)
//   hook     - install git hooks that refresh registration

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <fnmatch.h>

#include <CLI/CLI.hpp>
#include <git2.h>
#include <nlohmann/json.hpp>

#include "claim.hpp"
#include "git.hpp"
#include "index.hpp"
#include "registry.hpp"

namespace fs = std::filesystem;

namespace {

// Glob match a changed path against a claim's watch pattern.
//   - no slash in pattern: match the file name (e.g. "*.json")
//   - "**" in pattern: '*' crosses '/' (e.g. "docs/**")
//   - otherwise: path glob where '*' stays within one segment
bool watch_matches(const std::string& pattern, const std::string& path) {
  if (pattern == path) return true;
  if (pattern.find('/') == std::string::npos) {
    return fnmatch(pattern.c_str(), fs::path(path).filename().string().c_str(), 0) == 0;
  }
  if (pattern.find("**") != std::string::npos) {
    std::string collapsed = pattern;
    for (size_t p; (p = collapsed.find("**")) != std::string::npos;) collapsed.replace(p, 2, "*");
    return fnmatch(collapsed.c_str(), path.c_str(), 0) == 0;
  }
  return fnmatch(pattern.c_str(), path.c_str(), FNM_PATHNAME) == 0;
}

std::string normalize_url(std::string u) {
  std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return std::tolower(c); });
  if (u.size() >= 4 && u.compare(u.size() - 4, 4, ".git") == 0) u.erase(u.size() - 4);
  if (!u.empty() && u.back() == '/') u.pop_back();
  return u;
}

std::string canonical_path(const std::string& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  return ec ? fs::absolute(p).string() : c.string();
}

rctx::RepoEntry self_register(const std::string& repo_path) {
  rctx::RepoEntry e;
  e.path = canonical_path(repo_path);
  e.url = rctx::remote_url(repo_path);
  e.branch = rctx::current_branch(repo_path);
  e.default_branch = rctx::default_branch_ref(repo_path);
  rctx::register_repo(e);
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
  out << "#!/bin/sh\n# rctx-managed\nrctx register >/dev/null 2>&1 || true\n";
  out.close();
  fs::permissions(file,
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  fs::perm_options::replace);
  return "installed";
}

struct GitRuntime {
  GitRuntime() { git_libgit2_init(); }
  ~GitRuntime() { git_libgit2_shutdown(); }
};

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"rctx - repository-bounded context and claims"};
  app.set_version_flag("--version", std::string{"rctx 0.0.1"});
  app.require_subcommand(1);

  std::string claims_dir = ".rctx/claims";
  std::string db_path = ".rctx/cache/index.db";
  std::string repo_path = ".";
  std::string base_ref = "main";

  auto* list = app.add_subcommand("list", "parse claim files and print them as JSON");
  list->add_option("-C,--claims-dir", claims_dir, "claims directory")->capture_default_str();

  auto* index = app.add_subcommand("index", "build the derived FTS index from claims");
  index->add_option("-C,--claims-dir", claims_dir, "claims directory")->capture_default_str();
  index->add_option("--db", db_path, "index database path")->capture_default_str();

  std::string query_expr;
  auto* query = app.add_subcommand("query", "full-text search the index");
  query->add_option("expr", query_expr, "FTS5 match expression")->required();
  query->add_option("--db", db_path, "index database path")->capture_default_str();

  auto* status = app.add_subcommand("status", "current branch + files changed vs base ref");
  status->add_option("--repo", repo_path, "repository path")->capture_default_str();
  status->add_option("--base", base_ref, "base ref to compare against")->capture_default_str();

  auto* drift = app.add_subcommand("drift", "claims whose watched paths changed vs base ref");
  drift->add_option("-C,--claims-dir", claims_dir, "claims directory")->capture_default_str();
  drift->add_option("--repo", repo_path, "repository path")->capture_default_str();
  drift->add_option("--base", base_ref, "base ref to compare against")->capture_default_str();

  auto* reg = app.add_subcommand("register", "record this repo in the host-global registry");
  reg->add_option("--repo", repo_path, "repository path")->capture_default_str();

  app.add_subcommand("repos", "list registered repos");

  bool outbound = false;
  auto* impact = app.add_subcommand("impact", "cross-repo impact for this repo");
  impact->add_option("--repo", repo_path, "repository path")->capture_default_str();
  impact->add_flag("--outbound", outbound, "claims in THIS repo that impact others");

  auto* hook = app.add_subcommand("hook", "install git hooks that refresh registration");
  hook->add_option("--repo", repo_path, "repository path")->capture_default_str();

  CLI11_PARSE(app, argc, argv);

  GitRuntime git_runtime;

  if (*list) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& c : rctx::load_claims(claims_dir)) {
      nlohmann::json j;
      rctx::to_json(j, c);
      out.push_back(std::move(j));
    }
    std::cout << out.dump(2) << std::endl;
  } else if (*index) {
    const auto claims = rctx::load_claims(claims_dir);
    rctx::build_index(db_path, claims);
    std::cerr << "indexed " << claims.size() << " claim(s) -> " << db_path << std::endl;
  } else if (*query) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& h : rctx::search_index(db_path, query_expr)) {
      out.push_back({{"id", h.id}, {"source_path", h.source_path}, {"snippet", h.snippet}});
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
          if (watch_matches(w, f)) {
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
    const auto e = self_register(repo_path);
    std::cerr << "registered " << e.path << std::endl;
    std::cout << nlohmann::json{{"url", e.url},
                                {"path", e.path},
                                {"branch", e.branch},
                                {"default_branch", e.default_branch}}
                     .dump(2)
              << std::endl;
  } else if (*app.get_subcommand("repos")) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& r : rctx::list_repos()) {
      out.push_back({{"url", r.url},
                     {"path", r.path},
                     {"branch", r.branch},
                     {"default_branch", r.default_branch}});
    }
    std::cout << out.dump(2) << std::endl;
  } else if (*impact) {
    const auto me = self_register(repo_path);
    const std::string my_url = normalize_url(me.url);
    nlohmann::json out = nlohmann::json::array();

    if (outbound) {
      // Which repos are cloned here (by normalized URL that resolves to a path).
      std::vector<std::string> cloned;
      for (const auto& r : rctx::list_repos()) {
        if (fs::exists(r.path)) cloned.push_back(normalize_url(r.url));
      }
      const fs::path my_claims = fs::path{repo_path} / ".rctx" / "claims";
      for (const auto& c : rctx::load_claims(my_claims)) {
        for (const auto& imp : c.impacts) {
          const bool is_cloned =
              std::find(cloned.begin(), cloned.end(), normalize_url(imp.url)) != cloned.end();
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
          for (const auto& imp : claim->impacts) {
            if (normalize_url(imp.url) == my_url) {
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
  } else if (*hook) {
    const std::string dir = rctx::hooks_dir(repo_path);
    if (dir.empty()) {
      std::cerr << "error: not a git repository: " << repo_path << std::endl;
      return 1;
    }
    fs::create_directories(dir);
    nlohmann::json out;
    for (const char* name : {"post-checkout", "post-merge", "post-commit"}) {
      out[name] = install_hook(dir, name);
    }
    std::cout << out.dump(2) << std::endl;
  }

  return 0;
}
