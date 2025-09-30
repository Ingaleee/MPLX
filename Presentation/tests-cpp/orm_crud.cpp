#include "../src-cpp/mplx-orm/db.hpp"
#include <filesystem>
#include <gtest/gtest.h>

TEST(Orm, CRUD) {
  namespace fs = std::filesystem;
  auto path    = fs::temp_directory_path() / "mplx_orm_crud.sqlite";
  if (fs::exists(path))
    fs::remove(path);

  mplx::orm::Db db(path.string());
  db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT);");

  {
    mplx::orm::Stmt ins(db.handle(), "INSERT INTO t(name) VALUES(?);");
    ins.bind_text(1, std::string("alice"));
    (void)ins.step();
    ins.reset();
    ins.bind_text(1, std::string("bob"));
    (void)ins.step();
    ins.reset();
  }

  int count = 0;
  mplx::orm::query(db, "SELECT id,name FROM t ORDER BY id", [&](mplx::orm::Stmt &s) {
    auto id   = s.column_int64(0);
    auto name = s.column_text_str(1);
    ASSERT_GE(id, 1);
    ASSERT_FALSE(name.empty());
    ++count;
  });
  ASSERT_EQ(count, 2);
}
