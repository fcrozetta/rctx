#include "registry.hpp"

#include <cstdlib>
#include <stdexcept>

#include <sqlite3.h>

namespace fs = std::filesystem;

namespace rctx {

namespace {

struct Db {
  sqlite3* handle = nullptr;
  explicit Db(const fs::path& path) {
    if (sqlite3_open(path.string().c_str(), &handle) != SQLITE_OK) {
      std::string msg = sqlite3_errmsg(handle);
      sqlite3_close(handle);
      throw std::runtime_error("cannot open registry db: " + msg);
    }
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

void bind_text(sqlite3_stmt* s, int col, const std::string& v) {
  sqlite3_bind_text(s, col, v.c_str(), -1, SQLITE_TRANSIENT);
}

std::string column(sqlite3_stmt* s, int col) {
  const auto* t = sqlite3_column_text(s, col);
  return t ? std::string{reinterpret_cast<const char*>(t)} : std::string{};
}

void ensure_schema(sqlite3* db) {
  exec(db,
       "CREATE TABLE IF NOT EXISTS repos("
       "path TEXT PRIMARY KEY, url TEXT, branch TEXT, default_branch TEXT, last_seen TEXT);");
}

}  // namespace

fs::path registry_path() {
  if (const char* home = std::getenv("RCTX_HOME"); home && *home) {
    return fs::path{home} / "registry.db";
  }
  const char* h = std::getenv("HOME");
  return fs::path{h ? h : "."} / ".rctx" / "registry.db";
}

void register_repo(const RepoEntry& entry) {
  const fs::path db_path = registry_path();
  if (db_path.has_parent_path()) fs::create_directories(db_path.parent_path());

  Db db(db_path);
  ensure_schema(db.handle);
  Stmt ins(db.handle,
           "INSERT INTO repos(path, url, branch, default_branch, last_seen)"
           " VALUES(?,?,?,?,datetime('now'))"
           " ON CONFLICT(path) DO UPDATE SET"
           " url=excluded.url, branch=excluded.branch,"
           " default_branch=excluded.default_branch, last_seen=excluded.last_seen;");
  bind_text(ins.handle, 1, entry.path);
  bind_text(ins.handle, 2, entry.url);
  bind_text(ins.handle, 3, entry.branch);
  bind_text(ins.handle, 4, entry.default_branch);
  if (sqlite3_step(ins.handle) != SQLITE_DONE) {
    throw std::runtime_error(std::string{"register failed: "} + sqlite3_errmsg(db.handle));
  }
}

std::vector<RepoEntry> list_repos() {
  std::vector<RepoEntry> repos;
  const fs::path db_path = registry_path();
  if (!fs::exists(db_path)) return repos;

  Db db(db_path);
  ensure_schema(db.handle);
  Stmt q(db.handle, "SELECT url, path, branch, default_branch FROM repos ORDER BY path;");
  while (sqlite3_step(q.handle) == SQLITE_ROW) {
    RepoEntry e;
    e.url = column(q.handle, 0);
    e.path = column(q.handle, 1);
    e.branch = column(q.handle, 2);
    e.default_branch = column(q.handle, 3);
    repos.push_back(std::move(e));
  }
  return repos;
}

bool forget_repo(const std::string& path) {
  const fs::path db_path = registry_path();
  if (!fs::exists(db_path)) return false;

  Db db(db_path);
  ensure_schema(db.handle);
  Stmt del(db.handle, "DELETE FROM repos WHERE path=?;");
  bind_text(del.handle, 1, path);
  if (sqlite3_step(del.handle) != SQLITE_DONE) {
    throw std::runtime_error(std::string{"forget failed: "} + sqlite3_errmsg(db.handle));
  }
  return sqlite3_changes(db.handle) > 0;
}

std::vector<std::string> prune_missing_repos() {
  std::vector<std::string> removed;
  const fs::path db_path = registry_path();
  if (!fs::exists(db_path)) return removed;

  Db db(db_path);
  ensure_schema(db.handle);

  // Collect the dead paths first (statement finalized before we delete).
  {
    Stmt q(db.handle, "SELECT path FROM repos ORDER BY path;");
    while (sqlite3_step(q.handle) == SQLITE_ROW) {
      std::string p = column(q.handle, 0);
      if (!fs::exists(fs::path{p})) removed.push_back(std::move(p));
    }
  }
  for (const auto& p : removed) {
    Stmt del(db.handle, "DELETE FROM repos WHERE path=?;");
    bind_text(del.handle, 1, p);
    if (sqlite3_step(del.handle) != SQLITE_DONE) {
      throw std::runtime_error(std::string{"prune failed: "} + sqlite3_errmsg(db.handle));
    }
  }
  return removed;
}

}  // namespace rctx
