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

}  // namespace

std::optional<Claim> parse_claim_file(const fs::path& file) {
  std::ifstream f(file);
  if (!f) return std::nullopt;
  std::stringstream buf;
  buf << f.rdbuf();

  std::string fm_text, body;
  if (!split_frontmatter(buf.str(), fm_text, body)) return std::nullopt;

  YAML::Node fm = YAML::Load(fm_text);
  Claim c;
  c.source_path = file.string();
  c.body = trim(body);
  if (fm["id"]) c.id = fm["id"].as<std::string>();
  if (fm["scope"]) c.scope = fm["scope"].as<std::string>();
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
}

std::vector<Claim> load_claims(const fs::path& claims_dir) {
  std::vector<Claim> claims;
  if (!fs::exists(claims_dir)) return claims;
  for (const auto& entry : fs::recursive_directory_iterator(claims_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".md") continue;
    if (auto c = parse_claim_file(entry.path())) claims.push_back(std::move(*c));
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
