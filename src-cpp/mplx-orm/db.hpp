#pragma once
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace mplx::orm {

class Db {
public:
  explicit Db(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
      throw std::runtime_error("sqlite open failed");
    }
  }
  ~Db() { if (db_) sqlite3_close(db_); }
  sqlite3* handle() const { return db_; }
  void exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
      std::string msg = err ? err : "sqlite exec failed";
      if (err) sqlite3_free(err);
      throw std::runtime_error(msg);
    }
  }
private:
  sqlite3* db_{};
};

class Stmt {
public:
  Stmt(sqlite3* db, const std::string& sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error("sqlite prepare failed");
    }
  }
  ~Stmt() { if (stmt_) sqlite3_finalize(stmt_); }

  bool step() { int rc = sqlite3_step(stmt_); return rc == SQLITE_ROW; }
  void reset() { sqlite3_reset(stmt_); }
  void bind_int(int idx, int v) { sqlite3_bind_int(stmt_, idx, v); }
  void bind_int64(int idx, long long v) { sqlite3_bind_int64(stmt_, idx, v); }
  void bind_text(int idx, const std::string& v) { sqlite3_bind_text(stmt_, idx, v.c_str(), -1, SQLITE_TRANSIENT); }
  int column_int(int i) const { return sqlite3_column_int(stmt_, i); }
  long long column_int64(int i) const { return sqlite3_column_int64(stmt_, i); }
  std::string column_text_str(int i) const { auto p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, i)); return p ? std::string(p) : std::string(); }

private:
  sqlite3* db_{};
  sqlite3_stmt* stmt_{};
};

} // namespace mplx::orm


