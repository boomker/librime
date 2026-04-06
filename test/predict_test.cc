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
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include "predictor.h"
#include "predict_engine.h"
#include "rule_trigger_engine.h"

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

class PredictorForTest : public Predictor {
 public:
  using Predictor::continuous_prediction;
  using Predictor::Predictor;
};

bool CreatePredictorWithContinuousPrediction(bool continuous_prediction) {
  the<Engine> engine(Engine::Create());
  auto* config = new Config;
  config->SetBool("predictor/continuous_prediction", continuous_prediction);
  engine->ApplySchema(new Schema("predictor_test", config));

  auto predict_engine =
      New<PredictEngine>(nullptr, nullptr, nullptr, 1, 0, 0, 0, true, true, 2);
  Ticket ticket(engine.get(), "processor", "predictor");
  PredictorForTest predictor(ticket, predict_engine);
  return predictor.continuous_prediction();
}

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

  PredictEngine engine(user_db, fallback_db, nullptr, 0, 3, 0, 0, true, true,
                       2);
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

TEST(RimePredictTest, ContinuousPredictionEnabledByConfigOnAllPlatforms) {
  EXPECT_TRUE(CreatePredictorWithContinuousPrediction(true));
  EXPECT_FALSE(CreatePredictorWithContinuousPrediction(false));
}

TEST(RimePredictTest, SceneAwareLearningPromotesSceneSpecificCandidates) {
  const path db_path{"predict_scene_test.userdb"};
  ScopedPathCleaner db_cleaner(db_path);

  auto user_db = std::make_shared<PredictDb>(db_path);
  ASSERT_TRUE(user_db->valid());

  the<Engine> engine(Engine::Create());
  auto* config = new Config;
  engine->ApplySchema(new Schema("predict_scene_test", config));
  engine->context()->set_property("predict_scene", "office");

  PredictEngine predict_engine(user_db, nullptr, nullptr, 0, 0, 0, 0, true,
                               true, 2);
  engine->context()->commit_history().Push(CommitRecord("phrase", "请查收"));
  engine->context()->commit_history().Push(CommitRecord("phrase", "附件"));
  predict_engine.UpdatePredict(engine->context(), "请查收", "附件", false);

  engine->context()->set_property("predict_scene", "chat");
  engine->context()->commit_history().Push(CommitRecord("phrase", "请查收"));
  engine->context()->commit_history().Push(CommitRecord("phrase", "收到啦"));
  predict_engine.UpdatePredict(engine->context(), "请查收", "收到啦", false);

  engine->context()->set_property("predict_scene", "office");
  ASSERT_TRUE(predict_engine.Predict(engine->context(), "请查收"));
  ASSERT_EQ("附件", predict_engine.candidates(0));

  engine->context()->set_property("predict_scene", "chat");
  ASSERT_TRUE(predict_engine.Predict(engine->context(), "请查收"));
  ASSERT_EQ("收到啦", predict_engine.candidates(0));
}

TEST(RimePredictTest, RuleTriggerEngineLoadsCalendarAndSceneRules) {
  const path calendar_path{"predict_calendar_test.yaml"};
  const path rules_path{"trigger_rules_test.db"};
  ScopedPathCleaner calendar_cleaner(calendar_path);
  ScopedPathCleaner rules_cleaner(rules_path);

  std::ofstream out(calendar_path.string());
  ASSERT_TRUE(out.is_open());
  out << "solar_terms:\n";
  out << "  \"04-04\": 清明\n";
  out << "holidays:\n";
  out << "  \"2026-04-04\": [清明节]\n";
  out.close();

  RuleTriggerEngine rule_engine;
  ASSERT_TRUE(rule_engine.LoadCalendar(calendar_path));
  ASSERT_TRUE(rule_engine.LoadFromDB(rules_path));

  Config config;
  auto rules = New<ConfigList>();
  auto scene_rule = New<ConfigMap>();
  scene_rule->Set("trigger", New<ConfigValue>("请查收"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("附件"));
  scene_rule->Set("candidates", candidates);
  scene_rule->Set("tag", New<ConfigValue>("scene_office"));
  rules->Append(scene_rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  auto matched = rule_engine.Match("请查收", "office");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("附件", matched.front());
}

TEST(RimePredictTest, RecentLearningPrioritizedOverHighCommitCount) {
  const path db_path{"predict_ranking_recent_test.userdb"};
  ScopedPathCleaner db_cleaner(db_path);

  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* raw_db = nullptr;
  ASSERT_TRUE(leveldb::DB::Open(options, db_path.string(), &raw_db).ok());

  const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
  const uint64_t one_minute = 60;

  // Both candidates within 30-min window, but different recency and commits
  std::vector<Prediction> predict = {
      {"旧候选", 1.0, 10, 1.0, now - 25 * one_minute},  // Older, more commits
      {"新候选", 1.0, 3, 1.0, now - 5 * one_minute},    // Newer, fewer commits
  };
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, predict);
  ASSERT_TRUE(raw_db
                  ->Put(leveldb::WriteOptions(), "测试",
                        leveldb::Slice(sbuf.data(), sbuf.size()))
                  .ok());
  delete raw_db;

  auto user_db = std::make_shared<PredictDb>(db_path);
  ASSERT_TRUE(user_db->valid());

  PredictEngine engine(user_db, nullptr, nullptr, 0, 3, 0, 0, false, true, 2);
  ASSERT_TRUE(engine.Predict(nullptr, "测试"));
  ASSERT_GE(engine.num_candidates(), 1);
  // Recent candidate should rank first despite fewer commits
  EXPECT_EQ("新候选", engine.candidates(0));
}

TEST(RimePredictTest, ContextualCandidatesRankedByRecency) {
  const path db_path{"predict_ranking_contextual_test.userdb"};
  ScopedPathCleaner db_cleaner(db_path);

  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* raw_db = nullptr;
  ASSERT_TRUE(leveldb::DB::Open(options, db_path.string(), &raw_db).ok());

  const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
  const uint64_t one_hour = 3600;

  // Contextual candidates within 2-hour window (requires chain key with 2
  // items)
  std::vector<Prediction> contextual_predict = {
      {"旧上下文", 1.0, 8, 1.0, now - 90 * 60},  // Older, more commits
      {"新上下文", 1.0, 2, 1.0, now - 30 * 60},  // Newer, fewer commits
  };
  msgpack::sbuffer contextual_buf;
  msgpack::pack(contextual_buf, contextual_predict);
  // Chain key format: __chain__:item1\nitem2 (requires at least 2 commit
  // history items)
  ASSERT_TRUE(
      raw_db
          ->Put(leveldb::WriteOptions(), "__chain__:甲\n乙",
                leveldb::Slice(contextual_buf.data(), contextual_buf.size()))
          .ok());
  delete raw_db;

  auto user_db = std::make_shared<PredictDb>(db_path);
  ASSERT_TRUE(user_db->valid());

  the<Engine> engine_ctx(Engine::Create());
  auto* config = new Config;
  engine_ctx->ApplySchema(new Schema("contextual_test", config));
  // Push two commit history items to match the chain key
  engine_ctx->context()->commit_history().Push(CommitRecord("phrase", "甲"));
  engine_ctx->context()->commit_history().Push(CommitRecord("phrase", "乙"));

  PredictEngine engine(user_db, nullptr, nullptr, 0, 5, 0, 0, false, true, 2);
  ASSERT_TRUE(engine.Predict(engine_ctx->context(), "查询"));
  ASSERT_GE(engine.num_candidates(), 2);

  // Find positions of contextual candidates
  int new_pos = -1, old_pos = -1;
  for (int i = 0; i < engine.num_candidates(); ++i) {
    if (engine.candidates(i) == "新上下文")
      new_pos = i;
    if (engine.candidates(i) == "旧上下文")
      old_pos = i;
  }

  // Recent contextual candidate should rank before older one
  ASSERT_NE(new_pos, -1) << "新上下文 not found in candidates";
  ASSERT_NE(old_pos, -1) << "旧上下文 not found in candidates";
  EXPECT_LT(new_pos, old_pos) << "新上下文 should rank before 旧上下文";
}

TEST(RimePredictTest, RuleCandidatesPromotedWhenNoRecentLearning) {
  const path db_path{"predict_ranking_rule_test.userdb"};
  ScopedPathCleaner db_cleaner(db_path);

  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* raw_db = nullptr;
  ASSERT_TRUE(leveldb::DB::Open(options, db_path.string(), &raw_db).ok());

  const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
  const uint64_t one_hour = 3600;

  // All learning candidates outside both time windows (30-min and 2-hour)
  std::vector<Prediction> predict = {
      {"旧学习", 1.0, 5, 1.0, now - 3 * one_hour},
  };
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, predict);
  ASSERT_TRUE(raw_db
                  ->Put(leveldb::WriteOptions(), "触发",
                        leveldb::Slice(sbuf.data(), sbuf.size()))
                  .ok());
  delete raw_db;

  auto user_db = std::make_shared<PredictDb>(db_path);
  ASSERT_TRUE(user_db->valid());

  // Create rule engine with a matching rule
  auto rule_engine = std::make_shared<RuleTriggerEngine>();
  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("触发"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("规则候选"));
  rule->Set("candidates", candidates);
  rules->Append(rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine->LoadFromConfig(&config);

  PredictEngine engine(user_db, nullptr, rule_engine, 0, 3, 0, 0, true, true,
                       2);
  ASSERT_TRUE(engine.Predict(nullptr, "触发"));
  ASSERT_GE(engine.num_candidates(), 2);
  // Rule candidate should be promoted before old learned candidates
  EXPECT_EQ("规则候选", engine.candidates(0));
  EXPECT_EQ("旧学习", engine.candidates(1));
}
