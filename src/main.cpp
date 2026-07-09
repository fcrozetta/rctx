// rctx PoC - repo context CLI (vertical slice).
//   list   - walk .rctx/claims/** and emit parsed claims as JSON  (M1)
//   index  - build the derived FTS index from claims               (M2)
//   query  - full-text search the index                            (M2)
//   status - current branch + files changed vs a base ref          (M3)
//   drift  - claims whose watched paths changed vs the base ref     (M4)

#include <filesystem>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <git2.h>
#include <nlohmann/json.hpp>

#include "claim.hpp"
#include "git.hpp"
#include "index.hpp"

namespace {

// Naive path match for the PoC: exact, suffix-after-slash, or basename equality.
// Real globbing is a later concern; this is enough to prove the drift wiring.
bool watch_matches(const std::string& watch, const std::string& changed) {
  if (watch == changed) return true;
  if (changed.size() > watch.size() &&
      changed.compare(changed.size() - watch.size() - 1, watch.size() + 1, "/" + watch) == 0) {
    return true;
  }
  return std::filesystem::path(changed).filename() == watch;
}

// libgit2 needs global init/shutdown around any use.
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
  }

  return 0;
}
