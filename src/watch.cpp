#include "watch.hpp"

#include <filesystem>

#include <fnmatch.h>

namespace fs = std::filesystem;

namespace rctx {

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

}  // namespace rctx
