#include "claim.hpp"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace rctx {

namespace {

// Split a claim file into (frontmatter, body). Frontmatter is the block between
// a leading "---" line and the next "---" line. Returns false if absent.
bool split_frontmatter(const std::string& text, std::string& fm, std::string& body) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line) || line != "---") return false;

  std::ostringstream fm_out;
  bool closed = false;
  while (std::getline(in, line)) {
    if (line == "---") {
      closed = true;
      break;
    }
    fm_out << line << '\n';
  }
  if (!closed) return false;

  std::ostringstream body_out;
  while (std::getline(in, line)) body_out << line << '\n';

  fm = fm_out.str();
  body = body_out.str();
  return true;
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Render `s` as a YAML double-quoted scalar so template values containing
// metacharacters (`*` reads as an alias, `#`/`:`/`[`/`]` etc. can change how
// the line parses) always round-trip as the literal string the caller gave.
std::string yaml_quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default: out += c;
    }
  }
  out += '"';
  return out;
}

}  // namespace

std::string default_claim_template() {
  return
      "---\n"
      "volatility: {{volatility}}\n"
      "{{watches_block}}\n"
      "reverify: \"\"\n"
      "---\n"
      "\n"
      "TODO: describe the assumption this claim records.\n";
}

std::string render_claim_template(const std::string& tmpl, const std::string& volatility,
                                   const std::vector<std::string>& watches) {
  std::string watches_block;
  if (watches.empty()) {
    watches_block = "watches: []";
  } else {
    watches_block = "watches:";
    for (const auto& w : watches) watches_block += "\n  - " + yaml_quote(w);
  }

  const std::vector<std::pair<std::string, std::string>> vars = {
      {"{{volatility}}", yaml_quote(volatility)},
      {"{{watches_block}}", watches_block},
  };

  // Single pass: each placeholder is expanded exactly once and the substituted
  // value is never rescanned, so a value that happens to contain "{{scope}}"
  // (or any other placeholder text) is emitted literally rather than being
  // re-substituted. Unknown "{{...}}" runs are copied through untouched.
  std::string out;
  out.reserve(tmpl.size());
  for (size_t i = 0; i < tmpl.size();) {
    bool matched = false;
    if (tmpl[i] == '{') {
      for (const auto& [key, value] : vars) {
        if (tmpl.compare(i, key.size(), key) == 0) {
          out += value;
          i += key.size();
          matched = true;
          break;
        }
      }
    }
    if (!matched) out += tmpl[i++];
  }
  return out;
}

std::optional<Claim> parse_claim_text(const std::string& text, const std::string& source_label) {
  std::string fm_text, body;
  if (!split_frontmatter(text, fm_text, body)) return std::nullopt;

  try {
    YAML::Node fm = YAML::Load(fm_text);
    Claim c;
    c.source_path = source_label;
    c.body = trim(body);
    // id and scope are intentionally not read here: they are derived from the
    // claim file's path by the caller (see derive_identity).
    if (fm["volatility"]) c.volatility = fm["volatility"].as<std::string>();
    if (fm["reverify"]) c.reverify = fm["reverify"].as<std::string>();

    if (fm["watches"]) {
      for (const auto& w : fm["watches"]) c.watches.push_back(w.as<std::string>());
    }
    if (fm["impacts"]) {
      for (const auto& node : fm["impacts"]) {
        ImpactRef ref;
        if (node["url"]) ref.url = node["url"].as<std::string>();
        if (node["terms"]) {
          for (const auto& t : node["terms"]) ref.terms.push_back(t.as<std::string>());
        }
        c.impacts.push_back(std::move(ref));
      }
    }
    return c;
  } catch (const YAML::Exception&) {
    // Malformed YAML or a field of the wrong type (e.g. a sequence where a
    // string is expected). Treat like absent frontmatter rather than
    // crashing the CLI on one bad claim file.
    return std::nullopt;
  }
}

std::optional<Claim> parse_claim_file(const fs::path& file) {
  std::ifstream f(file);
  if (!f) return std::nullopt;
  std::stringstream buf;
  buf << f.rdbuf();
  return parse_claim_text(buf.str(), file.string());
}

ClaimIdentity derive_identity(const fs::path& claims_relative_path) {
  ClaimIdentity out;
  fs::path noext = claims_relative_path;
  noext.replace_extension();  // drop the trailing .md if present
  out.id = noext.generic_string();
  // scope is the top-level folder; empty for a claim directly under claims_dir.
  if (claims_relative_path.has_parent_path()) {
    out.scope = claims_relative_path.begin()->string();
  }
  return out;
}

std::vector<Claim> load_claims(const fs::path& claims_dir) {
  std::vector<Claim> claims;
  if (!fs::exists(claims_dir)) return claims;
  for (const auto& entry : fs::recursive_directory_iterator(claims_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".md") continue;
    if (auto c = parse_claim_file(entry.path())) {
      const auto ident = derive_identity(entry.path().lexically_relative(claims_dir));
      c->scope = ident.scope;
      c->id = ident.id;
      claims.push_back(std::move(*c));
    }
  }
  return claims;
}

void to_json(nlohmann::json& j, const Claim& c) {
  j = nlohmann::json{
      {"id", c.id},
      {"scope", c.scope},
      {"volatility", c.volatility},
      {"watches", c.watches},
      {"reverify", c.reverify},
      {"source_path", c.source_path},
  };
  j["impacts"] = nlohmann::json::array();
  for (const auto& ref : c.impacts) {
    j["impacts"].push_back({{"url", ref.url}, {"terms", ref.terms}});
  }
}

}  // namespace rctx
