#include "analytics_console.hpp"

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "grading_module.hpp"
#include "question_repository.hpp"

namespace quizsphere {
namespace {

class LeaderboardSqliteTest : public ::testing::Test {
protected:
    const std::string db_path_ = "leaderboard_sqlite_test.sqlite";
    const Session instructor_{"instructor", Role::Instructor, "2026-11-15T00:00:00Z"};
    const Session student_{"alice", Role::Student, "2026-11-15T00:00:00Z"};
    std::unique_ptr<DataAccessLayer> dal_;
    std::unique_ptr<QuestionBank> bank_;
    std::unique_ptr<GradingModule> grader_;
    std::unique_ptr<AnalyticsModule> analytics_;

    void removeFiles() {
        for (const std::string suffix : {"", "-journal", "-wal", "-shm"}) {
            std::remove((db_path_ + suffix).c_str());
        }
    }

    void open() {
        std::string error;
        dal_ = createSqliteDataAccessLayer(db_path_, &error);
        ASSERT_NE(dal_, nullptr) << error;
        auto repository = createSqliteQuestionRepository(db_path_, &error);
        ASSERT_NE(repository, nullptr) << error;
        bank_ = createQuestionBank(std::move(repository));
        ASSERT_TRUE(bank_->load().ok());
        grader_ = createGradingModule(*bank_, *dal_->quizzes, *dal_->attempts);
        analytics_ = createAnalyticsModule(*bank_, *dal_->quizzes, *dal_->attempts);
    }

    void close() {
        analytics_.reset();
        grader_.reset();
        bank_.reset();
        dal_.reset();
    }

    void SetUp() override {
        removeFiles();
        open();
        Question question{"q", "Math", Difficulty::Easy, QuestionType::MCQ,
                          "Two plus two?", {"3", "4"}, 1, true};
        ASSERT_TRUE(bank_->addQuestion(instructor_, question).ok());
        for (int i = 1; i <= 2; ++i) {
            Quiz quiz;
            quiz.quiz_id = "quiz" + std::to_string(i);
            quiz.title = "Math Quiz " + std::to_string(i);
            quiz.created_by = instructor_.user_id;
            quiz.created_at = instructor_.logged_in_at;
            quiz.time_limit_minutes = 10;
            quiz.scoring = {i == 1 ? 4.0 : 10.0, 0.0};
            quiz.question_ids = {"q"};
            ASSERT_TRUE(dal_->quizzes->saveQuiz(quiz).ok());
        }
    }

    void TearDown() override {
        close();
        removeFiles();
    }

    void grade(const std::string& quiz_id, const std::string& student_id) {
        Attempt attempt;
        attempt.attempt_id = "graded-" + student_id + "-" + quiz_id;
        attempt.quiz_id = quiz_id;
        attempt.student_id = student_id;
        attempt.mode = QuizMode::Graded;
        attempt.started_at = instructor_.logged_in_at;
        attempt.question_order = {"q"};
        ASSERT_TRUE(dal_->attempts->saveInProgressAttempt(attempt).ok());
        GradeReport report;
        ASSERT_TRUE(grader_->gradeAttempt(quiz_id, student_id, {{"q", 1, false}}, &report).ok());
    }

    // Retained legacy practice reports are allowed by the DAL, even though the current
    // practice flow does not save grades. Use the same student and quiz as a graded result.
    void saveNewerPracticeReport(const std::string& quiz_id, const std::string& student_id, double score) {
        Attempt attempt;
        attempt.attempt_id = "practice-" + student_id + "-" + quiz_id;
        attempt.quiz_id = quiz_id;
        attempt.student_id = student_id;
        attempt.mode = QuizMode::Practice;
        attempt.started_at = "2099-01-01T00:00:00Z";
        attempt.submitted_at = "2099-01-01T00:01:00Z";
        attempt.question_order = {"q"};
        attempt.answers = {{"q", 1, false}};
        attempt.submitted = true;
        attempt.score = score;
        GradeReport report;
        report.score = score;
        report.max_score = 100.0;
        report.topic_breakdown = {{"Math", 1, 1, score}};
        ASSERT_TRUE(dal_->attempts->saveGradedAttempt(attempt, report).ok());
    }

    void expectGradedRanking() const {
        std::vector<LeaderboardEntry> entries;
        ASSERT_TRUE(analytics_->topicLeaderboard("Math", &entries).ok());
        ASSERT_EQ(entries.size(), 2u);
        EXPECT_EQ(entries[0].student_id, "bob");
        EXPECT_EQ(entries[0].rank, 1);
        EXPECT_DOUBLE_EQ(entries[0].score, 10.0);
        EXPECT_EQ(entries[1].student_id, "alice");
        EXPECT_EQ(entries[1].rank, 2);
        EXPECT_DOUBLE_EQ(entries[1].score, 4.0);
        ASSERT_TRUE(analytics_->allTimeLeaderboard(&entries).ok());
        ASSERT_EQ(entries.size(), 2u);
        EXPECT_EQ(entries[0].student_id, "bob");
        EXPECT_DOUBLE_EQ(entries[0].score, 10.0);
        EXPECT_DOUBLE_EQ(entries[1].score, 4.0);
    }
};

TEST_F(LeaderboardSqliteTest, TopicRankingIgnoresNewerPracticeReportsAndSurvivesRestart) {
    grade("quiz1", "alice");
    grade("quiz2", "bob");
    expectGradedRanking();
    saveNewerPracticeReport("quiz1", "alice", 99.0);
    saveNewerPracticeReport("quiz2", "bob", 0.0);
    saveNewerPracticeReport("quiz1", "practice-only-student", 100.0);
    expectGradedRanking();
    close();
    open();
    expectGradedRanking();
}

TEST_F(LeaderboardSqliteTest, StudentCanReadAllThreeRankingsAfterRestart) {
    grade("quiz1", "alice");
    grade("quiz2", "bob");
    saveNewerPracticeReport("quiz1", "alice", 99.0);
    close();
    open();
    std::istringstream input("1\nquiz1\n2\nMath\n3\n0\n");
    std::ostringstream output;
    runStudentRankings(student_, *analytics_, *dal_->quizzes, input, output);
    const std::string text = output.str();
    EXPECT_NE(text.find("Leaderboard: quiz1"), std::string::npos);
    EXPECT_NE(text.find("Topic leaderboard: Math"), std::string::npos);
    EXPECT_NE(text.find("All-time leaderboard"), std::string::npos);
    EXPECT_NE(text.find("  1. bob  10.00"), std::string::npos);
    EXPECT_NE(text.find("  2. alice  4.00"), std::string::npos);
    EXPECT_EQ(text.find("99.00"), std::string::npos);
    EXPECT_EQ(text.find("Two plus two?"), std::string::npos);
    GradeReport report;
    ASSERT_TRUE(dal_->attempts->findReportForAttempt("graded-alice-quiz1", &report).ok());
    EXPECT_DOUBLE_EQ(report.score, 4.0);  // reading rankings never changes the result
}

TEST_F(LeaderboardSqliteTest, ExactReportLookupValidatesOutputAndDoesNotFallBackToAnotherAttempt) {
    grade("quiz1", "alice");
    saveNewerPracticeReport("quiz1", "alice", 99.0);
    GradeReport report;
    ASSERT_TRUE(dal_->attempts->findReportForAttempt("graded-alice-quiz1", &report).ok());
    EXPECT_DOUBLE_EQ(report.score, 4.0);
    ASSERT_TRUE(dal_->attempts->findReportForAttempt("practice-alice-quiz1", &report).ok());
    EXPECT_DOUBLE_EQ(report.score, 99.0);
    EXPECT_EQ(dal_->attempts->findReportForAttempt("missing", &report).code, ErrorCode::ATTEMPT_NOT_FOUND);
    EXPECT_EQ(dal_->attempts->findReportForAttempt("graded-alice-quiz1", nullptr).code,
              ErrorCode::VALIDATION_FAILED);
}

}  // namespace
}  // namespace quizsphere
