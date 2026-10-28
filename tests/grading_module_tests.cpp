#include "grading_module.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "data_access_layer.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"
#include "quiz_types.hpp"

namespace {

using namespace quizsphere;

Session instructorSession() {
    return {"instructor-1", Role::Instructor, "2026-10-24T00:00:00Z"};
}

// BL-10 test doubles: BL-06 (the real SQLite DAL) is not implemented yet, so grading is exercised
// against fakes that satisfy the agreed BL-01 interfaces (data_access_layer.hpp, question_repository.hpp).

class InMemoryQuestionRepository final : public QuestionRepository {
public:
    Result loadAll(std::vector<Question>* questions) override {
        *questions = stored_;
        return {ErrorCode::OK, "ok"};
    }
    Result saveAll(const std::vector<Question>& questions) override {
        stored_ = questions;
        return {ErrorCode::OK, "ok"};
    }
    Result readTextFile(const std::string&, std::string*) const override {
        return {ErrorCode::IO_ERROR, "not used"};
    }
    Result writeTextFileAtomic(const std::string&, const std::string&, bool) const override {
        return {ErrorCode::IO_ERROR, "not used"};
    }
    bool isReferenced(const std::string&) const override { return false; }

private:
    std::vector<Question> stored_;
};

class FakeQuizRepository final : public QuizRepository {
public:
    Result saveQuiz(const Quiz& quiz) override {
        quizzes_[quiz.quiz_id] = quiz;
        return {ErrorCode::OK, "ok"};
    }
    Result findQuiz(const std::string& quiz_id, Quiz* quiz) const override {
        const auto it = quizzes_.find(quiz_id);
        if (it == quizzes_.end()) {
            return {ErrorCode::QUIZ_NOT_FOUND, "Quiz not found."};
        }
        *quiz = it->second;
        return {ErrorCode::OK, "ok"};
    }
    Result listQuizzes(std::vector<Quiz>* quizzes) const override {
        quizzes->clear();
        for (const auto& entry : quizzes_) {
            quizzes->push_back(entry.second);
        }
        return {ErrorCode::OK, "ok"};
    }

private:
    std::unordered_map<std::string, Quiz> quizzes_;
};

class FakeAttemptRepository final : public AttemptRepository {
public:
    Result saveInProgressAttempt(const Attempt& attempt) override {
        open_[key(attempt.quiz_id, attempt.student_id)] = attempt;
        return {ErrorCode::OK, "ok"};
    }
    Result findAttempt(const std::string& attempt_id, Attempt* attempt) const override {
        for (const auto& entry : open_) {
            if (entry.second.attempt_id == attempt_id) {
                *attempt = entry.second;
                return {ErrorCode::OK, "ok"};
            }
        }
        for (const auto& entry : graded_) {
            if (entry.second.first.attempt_id == attempt_id) {
                *attempt = entry.second.first;
                return {ErrorCode::OK, "ok"};
            }
        }
        return {ErrorCode::ATTEMPT_NOT_FOUND, "Attempt not found."};
    }
    Result findOpenAttempt(const std::string& quiz_id, const std::string& student_id, Attempt* attempt) const override {
        const auto it = open_.find(key(quiz_id, student_id));
        if (it == open_.end()) {
            return {ErrorCode::ATTEMPT_NOT_FOUND, "No open attempt."};
        }
        *attempt = it->second;
        return {ErrorCode::OK, "ok"};
    }
    Result saveGradedAttempt(const Attempt& attempt, const GradeReport& report) override {
        const std::string k = key(attempt.quiz_id, attempt.student_id);
        if (graded_.find(k) != graded_.end()) {
            return {ErrorCode::ATTEMPT_ALREADY_SUBMITTED, "Attempt already submitted."};
        }
        graded_[k] = {attempt, report};
        open_.erase(k);
        return {ErrorCode::OK, "ok"};
    }
    Result findReport(const std::string& quiz_id, const std::string& student_id, GradeReport* report) const override {
        const auto it = graded_.find(key(quiz_id, student_id));
        if (it == graded_.end()) {
            return {ErrorCode::ATTEMPT_NOT_FOUND, "No graded report."};
        }
        *report = it->second.second;
        return {ErrorCode::OK, "ok"};
    }
    Result listGradedAttempts(const std::string& quiz_id, std::vector<Attempt>* attempts) const override {
        attempts->clear();
        for (const auto& entry : graded_) {
            if (entry.second.first.quiz_id == quiz_id) {
                attempts->push_back(entry.second.first);
            }
        }
        return {ErrorCode::OK, "ok"};
    }
    Result listAttemptsForStudent(const std::string& student_id, std::vector<Attempt>* attempts) const override {
        attempts->clear();
        for (const auto& entry : graded_) {
            if (entry.second.first.student_id == student_id) {
                attempts->push_back(entry.second.first);
            }
        }
        return {ErrorCode::OK, "ok"};
    }

    void seedOpenAttempt(const Attempt& attempt) { open_[key(attempt.quiz_id, attempt.student_id)] = attempt; }
    const Attempt* gradedAttempt(const std::string& quiz_id, const std::string& student_id) const {
        const auto it = graded_.find(key(quiz_id, student_id));
        return it == graded_.end() ? nullptr : &it->second.first;
    }

private:
    static std::string key(const std::string& quiz_id, const std::string& student_id) {
        return quiz_id + "|" + student_id;
    }
    std::unordered_map<std::string, Attempt> open_;
    std::unordered_map<std::string, std::pair<Attempt, GradeReport>> graded_;
};

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

class GradingModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        bank_ = createQuestionBank(std::make_unique<InMemoryQuestionRepository>());
        ASSERT_TRUE(bank_->load().ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q1", "Math", 1)).ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q2", "Science", 0)).ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q3", "Math", 2)).ok());

        grading_ = createGradingModule(*bank_, quizzes_, attempts_);
    }

    Quiz makeQuiz(double marks_per_question, double negative_mark) {
        Quiz quiz;
        quiz.quiz_id = "quiz-1";
        quiz.title = "Sample Quiz";
        quiz.created_by = "instructor-1";
        quiz.created_at = "2026-10-24T00:00:00Z";
        quiz.time_limit_minutes = 10;
        quiz.scoring = ScoringScheme{marks_per_question, negative_mark};
        quiz.question_ids = {"Q1", "Q2", "Q3"};
        quizzes_.saveQuiz(quiz);
        return quiz;
    }

    Attempt makeOpenAttempt(bool auto_submitted = false) {
        Attempt attempt;
        attempt.attempt_id = "attempt-1";
        attempt.quiz_id = "quiz-1";
        attempt.student_id = "student-1";
        attempt.mode = QuizMode::Graded;
        attempt.started_at = "2026-10-24T00:00:00Z";
        attempt.question_order = {"Q1", "Q2", "Q3"};
        attempt.auto_submitted = auto_submitted;
        attempts_.seedOpenAttempt(attempt);
        return attempt;
    }

    std::vector<Answer> sampleAnswers() {
        // Q1 correct (selected == correct_option 1), Q2 wrong (selected 2, correct is 0), Q3 blank.
        return {
            Answer{"Q1", 1, false},
            Answer{"Q2", 2, false},
        };
    }

    std::unique_ptr<QuestionBank> bank_;
    FakeQuizRepository quizzes_;
    FakeAttemptRepository attempts_;
    std::unique_ptr<GradingModule> grading_;
};

TEST_F(GradingModuleTest, UT_GRADE_01_ScoresCorrectWrongAndBlankAnswers) {
    makeQuiz(/*marks_per_question=*/2.0, /*negative_mark=*/0.0);
    makeOpenAttempt();

    GradeReport report;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &report);

    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(report.score, 2.0);
    EXPECT_DOUBLE_EQ(report.max_score, 6.0);
    ASSERT_EQ(report.itemised_breakdown.size(), 3u);

    EXPECT_EQ(report.itemised_breakdown[0].question_id, "Q1");
    EXPECT_TRUE(report.itemised_breakdown[0].correct);
    EXPECT_FALSE(report.itemised_breakdown[0].blank);
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[0].marks_awarded, 2.0);

    EXPECT_EQ(report.itemised_breakdown[1].question_id, "Q2");
    EXPECT_FALSE(report.itemised_breakdown[1].correct);
    EXPECT_FALSE(report.itemised_breakdown[1].blank);
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[1].marks_awarded, 0.0);  // no negative marking configured

    EXPECT_EQ(report.itemised_breakdown[2].question_id, "Q3");
    EXPECT_TRUE(report.itemised_breakdown[2].blank);
    EXPECT_FALSE(report.itemised_breakdown[2].correct);
    EXPECT_EQ(report.itemised_breakdown[2].selected_option, kBlankAnswer);
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[2].marks_awarded, 0.0);
}

TEST_F(GradingModuleTest, UT_GRADE_02_NegativeMarkingSubtractsForWrongAnswersOnly) {
    makeQuiz(/*marks_per_question=*/1.0, /*negative_mark=*/0.25);
    makeOpenAttempt();

    GradeReport report;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &report);

    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[0].marks_awarded, 1.0);    // correct
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[1].marks_awarded, -0.25);  // wrong
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[2].marks_awarded, 0.0);    // blank, never penalised
    EXPECT_DOUBLE_EQ(report.score, 0.75);
    EXPECT_DOUBLE_EQ(report.max_score, 3.0);
}

TEST_F(GradingModuleTest, UT_GRADE_03_GroupsTopicBreakdownBySubjectTag) {
    makeQuiz(1.0, 0.0);
    makeOpenAttempt();

    GradeReport report;
    ASSERT_TRUE(grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &report).ok());

    ASSERT_EQ(report.topic_breakdown.size(), 2u);
    EXPECT_EQ(report.topic_breakdown[0].subject_tag, "Math");
    EXPECT_EQ(report.topic_breakdown[0].total, 2);
    EXPECT_EQ(report.topic_breakdown[0].correct, 1);
    EXPECT_DOUBLE_EQ(report.topic_breakdown[0].marks, 1.0);

    EXPECT_EQ(report.topic_breakdown[1].subject_tag, "Science");
    EXPECT_EQ(report.topic_breakdown[1].total, 1);
    EXPECT_EQ(report.topic_breakdown[1].correct, 0);
    EXPECT_DOUBLE_EQ(report.topic_breakdown[1].marks, 0.0);
}

TEST_F(GradingModuleTest, UT_GRADE_04_PersistsGradedAttemptAndIsRetrievableViaGetReport) {
    makeQuiz(1.0, 0.0);
    makeOpenAttempt();

    GradeReport graded_report;
    ASSERT_TRUE(grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &graded_report).ok());

    const Attempt* stored = attempts_.gradedAttempt("quiz-1", "student-1");
    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->submitted);
    EXPECT_DOUBLE_EQ(stored->score, graded_report.score);
    EXPECT_FALSE(stored->submitted_at.empty());
    ASSERT_EQ(stored->answers.size(), sampleAnswers().size());

    GradeReport fetched_report;
    const Result fetch_result = grading_->getReport("quiz-1", "student-1", &fetched_report);
    ASSERT_TRUE(fetch_result.ok());
    EXPECT_DOUBLE_EQ(fetched_report.score, graded_report.score);
    EXPECT_EQ(fetched_report.itemised_breakdown.size(), graded_report.itemised_breakdown.size());
}

TEST_F(GradingModuleTest, UT_GRADE_05_RegradingAnAlreadySubmittedAttemptIsRejected) {
    makeQuiz(1.0, 0.0);
    makeOpenAttempt();

    GradeReport first;
    ASSERT_TRUE(grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &first).ok());

    // Simulate the student starting a second (graded-mode) attempt; grading the duplicate must fail.
    makeOpenAttempt();
    GradeReport second;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &second);
    EXPECT_EQ(result.code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
}

TEST_F(GradingModuleTest, UT_GRADE_06_UnknownQuizReturnsQuizNotFound) {
    makeOpenAttempt();
    GradeReport report;
    const Result result = grading_->gradeAttempt("missing-quiz", "student-1", sampleAnswers(), &report);
    EXPECT_EQ(result.code, ErrorCode::QUIZ_NOT_FOUND);
}

TEST_F(GradingModuleTest, UT_GRADE_07_NoOpenAttemptReturnsAttemptNotFound) {
    makeQuiz(1.0, 0.0);
    // No attempt seeded.
    GradeReport report;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &report);
    EXPECT_EQ(result.code, ErrorCode::ATTEMPT_NOT_FOUND);
}

TEST_F(GradingModuleTest, UT_GRADE_08_AutoSubmittedAttemptIsStillGradedButReportsTimeExpired) {
    makeQuiz(1.0, 0.0);
    makeOpenAttempt(/*auto_submitted=*/true);

    GradeReport report;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", sampleAnswers(), &report);

    EXPECT_EQ(result.code, ErrorCode::TIME_EXPIRED);
    EXPECT_TRUE(report.auto_submitted);
    EXPECT_DOUBLE_EQ(report.score, 1.0);  // grading itself still ran normally
    EXPECT_NE(attempts_.gradedAttempt("quiz-1", "student-1"), nullptr);
}

TEST_F(GradingModuleTest, UT_GRADE_09_BR5_TotalScoreNeverGoesBelowZeroWhenPenaltyExceedsMarks) {
    // negative_mark (5.0) exceeds marks_per_question (1.0): both wrong answers would raw-sum to -10.0.
    makeQuiz(/*marks_per_question=*/1.0, /*negative_mark=*/5.0);
    makeOpenAttempt();

    // Q1 wrong (correct is 1, selected 0), Q2 wrong (correct is 0, selected 1), Q3 blank.
    std::vector<Answer> answers = {
        Answer{"Q1", 0, false},
        Answer{"Q2", 1, false},
    };

    GradeReport report;
    const Result result = grading_->gradeAttempt("quiz-1", "student-1", answers, &report);

    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(report.score, 0.0);  // BR-5 floor, not -10.0
    // Itemised breakdown keeps the raw deduction for transparency (FR-017), unclamped.
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[0].marks_awarded, -5.0);
    EXPECT_DOUBLE_EQ(report.itemised_breakdown[1].marks_awarded, -5.0);

    const Attempt* stored = attempts_.gradedAttempt("quiz-1", "student-1");
    ASSERT_NE(stored, nullptr);
    EXPECT_DOUBLE_EQ(stored->score, 0.0);
}

}  // namespace