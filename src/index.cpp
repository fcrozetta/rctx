#include "index.hpp"

#include <stdexcept>
#include <string>

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

}  // namespace

void build_index(const fs::path& db_path, const std::vector<Claim>& claims) {
  if (db_path.has_parent_path()) fs::create_directories(db_path.parent_path());

  Db db(db_path);
  exec(db.handle, "DROP TABLE IF EXISTS claims_fts;");
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
