#include <gtest/gtest.h>
#include "../../Infrastructure/mplx-orm/db.hpp"

TEST(ORM, Transactions){
  mplx::orm::Db db(":memory:");
  db.exec("create table t(id integer primary key, name text);");
  db.begin();
  {
    mplx::orm::Stmt ins(db.handle(), "insert into t(name) values(?);");
    ins.bind_text(1, std::string("alice")); (void)ins.step(); ins.reset();
    ins.bind_text(1, std::string("bob"));   (void)ins.step(); ins.reset();
  }
  db.rollback();
  int cnt=0; mplx::orm::query(db, "select count(*) from t", [&](mplx::orm::Stmt& s){ cnt = (int)s.column_int64(0); });
  ASSERT_EQ(cnt, 0);

  db.begin();
  {
    mplx::orm::Stmt ins(db.handle(), "insert into t(name) values(?);");
    ins.bind_text(1, std::string("carol")); (void)ins.step(); ins.reset();
  }
  db.commit();
  cnt=0; mplx::orm::query(db, "select count(*) from t", [&](mplx::orm::Stmt& s){ cnt = (int)s.column_int64(0); });
  ASSERT_EQ(cnt, 1);
}


