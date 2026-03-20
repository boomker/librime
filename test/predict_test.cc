//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <filesystem>
#include <fstream>
#include <vector>
#include <gtest/gtest.h>
#include <leveldb/db.h>
#include <msgpack.hpp>
#include "predict_engine.h"

using namespace rime;

namespace {

class ScopedPathCleaner {
 public:
  explicit ScopedPathCleaner(path target) : target_(std::move(target)) {}
  ~ScopedPathCleaner() {
    std::error_code ec;
    std::filesystem::remove_all(target_, ec);
  }

 private:
  path target_;
};

}  // namespace

TEST(RimePredictTest, DeleteMarksAllDuplicateWordsUnderSameKey) {
  const path db_path{"predict_db_test.userdb"};
  const path snapshot_path{"predict_db_test.userdb.txt"};
  ScopedPathCleaner db_cleaner(db_path);
  ScopedPathCleaner snapshot_cleaner(snapshot_path);

  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* raw_db = nullptr;
  ASSERT_TRUE(leveldb::DB::Open(options, db_path.string(), &raw_db).ok());

  std::vector<Prediction> predict = {
      {"候选", 1.0, 3, 1.0, 101},
      {"候选", 0.6, 2, 0.6, 102},
      {"其他", 0.4, 1, 0.4, 103},
  };
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, predict);
  ASSERT_TRUE(raw_db
                  ->Put(leveldb::WriteOptions(), "key",
                        leveldb::Slice(sbuf.data(), sbuf.size()))
                  .ok());
  delete raw_db;

  PredictDb db(db_path);
  ASSERT_TRUE(db.valid());

  db.UpdatePredict("key", "候选", true);
  ASSERT_TRUE(db.Backup(snapshot_path, 0));

  std::ifstream in(snapshot_path.string());
  ASSERT_TRUE(in.is_open());

  string line;
  int deleted_count = 0;
  while (std::getline(in, line)) {
    if (line.rfind("key\t候选\t", 0) != 0) {
      continue;
    }
    ++deleted_count;
    EXPECT_NE(line.find("c=-"), string::npos) << line;
  }
  EXPECT_EQ(2, deleted_count);
}

TEST(RimePredictTest, FallsBackToLegacyPredictDbWhenUserDbMissingKey) {
  const path user_db_path{"predict_fallback_test.userdb"};
  const path fallback_db_path{"predict_fallback_test.db"};
  ScopedPathCleaner user_db_cleaner(user_db_path);
  ScopedPathCleaner fallback_db_cleaner(fallback_db_path);

  auto user_db = std::make_shared<PredictDb>(user_db_path);
  ASSERT_TRUE(user_db->valid());

  predict_legacy::RawData fallback_data;
  fallback_data["今天"] = {
      {"天气", 2.0},
      {"不错", 1.0},
  };
  LegacyPredictDb fallback_builder(fallback_db_path);
  ASSERT_TRUE(fallback_builder.Build(fallback_data));
  ASSERT_TRUE(fallback_builder.Save());
  fallback_builder.Close();

  auto fallback_db = std::make_shared<LegacyPredictDb>(fallback_db_path);
  ASSERT_TRUE(fallback_db->Load());
  ASSERT_TRUE(fallback_db->valid());

  PredictEngine engine(user_db, fallback_db, 0, 3, 0, 0);
  ASSERT_TRUE(engine.Predict(nullptr, "今天"));
  ASSERT_EQ(2, engine.num_candidates());
  EXPECT_EQ("天气", engine.candidates(0));
  EXPECT_EQ("不错", engine.candidates(1));
}

TEST(RimePredictTest, BackupPrunesExpiredDeletedRecords) {
  const path db_path{"predict_prune_test.userdb"};
  const path snapshot_path{"predict_prune_test.userdb.txt"};
  ScopedPathCleaner db_cleaner(db_path);
  ScopedPathCleaner snapshot_cleaner(snapshot_path);

  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* raw_db = nullptr;
  ASSERT_TRUE(leveldb::DB::Open(options, db_path.string(), &raw_db).ok());

  const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
  const uint64_t one_day = 24 * 3600;
  std::vector<Prediction> predict = {
      {"过期删除", 0.8, -3, 0.0, now - 120 * one_day},
      {"未过期删除", 0.6, -2, 0.0, now - 10 * one_day},
      {"正常词", 1.0, 4, 1.0, now - 120 * one_day},
  };
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, predict);
  ASSERT_TRUE(raw_db
                  ->Put(leveldb::WriteOptions(), "key",
                        leveldb::Slice(sbuf.data(), sbuf.size()))
                  .ok());
  delete raw_db;

  PredictDb db(db_path);
  ASSERT_TRUE(db.valid());
  ASSERT_TRUE(db.Backup(snapshot_path, 90));

  std::ifstream in(snapshot_path.string());
  ASSERT_TRUE(in.is_open());

  string text((std::istreambuf_iterator<char>(in)),
              std::istreambuf_iterator<char>());
  EXPECT_EQ(text.find("\t过期删除\t"), string::npos);
  EXPECT_NE(text.find("\t未过期删除\t"), string::npos);
  EXPECT_NE(text.find("\t正常词\t"), string::npos);
}
