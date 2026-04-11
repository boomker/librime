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
  using Predictor::Predictor;
};

class RuleTriggerEngineForTest : public RuleTriggerEngine {
 public:
  using RuleTriggerEngine::MatchRule;
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

  PredictEngine engine(user_db, fallback_db, nullptr, 0, 3, 0, 0, true, true, 2,
                       UserRulePriority::Auto);
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
                               true, 2, UserRulePriority::Auto);
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

  PredictEngine engine(user_db, nullptr, nullptr, 0, 3, 0, 0, false, true, 2,
                       UserRulePriority::Auto);
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

  PredictEngine engine(user_db, nullptr, nullptr, 0, 5, 0, 0, false, true, 2,
                       UserRulePriority::Auto);
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

  PredictEngine engine(user_db, nullptr, rule_engine, 0, 3, 0, 0, true, true, 2,
                       UserRulePriority::Auto);
  ASSERT_TRUE(engine.Predict(nullptr, "触发"));
  ASSERT_GE(engine.num_candidates(), 2);
  // Rule candidate should be promoted before old learned candidates
  EXPECT_EQ("规则候选", engine.candidates(0));
  EXPECT_EQ("旧学习", engine.candidates(1));
}

TEST(RimePredictTest, RuleMatchTypePrefix) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("周"));
  rule->Set("match_type", New<ConfigValue>("prefix"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("周期"));
  candidates->Append(New<ConfigValue>("周报"));
  rule->Set("candidates", candidates);
  rules->Append(rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // "周" with prefix should match "周期"
  auto matched = rule_engine.Match("周期", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("周期", matched[0]);

  // "周" with prefix should match "周报"
  matched = rule_engine.Match("周报", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("周报", matched[0]);

  // "周" with prefix matches anything starting with "周"
  matched = rule_engine.Match("周好", "general");
  ASSERT_FALSE(matched.empty());  // "周好" starts with "周"

  // "周" should NOT match unrelated word "好"
  matched = rule_engine.Match("好", "general");
  EXPECT_TRUE(matched.empty());
}

TEST(RimePredictTest, RuleMatchTypeSuffix) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("好"));
  rule->Set("match_type", New<ConfigValue>("suffix"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("早上好"));
  candidates->Append(New<ConfigValue>("晚上好"));
  rule->Set("candidates", candidates);
  rules->Append(rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // "好" with suffix should match "早上好"
  auto matched = rule_engine.Match("早上好", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("早上好", matched[0]);

  // "好" with suffix should match "晚上好"
  matched = rule_engine.Match("晚上好", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("晚上好", matched[0]);

  // "好" with suffix matches anything ending with "好"
  matched = rule_engine.Match("周好", "general");
  ASSERT_FALSE(matched.empty());  // "周好" ends with "好"

  // "好" should NOT match unrelated word "周"
  matched = rule_engine.Match("周", "general");
  EXPECT_TRUE(matched.empty());
}

TEST(RimePredictTest, RuleMatchTypeContains) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("开会"));
  rule->Set("match_type", New<ConfigValue>("contains"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("下午开会"));
  candidates->Append(New<ConfigValue>("早上有开会"));
  rule->Set("candidates", candidates);
  rules->Append(rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // "开会" with contains should match "下午开会"
  auto matched = rule_engine.Match("下午开会", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("下午开会", matched[0]);
}

TEST(RimePredictTest, RuleMatchTypeExact) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("周报"));
  rule->Set("match_type", New<ConfigValue>("exact"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("本周工作总结"));
  rule->Set("candidates", candidates);
  rules->Append(rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // Exact match
  auto matched = rule_engine.Match("周报", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("本周工作总结", matched[0]);

  // "周期" should NOT match "周报" with exact type
  matched = rule_engine.Match("周期", "general");
  EXPECT_TRUE(matched.empty());
}

TEST(RimePredictTest, RuleScenesAllowlist) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  // Rule for programming scene only
  auto prog_rule = New<ConfigMap>();
  prog_rule->Set("trigger", New<ConfigValue>("gitprog"));
  prog_rule->Set("scenes", New<ConfigList>());
  As<ConfigList>(prog_rule->Get("scenes"))
      ->Append(New<ConfigValue>("programming"));
  auto prog_candidates = New<ConfigList>();
  prog_candidates->Append(New<ConfigValue>("git_commit"));
  prog_rule->Set("candidates", prog_candidates);
  rules->Append(prog_rule);

  // Rule for all scenes
  auto all_rule = New<ConfigMap>();
  all_rule->Set("trigger", New<ConfigValue>("gitalways"));
  auto all_candidates = New<ConfigList>();
  all_candidates->Append(New<ConfigValue>("git_status"));
  all_rule->Set("candidates", all_candidates);
  rules->Append(all_rule);

  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // In programming scene, both rules should match
  auto matched = rule_engine.Match("gitprog", "programming");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("git_commit", matched[0]);

  // In general scene, only the no-scene rule should match
  matched = rule_engine.Match("gitalways", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("git_status", matched[0]);

  // In programming scene, no-scene rule should also match
  matched = rule_engine.Match("gitalways", "programming");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("git_status", matched[0]);

  // In chat scene, only the no-scene rule should match
  matched = rule_engine.Match("gitalways", "chat");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("git_status", matched[0]);

  // In chat scene, scene-restricted rule should NOT match
  matched = rule_engine.Match("gitprog", "chat");
  EXPECT_TRUE(matched.empty());
}

TEST(RimePredictTest, RuleMonthDayRangeBasic) {
  RuleTriggerEngineForTest rule_engine;

  // Test month-day range within same month
  TriggerRule test_rule;
  test_rule.trigger = "test";
  test_rule.match_type = MatchType::Exact;
  test_rule.month_day_start = "01-15";
  test_rule.month_day_end = "01-20";
  test_rule.candidate = "test";

  set<string> tags;

  // Jan 17 should match
  std::tm jan_17 = {};
  jan_17.tm_mon = 0;  // January
  jan_17.tm_mday = 17;
  EXPECT_TRUE(
      rule_engine.MatchRule(test_rule, "test", "general", tags, jan_17));

  // Jan 10 should NOT match (before range)
  std::tm jan_10 = jan_17;
  jan_10.tm_mday = 10;
  EXPECT_FALSE(
      rule_engine.MatchRule(test_rule, "test", "general", tags, jan_10));

  // Jan 25 should NOT match (after range)
  std::tm jan_25 = jan_17;
  jan_25.tm_mday = 25;
  EXPECT_FALSE(
      rule_engine.MatchRule(test_rule, "test", "general", tags, jan_25));

  // Empty range should always match
  test_rule.month_day_start = "";
  test_rule.month_day_end = "";
  EXPECT_TRUE(
      rule_engine.MatchRule(test_rule, "test", "general", tags, jan_17));
}

TEST(RimePredictTest, RuleMonthDayRangeCrossYear) {
  RuleTriggerEngineForTest rule_engine;

  // Test cross-year range like Dec 25 to Jan 5
  TriggerRule test_rule;
  test_rule.trigger = "holiday";
  test_rule.match_type = MatchType::Exact;
  test_rule.month_day_start = "12-25";
  test_rule.month_day_end = "01-05";
  test_rule.candidate = "test";

  set<string> tags;

  // Dec 28 should match (within range)
  std::tm dec_28 = {};
  dec_28.tm_mon = 11;  // December
  dec_28.tm_mday = 28;
  EXPECT_TRUE(
      rule_engine.MatchRule(test_rule, "holiday", "general", tags, dec_28));

  // Dec 20 should NOT match (before range)
  std::tm dec_20 = dec_28;
  dec_20.tm_mday = 20;
  EXPECT_FALSE(
      rule_engine.MatchRule(test_rule, "holiday", "general", tags, dec_20));

  // Jan 3 should match (within range)
  std::tm jan_3 = {};
  jan_3.tm_mon = 0;  // January
  jan_3.tm_mday = 3;
  EXPECT_TRUE(
      rule_engine.MatchRule(test_rule, "holiday", "general", tags, jan_3));

  // Jan 10 should NOT match (after range)
  std::tm jan_10 = jan_3;
  jan_10.tm_mday = 10;
  EXPECT_FALSE(
      rule_engine.MatchRule(test_rule, "holiday", "general", tags, jan_10));
}

TEST(RimePredictTest, RuleCombinedMatchTypeAndScenes) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("git"));
  rule->Set("match_type", New<ConfigValue>("exact"));
  rule->Set("scenes", New<ConfigList>());
  As<ConfigList>(rule->Get("scenes"))->Append(New<ConfigValue>("programming"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("git专业命令"));
  rule->Set("candidates", candidates);
  rules->Append(rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // Should match in programming scene
  auto matched = rule_engine.Match("git", "programming");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("git专业命令", matched[0]);

  // Should NOT match in chat scene
  matched = rule_engine.Match("git", "chat");
  EXPECT_TRUE(matched.empty());
}

TEST(RimePredictTest, TemplateTimeGreeting) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto template_rule = New<ConfigMap>();
  template_rule->Set("template", New<ConfigValue>("time_greeting"));
  template_rule->Set("base_priority", New<ConfigValue>(200));

  auto items = New<ConfigList>();
  auto morning = New<ConfigMap>();
  morning->Set("trigger", New<ConfigValue>("早"));
  // 不设置 hour_min/hour_max，这样任何时间都能匹配
  auto morning_candidates = New<ConfigList>();
  morning_candidates->Append(New<ConfigValue>("早上好"));
  morning_candidates->Append(New<ConfigValue>("早安"));
  morning->Set("candidates", morning_candidates);
  items->Append(morning);

  auto noon = New<ConfigMap>();
  noon->Set("trigger", New<ConfigValue>("午"));
  auto noon_candidates = New<ConfigList>();
  noon_candidates->Append(New<ConfigValue>("中午好"));
  noon->Set("candidates", noon_candidates);
  items->Append(noon);

  template_rule->Set("items", items);
  rules->Append(template_rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // 模板应该展开成 3 条规则（2个早上 + 1个中午）
  // 当用户输入 "早" 时，应该建议 "早上好" 和 "早安"
  auto matched = rule_engine.Match("早", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ(2, matched.size());
  EXPECT_EQ("早上好", matched[0]);
  EXPECT_EQ("早安", matched[1]);

  // 当用户输入 "午" 时，应该建议 "中午好"
  matched = rule_engine.Match("午", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("中午好", matched[0]);
}

TEST(RimePredictTest, TemplateHolidayGreeting) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto template_rule = New<ConfigMap>();
  template_rule->Set("template", New<ConfigValue>("holiday_greeting"));
  template_rule->Set("base_priority", New<ConfigValue>(150));

  auto holidays = New<ConfigList>();
  holidays->Append(New<ConfigValue>("春节"));
  holidays->Append(New<ConfigValue>("中秋"));
  holidays->Append(New<ConfigValue>("端午"));
  template_rule->Set("holidays", holidays);
  template_rule->Set("candidate_template", New<ConfigValue>("{holiday}快乐"));

  rules->Append(template_rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // 模板应该展开成 3 条规则
  // trigger 默认是节日名本身，candidate 是 "{holiday}快乐"
  // 当用户输入 "春节" 时，应该建议 "春节快乐"
  auto matched = rule_engine.Match("春节", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("春节快乐", matched[0]);

  matched = rule_engine.Match("中秋", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("中秋快乐", matched[0]);

  matched = rule_engine.Match("端午", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("端午快乐", matched[0]);
}

TEST(RimePredictTest, TemplateWeekdayReminder) {
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto template_rule = New<ConfigMap>();
  template_rule->Set("template", New<ConfigValue>("weekday_reminder"));
  template_rule->Set("base_priority", New<ConfigValue>(120));

  auto items = New<ConfigList>();
  auto monday = New<ConfigMap>();
  monday->Set("trigger", New<ConfigValue>("周报"));
  // 不设置 weekday，这样任何日期都能匹配
  auto monday_candidates = New<ConfigList>();
  monday_candidates->Append(New<ConfigValue>("记得写周报"));
  monday->Set("candidates", monday_candidates);
  items->Append(monday);

  auto friday = New<ConfigMap>();
  friday->Set("trigger", New<ConfigValue>("总结"));
  auto friday_candidates = New<ConfigList>();
  friday_candidates->Append(New<ConfigValue>("周总结"));
  friday->Set("candidates", friday_candidates);
  items->Append(friday);

  template_rule->Set("items", items);
  rules->Append(template_rule);
  config.SetItem("predict_trigger_rules", rules);
  rule_engine.LoadFromConfig(&config);

  // 模板应该展开成 2 条规则
  // 当用户输入 "周报" 时，应该建议 "记得写周报"
  auto matched = rule_engine.Match("周报", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("记得写周报", matched[0]);

  matched = rule_engine.Match("总结", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("周总结", matched[0]);
}

TEST(RimePredictTest, LoadFromConfigReadsPredictorNamespace) {
  // Verify that predict_trigger_rules nested under predictor/ is loaded.
  // This matches the real schema config layout used in production.
  RuleTriggerEngine rule_engine;

  Config config;
  auto rules = New<ConfigList>();
  auto rule = New<ConfigMap>();
  rule->Set("trigger", New<ConfigValue>("我没"));
  auto candidates = New<ConfigList>();
  candidates->Append(New<ConfigValue>("token"));
  candidates->Append(New<ConfigValue>("余额"));
  rule->Set("candidates", candidates);
  rules->Append(rule);

  auto trigger_map = New<ConfigMap>();
  trigger_map->Set("rules", rules);

  auto predictor_map = New<ConfigMap>();
  predictor_map->Set("predict_trigger_rules", trigger_map);

  config.SetItem("predictor", predictor_map);
  rule_engine.LoadFromConfig(&config);

  auto matched = rule_engine.Match("我没", "general");
  ASSERT_FALSE(matched.empty());
  EXPECT_EQ("token", matched[0]);
  ASSERT_GE(matched.size(), 2u);
  EXPECT_EQ("余额", matched[1]);
}
