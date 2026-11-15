// Integration test (BL-26/#63): AnalyticsModule::difficultyIndex against the real SQLite DAL and
// QuestionBank. Two students take the same graded quiz through the real Grading Module (one
// correct, one wrong) -> 50%. A correct PRACTICE attempt is then persisted directly through the
// DAL and must NOT move the index (BR-4). The value also survives closing/reopening the database.
//
// Note: the practice attempt is written with saveGradedAttempt() rather than through
// GradingModule::gradeAttempt(), because the real DAL's findOpenAttempt() only resolves Graded
// attempts, so practice attempts cannot currently be graded via gradeAttempt() at all (see the
// PracticeModeAttemptsCannotCurrentlyBeGradedAtAll test in grading_module_tests.cpp).
#include "analytics_module.hpp"

#include <cstdio>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"
#include "quiz_types.hpp"

namespace quizsphere {
namespace {

Session instructorSession() {
    return {"instructor-1", Role::Instructor, "2026-11-15T00:00:00Z"};
}

class AnalyticsSqliteIntegrationTest : public ::testing::Test {
protected:
    std::string db_path = "analytics_module_integration_test.sqlite";

    void SetUp() override {
        removeDbFiles();
        open();

        Question q1;
        q1.question_id = "Q1";
        q1.subject_tag = "Geography";
        q1.difficulty = Difficulty::Easy;
        q1.question_type = QuestionType::MCQ;
        q1.question_text = "Capital of France?";
        q1.options = {"Berlin", "Paris", "Madrid"};
        q1.correct_option = 1;
        q1.is_available = true;
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), q1).ok());

        Quiz quiz;
        quiz.quiz_id = "quiz-1";
        quiz.title = "Geography Quiz";
        quiz.created_by = "instructor-1";
        quiz.created_at = "2026-11-15T00:00:00Z";
        quiz.time_limit_minutes = 10;
        quiz.scoring = ScoringScheme{1.0, 0.0};
        quiz.question_ids = {"Q1"};
        ASSERT_TRUE(dal_->quizzes->saveQuiz(quiz).ok());
    }

    void open() {
        std::string error;
        dal_ = createSqliteDataAccessLayer(db_path, &error);
        ASSERT_NE(dal_, nullptr) << error;

        std::string question_repo_error;
        auto question_repository = createSqliteQuestionRepository(db_path, &question_repo_error);
        ASSERT_NE(question_repository, nullptr) << question_repo_error;
        bank_ = createQuestionBank(std::move(question_repository));
        ASSERT_TRUE(bank_->load().ok());

        grading_ = createGradingModule(*bank_, *dal_->quizzes, *dal_->attempts);
        analytics_ = createAnalyticsModule(*bank_, *dal_->quizzes, *dal_->attempts);
    }

    void reopen() {
        analytics_.reset();
        grading_.reset();
        bank_.reset();
        dal_.reset();
        open();
    }

    // Starts a graded attempt for student_id and grades it through the real Grading Module.
    void gradedAttempt(const std::string& attempt_id, const std::string& student_id, int selected_option) {
        Attempt attempt;
        attempt.attempt_id = attempt_id;
        attempt.quiz_id = "quiz-1";
        attempt.student_id = student_id;
        attempt.mode = QuizMode::Graded;
        attempt.started_at = "2026-11-15T00:00:00Z";
        attempt.question_order = {"Q1"};
        ASSERT_TRUE(dal_->attempts->saveInProgressAttempt(attempt).ok());

        GradeReport report;
        std::vector<Answer> answers = {Answer{"Q1", selected_option, false}};
        ASSERT_TRUE(grading_->gradeAttempt("quiz-1", student_id, answers, &report).ok());
    }

    void TearDown() override {
        analytics_.reset();
        grading_.reset();
        bank_.reset();
        dal_.reset();
        removeDbFiles();
    }

    void removeDbFiles() {
        std::remove(db_path.c_str());
        std::remove((db_path + "-journal").c_str());
        std::remove((db_path + "-wal").c_str());
        std::remove((db_path + "-shm").c_str());
    }

    std::unique_ptr<DataAccessLayer> dal_;
    std::unique_ptr<QuestionBank> bank_;
    std::unique_ptr<GradingModule> grading_;
    std::unique_ptr<AnalyticsModule> analytics_;
};

TEST_F(AnalyticsSqliteIntegrationTest, DifficultyIndexCountsOnlyGradedAttemptsAndSurvivesRestart) {
    gradedAttempt("attempt-alice", "alice", 1);  // Paris - correct
    gradedAttempt("attempt-bob", "bob", 0);      // Berlin - wrong

    double percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);

    // A correct PRACTICE attempt persisted as a result row must not move the index (BR-4).
    Attempt practice;
    practice.attempt_id = "attempt-carol-practice";
    practice.quiz_id = "quiz-1";
    practice.student_id = "carol";
    practice.mode = QuizMode::Practice;
    practice.started_at = "2026-11-15T00:00:00Z";
    practice.submitted_at = "2026-11-15T00:05:00Z";
    practice.question_order = {"Q1"};
    practice.answers = {Answer{"Q1", 1, false}};
    practice.submitted = true;
    practice.score = 1.0;
    GradeReport practice_report;
    practice_report.score = 1.0;
    practice_report.max_score = 1.0;
    ASSERT_TRUE(dal_->attempts->saveGradedAttempt(practice, practice_report).ok());

    percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);

    // Restart: close and reopen the database; the stored graded attempts still give 50%.
    reopen();
    percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);
}

TEST_F(AnalyticsSqliteIntegrationTest, DifficultyIndexTreatsAPersistedBlankAsIncorrect) {
    gradedAttempt("attempt-alice", "alice", 1);              // correct
    gradedAttempt("attempt-bob", "bob", kBlankAnswer);       // skipped -> incorrect

    double percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);
}

}  // namespace
}  // namespace quizsphere
