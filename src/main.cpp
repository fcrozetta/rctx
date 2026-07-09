// rctx PoC - repo context CLI.
//   list     - walk .rctx/claims/** and emit parsed claims as JSON
//   index    - build the derived FTS index from claims
//   query    - full-text search the index
//   status   - current branch + files changed vs a base ref
//   drift    - claims whose watched paths changed vs the base ref
//   register - record this repo in the host-global registry
//   repos    - list registered repos
//   impact   - claims in OTHER registered repos that declare impact on this one

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <git2.h>
#include <nlohmann/json.hpp>

#include "claim.hpp"
#include "git.hpp"
#include "index.hpp"
#include "registry.hpp"

namespace fs = std::filesystem;

namespace {

bool watch_matches(const std::string& watch, const std::string& changed) {
  if (watch == changed) return true;
  if (changed.size() > watch.size() &&
      changed.compare(changed.size() - watch.size() - 1, watch.size() + 1, "/" + watch) == 0) {
    return true;
  }
  return fs::path(changed).filename() == watch;
}

// Normalize a git URL for comparison: lowercase, drop a trailing ".git" and "/".
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

  auto* impact = app.add_subcommand("impact", "claims in other repos that declare impact on this one");
  impact->add_option("--repo", repo_path, "repository path")->capture_default_str();

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
    nlohmann::json out{{"url", e.url},
                       {"path", e.path},
                       {"branch", e.branch},
                       {"default_branch", e.default_branch}};
    std::cerr << "registered " << e.path << std::endl;
    std::cout << out.dump(2) << std::endl;
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
    const auto me = self_register(repo_path);  // ensure this repo is known
    const std::string my_url = normalize_url(me.url);

    nlohmann::json out = nlohmann::json::array();
    if (my_url.empty()) {
      std::cerr << "warning: this repo has no origin URL; cross-repo impact needs one" << std::endl;
    } else {
      for (const auto& r : rctx::list_repos()) {
        if (r.path == me.path) continue;              // skip self
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
  }

  return 0;
}
