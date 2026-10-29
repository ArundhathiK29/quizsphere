// Integration test (IT-01 / PR #59 checklist item 7): grading against Ashwin's real SQLite DAL
// (data_access_layer.hpp/.cpp, merged in PR #56) and the real file-backed QuestionBank, including
// retrieval of a graded result after the database is closed and reopened (simulating a restart).
#include "grading_module.hpp"

#include <cstdio>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "data_access_layer.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"
#include "quiz_types.hpp"

namespace quizsphere {
namespace {

Session instructorSession() {
    return {"instructor-1", Role::Instructor, "2026-10-24T00:00:00Z"};
}

Question makeQuestion(const std::string& id, const std::string& subject_tag, std::size_t correct_option) {
    Question q;
    q.question_id = id;
    q.subject_tag = subject_tag;
    q.difficulty = Difficulty::Easy;
    q.question_type = QuestionType::MCQ;
    q.question_text = "Q " + id;
    q.options = {"a", "b", "c"};
    q.correct_option = correct_option;
    q.is_available = true;
    return q;
}

class GradingSqliteIntegrationTest : public ::testing::Test {
protected:
    std::string db_path = "grading_integration_test.sqlite";
    std::string question_path = "grading_integration_test_questions.tsv";

    void SetUp() override {
        removeDbFiles();
        std::remove(question_path.c_str());

        std::string error;
        dal_ = createSqliteDataAccessLayer(db_path, &error);
        ASSERT_NE(dal_, nullptr) << error;
        ASSERT_NE(dal_->quizzes, nullptr);
        ASSERT_NE(dal_->attempts, nullptr);

        bank_ = createQuestionBank(createFileQuestionRepository(question_path));
        ASSERT_TRUE(bank_->load().ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q1", "Math", 1)).ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q2", "Science", 0)).ok());

        Quiz quiz;
        quiz.quiz_id = "quiz-1";
        quiz.title = "Integration Quiz";
        quiz.created_by = "instructor-1";
        quiz.created_at = "2026-10-29T00:00:00Z";
        quiz.time_limit_minutes = 10;
        quiz.scoring = ScoringScheme{1.0, 0.25};
        quiz.question_ids = {"Q1", "Q2"};
        ASSERT_TRUE(dal_->quizzes->saveQuiz(quiz).ok());

        Attempt attempt;
        attempt.attempt_id = "attempt-1";
        attempt.quiz_id = "quiz-1";
        attempt.student_id = "student-1";
        attempt.mode = QuizMode::Graded;
        attempt.started_at = "2026-10-29T00:00:00Z";
        attempt.question_order = {"Q1", "Q2"};
        ASSERT_TRUE(dal_->attempts->saveInProgressAttempt(attempt).ok());

        grading_ = createGradingModule(*bank_, *dal_->quizzes, *dal_->attempts);
    }

    void TearDown() override {
        dal_.reset();
        removeDbFiles();
        std::remove(question_path.c_str());
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

TEST_F(GradingSqliteIntegrationTest, GradesAgainstTheRealDalAndQuestionBank) {
    std::vector<Answer> answers = {
        Answer{"Q1", 1, false},  // correct
        Answer{"Q2", 1, false},  // wrong (correct is 0)
    };

    GradeReport report;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", answers, &report);

    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_DOUBLE_EQ(report.score, 0.75);  // +1.0 correct, -0.25 wrong
    EXPECT_DOUBLE_EQ(report.max_score, 2.0);
    ASSERT_EQ(report.itemised_breakdown.size(), 2u);
}

// IT-01 / PR #59 checklist: "Test ... result retrieval after restart."
TEST_F(GradingSqliteIntegrationTest, GradedResultSurvivesClosingAndReopeningTheDatabase) {
    std::vector<Answer> answers = {
        Answer{"Q1", 1, false},
        Answer{"Q2", 1, false},
    };
    GradeReport original;
    ASSERT_TRUE(grading_->gradeAttempt("quiz-1", "student-1", answers, &original).ok());

    // Simulate a restart: close the database and every in-memory object that held a handle to it,
    // then reopen from the same file and rebuild Grading on top of the fresh DAL.
    grading_.reset();
    dal_.reset();

    std::string error;
    dal_ = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal_, nullptr) << error;
    grading_ = createGradingModule(*bank_, *dal_->quizzes, *dal_->attempts);

    GradeReport reloaded;
    const Result result = grading_->getReport("quiz-1", "student-1", &reloaded);

    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_DOUBLE_EQ(reloaded.score, original.score);
    EXPECT_DOUBLE_EQ(reloaded.max_score, original.max_score);
    ASSERT_EQ(reloaded.itemised_breakdown.size(), original.itemised_breakdown.size());
    for (std::size_t i = 0; i < reloaded.itemised_breakdown.size(); ++i) {
        EXPECT_EQ(reloaded.itemised_breakdown[i].question_id, original.itemised_breakdown[i].question_id);
        EXPECT_DOUBLE_EQ(reloaded.itemised_breakdown[i].marks_awarded, original.itemised_breakdown[i].marks_awarded);
    }

    // And re-grading after restart is still rejected (NFR-006), not silently allowed just because
    // the process restarted. The real DAL moves a graded attempt out of the "open" table entirely,
    // so a blind re-submit (no fresh open attempt re-seeded) surfaces as ATTEMPT_NOT_FOUND here -
    // a different error path than the ATTEMPT_ALREADY_SUBMITTED case covered by
    // UT_GRADE_04/UT_GRADE_07 in grading_module_tests.cpp (where a *new* open attempt exists), but
    // an equally firm rejection either way.
    const Result regrade_result = grading_->gradeAttempt("quiz-1", "student-1", answers, &reloaded);
    EXPECT_EQ(regrade_result.code, ErrorCode::ATTEMPT_NOT_FOUND);

    // If a fresh open attempt DOES exist (e.g. a disallowed second Graded-mode attempt slipped
    // through), the real DAL's own saveGradedAttempt duplicate check rejects it with
    // ATTEMPT_ALREADY_SUBMITTED, and the original stored result is untouched.
    Attempt second_attempt;
    second_attempt.attempt_id = "attempt-2";
    second_attempt.quiz_id = "quiz-1";
    second_attempt.student_id = "student-1";
    second_attempt.mode = QuizMode::Graded;
    second_attempt.started_at = "2026-10-29T01:00:00Z";
    second_attempt.question_order = {"Q1", "Q2"};
    ASSERT_TRUE(dal_->attempts->saveInProgressAttempt(second_attempt).ok());

    GradeReport second_report;
    const Result second_result = grading_->gradeAttempt("quiz-1", "student-1", answers, &second_report);
    EXPECT_EQ(second_result.code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);

    GradeReport still_original;
    ASSERT_TRUE(grading_->getReport("quiz-1", "student-1", &still_original).ok());
    EXPECT_DOUBLE_EQ(still_original.score, original.score);
}

}  // namespace
}  // namespace quizsphere
