#include "index.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include <sqlite3.h>

namespace fs = std::filesystem;

namespace rctx {

namespace {

// Minimal RAII wrappers so the raw sqlite3 C API can't leak on error paths.
struct Db {
  sqlite3* handle = nullptr;
  explicit Db(const fs::path& path) {
    if (sqlite3_open(path.string().c_str(), &handle) != SQLITE_OK) {
      std::string msg = sqlite3_errmsg(handle);
      sqlite3_close(handle);
      throw std::runtime_error("cannot open index db: " + msg);
    }
    // Cheap insurance for concurrent access to this per-repo cache: wait on a
    // contended lock instead of failing outright with SQLITE_BUSY (as the
    // registry does). Rebuilds go through a private temp + atomic rename, so
    // real contention on this handle is rare.
    sqlite3_busy_timeout(handle, 3000);
  }
  ~Db() { sqlite3_close(handle); }
  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;
};

struct Stmt {
  sqlite3_stmt* handle = nullptr;
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &handle, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string{"prepare failed: "} + sqlite3_errmsg(db));
    }
  }
  ~Stmt() { sqlite3_finalize(handle); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
};

void exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "unknown error";
    sqlite3_free(err);
    throw std::runtime_error("exec failed: " + msg);
  }
}

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const auto& p : parts) {
    if (!out.empty()) out += ' ';
    out += p;
  }
  return out;
}

void bind_text(sqlite3_stmt* s, int col, const std::string& v) {
  sqlite3_bind_text(s, col, v.c_str(), -1, SQLITE_TRANSIENT);
}

// 64-bit FNV-1a, hex-encoded. Deterministic across runs and platforms (unlike
// std::hash), so it makes a stable per-repo cache-directory key.
std::string fnv1a_hex(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return std::string{buf};
}

// A filesystem-safe, human-recognizable form of the repo basename.
std::string sanitize(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    out += (std::isalnum(c) || c == '.' || c == '_' || c == '-') ? static_cast<char>(c) : '-';
  }
  return out.empty() ? "repo" : out;
}

fs::path cache_home() {
  if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) return fs::path{x};
  const char* h = std::getenv("HOME");
  return fs::path{h ? h : "."} / ".cache";
}

}  // namespace

fs::path default_index_path(const fs::path& repo_root) {
  // basename keeps the dir eyeball-identifiable; the hash of the full path keeps
  // it unique across same-named repos and distinct per worktree.
  const std::string key =
      sanitize(repo_root.filename().string()) + "-" + fnv1a_hex(repo_root.string());
  return cache_home() / "rctx" / key / "index.db";
}

namespace {

// Newest mtime among claims_dir and everything under it. Both files and
// directories count, so adds, edits and deletes all move it (a dir's mtime
// bumps when a child is added or removed). nullopt if the directory is gone.
std::optional<fs::file_time_type> newest_source_mtime(const fs::path& claims_dir) {
  std::error_code ec;
  if (!fs::exists(claims_dir, ec)) return std::nullopt;
  auto newest = fs::last_write_time(claims_dir, ec);
  if (ec) newest = fs::file_time_type::min();
  std::error_code it_ec;
  for (const auto& entry : fs::recursive_directory_iterator(claims_dir, it_ec)) {
    std::error_code entry_ec;
    const auto t = fs::last_write_time(entry.path(), entry_ec);
    if (!entry_ec && t > newest) newest = t;
  }
  return newest;
}

}  // namespace

bool index_stale(const fs::path& db_path, const fs::path& claims_dir) {
  std::error_code ec;
  if (!fs::exists(db_path, ec)) return true;
  const auto db_time = fs::last_write_time(db_path, ec);
  if (ec) return true;  // can't read the index's mtime: treat as stale, rebuild
  const auto newest = newest_source_mtime(claims_dir);
  // A gone claims tree means the index holds rows for claims that no longer
  // exist, so rebuild it to empty rather than keep serving them.
  if (!newest) return true;
  return *newest > db_time;
}

void build_index(const fs::path& db_path, const std::vector<Claim>& claims) {
  if (db_path.has_parent_path()) fs::create_directories(db_path.parent_path());

  // Build into a private temp file, then atomically rename it over db_path.
  // Other processes (a git hook, a concurrent lazy query) therefore only ever
  // observe a complete index — the old one or the new one — never a db that is
  // still being built. That is what a plain rebuild in place gets wrong: the db
  // file exists the moment it is opened, so a reader can pass the staleness
  // check and then hit "no such table" (or two builders collide on DROP/CREATE)
  // before the table is committed. Same directory keeps the rename atomic.
  const fs::path tmp = db_path.string() + ".tmp." + std::to_string(::getpid());
  std::error_code ec;
  fs::remove(tmp, ec);  // clear any leftover temp from a previously killed run

  {
    Db db(tmp);
    exec(db.handle,
         "CREATE VIRTUAL TABLE claims_fts USING fts5("
         "id, scope, volatility, watches, terms, body, source_path UNINDEXED);");

    exec(db.handle, "BEGIN;");
    Stmt ins(db.handle,
             "INSERT INTO claims_fts(id, scope, volatility, watches, terms, body, source_path)"
             " VALUES(?,?,?,?,?,?,?);");
    for (const auto& c : claims) {
      std::vector<std::string> all_terms;
      for (const auto& ref : c.impacts)
        all_terms.insert(all_terms.end(), ref.terms.begin(), ref.terms.end());

      bind_text(ins.handle, 1, c.id);
      bind_text(ins.handle, 2, c.scope);
      bind_text(ins.handle, 3, c.volatility);
      bind_text(ins.handle, 4, join(c.watches));
      bind_text(ins.handle, 5, join(all_terms));
      bind_text(ins.handle, 6, c.body);
      bind_text(ins.handle, 7, c.source_path);
      if (sqlite3_step(ins.handle) != SQLITE_DONE) {
        throw std::runtime_error(std::string{"insert failed: "} + sqlite3_errmsg(db.handle));
      }
      sqlite3_reset(ins.handle);
    }
    exec(db.handle, "COMMIT;");
  }  // close the temp db (flush + drop its journal) before the rename

  fs::rename(tmp, db_path);
}

std::size_t refresh_index(const fs::path& db_path, const fs::path& claims_dir) {
  // Snapshot the newest source mtime BEFORE reading the claims. Stamping the
  // rebuilt index with this (rather than the build-finish time) means a claim
  // edited during the rebuild lands with an mtime past the stamp and is caught
  // as stale next time, instead of being masked by a freshly-written index.
  const auto snapshot = newest_source_mtime(claims_dir);
  const auto claims = load_claims(claims_dir);
  build_index(db_path, claims);
  if (snapshot) {
    std::error_code ec;
    fs::last_write_time(db_path, *snapshot, ec);  // best-effort stamp
  }
  return claims.size();
}

std::vector<SearchHit> search_index(const fs::path& db_path, const std::string& query) {
  Db db(db_path);
  Stmt q(db.handle,
         "SELECT id, source_path, snippet(claims_fts, 5, '[', ']', '…', 10)"
         " FROM claims_fts WHERE claims_fts MATCH ? ORDER BY rank;");
  bind_text(q.handle, 1, query);

  std::vector<SearchHit> hits;
  while (sqlite3_step(q.handle) == SQLITE_ROW) {
    SearchHit h;
    auto col = [&](int i) {
      const auto* t = sqlite3_column_text(q.handle, i);
      return t ? std::string{reinterpret_cast<const char*>(t)} : std::string{};
    };
    h.id = col(0);
    h.source_path = col(1);
    h.snippet = col(2);
    hits.push_back(std::move(h));
  }
  return hits;
}

}  // namespace rctx
