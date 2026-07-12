#include <doctest/doctest.h>

#include <string>

#include "claim.hpp"

using namespace rctx;

TEST_CASE("parse_claim_text reads frontmatter and body but not id/scope") {
  const std::string text =
      "---\n"
      "id: should-be-ignored\n"
      "scope: also-ignored\n"
      "volatility: volatile\n"
      "impacts:\n"
      "  - url: https://github.com/x/y\n"
      "    terms: [a, b]\n"
      "watches:\n"
      "  - openapi.json\n"
      "reverify: \"test -f openapi.json\"\n"
      "---\n"
      "\n"
      "Body text here.\n";
  auto c = parse_claim_text(text, "some/path.md");
  REQUIRE(c.has_value());
  CHECK(c->volatility == "volatile");
  // id and scope come from the file path, never the frontmatter.
  CHECK(c->id.empty());
  CHECK(c->scope.empty());
  REQUIRE(c->watches.size() == 1);
  CHECK(c->watches[0] == "openapi.json");
  CHECK(c->reverify == "test -f openapi.json");
  REQUIRE(c->impacts.size() == 1);
  CHECK(c->impacts[0].url == "https://github.com/x/y");
  REQUIRE(c->impacts[0].terms.size() == 2);
  CHECK(c->impacts[0].terms[0] == "a");
  CHECK(c->body == "Body text here.");
  CHECK(c->source_path == "some/path.md");
}

TEST_CASE("parse_claim_text returns nullopt without frontmatter") {
  CHECK_FALSE(parse_claim_text("no frontmatter here", "x").has_value());
  CHECK_FALSE(parse_claim_text("", "x").has_value());
  // opening --- but never closed
  CHECK_FALSE(parse_claim_text("---\nvolatility: stable\n", "x").has_value());
}

TEST_CASE("parse_claim_text returns nullopt on malformed yaml") {
  // unclosed flow sequence in the frontmatter block
  CHECK_FALSE(parse_claim_text("---\nwatches: [a, b\n---\nbody\n", "x").has_value());
}

TEST_CASE("derive_identity: scope is the top folder, id is the path minus .md") {
  auto a = derive_identity("build/requires-env.md");
  CHECK(a.scope == "build");
  CHECK(a.id == "build/requires-env");

  auto b = derive_identity("contract.md");  // directly under the claims dir
  CHECK(b.scope == "");
  CHECK(b.id == "contract");

  auto c = derive_identity("api/v2/pagination.md");  // deeper: scope is still the top folder
  CHECK(c.scope == "api");
  CHECK(c.id == "api/v2/pagination");

  auto d = derive_identity("build/requires-env");  // extension optional
  CHECK(d.scope == "build");
  CHECK(d.id == "build/requires-env");
}

TEST_CASE("render_claim_template produces frontmatter that parses back") {
  const auto out = render_claim_template(default_claim_template(), "stable", {"a.json", "b/*.yaml"});
  auto c = parse_claim_text(out, "x.md");
  REQUIRE(c.has_value());
  CHECK(c->volatility == "stable");
  REQUIRE(c->watches.size() == 2);
  CHECK(c->watches[0] == "a.json");
  CHECK(c->watches[1] == "b/*.yaml");
}
