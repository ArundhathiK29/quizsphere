#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "analytics_module.hpp"
#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "quiz_engine.hpp"

using namespace quizsphere;

// BL-01 / MNT-01: the agreed shared structs and module interfaces exist, link together and can be wired up.
// These tests deliberately do NOT assert that stubs return NOT_IMPLEMENTED, so module owners can replace the
// stubs without touching this file.

TEST(ModuleInterfaces, MNT_01_SharedStructsHaveTheAgreedDefaults) {
    const Answer answer;
    EXPECT_EQ(answer.selected_option, kBlankAnswer);
    EXPECT_FALSE(answer.flagged_for_review);

    const Attempt attempt;
    EXPECT_EQ(attempt.mode, QuizMode::Graded);
    EXPECT_FALSE(attempt.submitted);
    EXPECT_FALSE(attempt.auto_submitted);
    EXPECT_TRUE(attempt.answers.empty());

    const ScoringScheme scoring;
    EXPECT_DOUBLE_EQ(scoring.marks_per_question, 1.0);
    EXPECT_DOUBLE_EQ(scoring.negative_mark, 0.0);

    const GradeReport report;
    EXPECT_DOUBLE_EQ(report.score, 0.0);
    EXPECT_TRUE(report.itemised_breakdown.empty());
    EXPECT_TRUE(report.topic_breakdown.empty());
}

TEST(ModuleInterfaces, MNT_01_AllModulesCanBeWiredTogether) {
    std::string error;
    auto dal = createSqliteDataAccessLayer(":memory:", &error);
    ASSERT_NE(dal, nullptr) << error;
    ASSERT_NE(dal->users, nullptr);
    ASSERT_NE(dal->quizzes, nullptr);
    ASSERT_NE(dal->attempts, nullptr);

    auto bank = createQuestionBank(createFileQuestionRepository());
    ASSERT_NE(bank, nullptr);

    auto grading = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
    ASSERT_NE(grading, nullptr);
    auto engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grading);
    EXPECT_NE(engine, nullptr);
    auto analytics = createAnalyticsModule(*bank, *dal->quizzes, *dal->attempts);
    EXPECT_NE(analytics, nullptr);
}

TEST(ModuleInterfaces, DataAccessLayerReportsAnUnopenableDatabase) {
    std::string error;
    auto dal = createSqliteDataAccessLayer("/nonexistent-directory-for-quizsphere/x.db", &error);
    EXPECT_EQ(dal, nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(ModuleInterfaces, UnfinishedFeaturesReportNotImplementedInsteadOfSucceeding) {
    const Result result = notImplemented("Example::feature");
    EXPECT_EQ(result.code, ErrorCode::NOT_IMPLEMENTED);
    EXPECT_NE(result.message.find("Example::feature"), std::string::npos);
}
