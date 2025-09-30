#include "../src-cpp/mplx-orm/db.hpp"
#include <filesystem>
#include <gtest/gtest.h>

TEST(Orm, Smoke) {
  namespace fs = std::filesystem;
  auto path    = fs::temp_directory_path() / "mplx_test.sqlite";
  if (fs::exists(path))
    fs::remove(path);
  mplx::orm::Db db(path.string());
  db.exec("CREATE TABLE t(a INTEGER);");
  mplx::orm::Stmt ins(db.handle(), "INSERT INTO t(a) VALUES(1);");
  ins.step();
  // no crash is ok for smoke
  SUCCEED();
}
