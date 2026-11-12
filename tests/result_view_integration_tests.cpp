// Integration test (BL-23/#61): formatItemisedResult's QuestionBank overload against the real
// SQLite DAL and QuestionBank, including after the database is closed and reopened (restart).
// A standalone formatter test over a hand-built GradeReport isn't enough on its own - this
// confirms option indexes resolve to actual, readable option text, not bare numbers.
#include "result_view.hpp"

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
    return {"instructor-1", Role::Instructor, "2026-11-12T00:00:00Z"};
}

class ResultViewIntegrationTest : public ::testing::Test {
protected:
    std::string db_path = "result_view_integration_test.sqlite";

    void SetUp() override {
        removeDbFiles();

        std::string error;
        dal_ = createSqliteDataAccessLayer(db_path, &error);
        ASSERT_NE(dal_, nullptr) << error;

        std::string question_repo_error;
        auto question_repository = createSqliteQuestionRepository(db_path, &question_repo_error);
        ASSERT_NE(question_repository, nullptr) << question_repo_error;
        bank_ = createQuestionBank(std::move(question_repository));
        ASSERT_TRUE(bank_->load().ok());

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
        quiz.created_at = "2026-11-12T00:00:00Z";
        quiz.time_limit_minutes = 10;
        quiz.scoring = ScoringScheme{1.0, 0.0};
        quiz.question_ids = {"Q1"};
        ASSERT_TRUE(dal_->quizzes->saveQuiz(quiz).ok());

        Attempt attempt;
        attempt.attempt_id = "attempt-1";
        attempt.quiz_id = "quiz-1";
        attempt.student_id = "student-1";
        attempt.mode = QuizMode::Graded;
        attempt.started_at = "2026-11-12T00:00:00Z";
        attempt.question_order = {"Q1"};
        ASSERT_TRUE(dal_->attempts->saveInProgressAttempt(attempt).ok());

        grading_ = createGradingModule(*bank_, *dal_->quizzes, *dal_->attempts);
    }

    void TearDown() override {
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
};

TEST_F(ResultViewIntegrationTest, ResolvesOriginalOrderOptionIndexesToReadableText) {
    // Selected option 0 ("Berlin"), correct is option 1 ("Paris") - both in the question's
    // ORIGINAL (unshuffled) order, matching how Grading itself scores.
    std::vector<Answer> answers = {Answer{"Q1", 0, false}};
    GradeReport report;
    ASSERT_TRUE(grading_->gradeAttempt("quiz-1", "student-1", answers, &report).ok());

    const std::string text = formatItemisedResult(report, *bank_);

    EXPECT_NE(text.find("your answer = Berlin"), std::string::npos);
    EXPECT_NE(text.find("correct answer = Paris"), std::string::npos);
    // The old index-only form must not appear - this is the whole point of the overload.
    EXPECT_EQ(text.find("your answer = 0,"), std::string::npos);
}

TEST_F(ResultViewIntegrationTest, StillResolvesReadableTextAfterClosingAndReopeningTheDatabase) {
    std::vector<Answer> answers = {Answer{"Q1", 0, false}};
    GradeReport original;
    ASSERT_TRUE(grading_->gradeAttempt("quiz-1", "student-1", answers, &original).ok());

    // Simulate a restart: close everything, then reopen from the same file.
    grading_.reset();
    dal_.reset();
    bank_.reset();

    std::string error;
    dal_ = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal_, nullptr) << error;
    std::string question_repo_error;
    auto question_repository = createSqliteQuestionRepository(db_path, &question_repo_error);
    ASSERT_NE(question_repository, nullptr) << question_repo_error;
    bank_ = createQuestionBank(std::move(question_repository));
    ASSERT_TRUE(bank_->load().ok());
    grading_ = createGradingModule(*bank_, *dal_->quizzes, *dal_->attempts);

    GradeReport reloaded;
    ASSERT_TRUE(grading_->getReport("quiz-1", "student-1", &reloaded).ok());

    const std::string text = formatItemisedResult(reloaded, *bank_);
    EXPECT_NE(text.find("your answer = Berlin"), std::string::npos);
    EXPECT_NE(text.find("correct answer = Paris"), std::string::npos);
}

}  // namespace
}  // namespace quizsphere