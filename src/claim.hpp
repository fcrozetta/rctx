// Claim model + loader. A claim is a Markdown file with YAML frontmatter,
// living under .rctx/claims/**. Claims are the committed source of truth;
// everything else in rctx is a derived, disposable accelerator over these.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rctx {

// A cross-repo reference. Deliberately loose: only a git URL + broad terms,
// so repos never hard-couple to each other's internals.
struct ImpactRef {
  std::string url;
  std::vector<std::string> terms;
};

struct Claim {
  std::string id;
  std::string scope;
  std::string volatility;          // e.g. "stable" | "volatile"
  std::vector<ImpactRef> impacts;  // other repos this claim affects
  std::vector<std::string> watches;  // paths/patterns that trigger drift checks
  std::string reverify;            // machine-executable check (exit 0/1)
  std::string body;                // markdown body after the frontmatter
  std::string source_path;         // file the claim was loaded from
};

// Parse a single claim file. Returns nullopt if the file has no frontmatter.
std::optional<Claim> parse_claim_file(const std::filesystem::path& file);

// Recursively load every *.md claim under `claims_dir`.
std::vector<Claim> load_claims(const std::filesystem::path& claims_dir);

void to_json(nlohmann::json& j, const Claim& c);

}  // namespace rctx
