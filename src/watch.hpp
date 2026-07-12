// Path-matching for a claim's `watches` patterns against changed files.
#pragma once

#include <string>

namespace rctx {

// Glob-match a changed path against a claim's watch pattern:
//   - no slash in pattern: match the file name (e.g. "*.json")
//   - "**" in pattern: '*' crosses '/' (e.g. "docs/**")
//   - otherwise: path glob where '*' stays within one segment
bool watch_matches(const std::string& pattern, const std::string& path);

}  // namespace rctx
