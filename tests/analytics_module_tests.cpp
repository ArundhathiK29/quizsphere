#include "analytics_module.hpp"

#include <memory>
#include <string>
#include <unordered_map>
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

// Same test doubles as grading_module_tests.cpp - BL-06 (the real SQLite DAL) isn't implemented yet.

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
    Result saveInProgressAttempt(const Attempt&) override { return {ErrorCode::OK, "ok"}; }
    Result findAttempt(const std::string&, Attempt*) const override {
        return {ErrorCode::ATTEMPT_NOT_FOUND, "not used"};
    }
    Result findOpenAttempt(const std::string&, const std::string&, Attempt*) const override {
        return {ErrorCode::ATTEMPT_NOT_FOUND, "not used"};
    }
    Result saveGradedAttempt(const Attempt&, const GradeReport&) override { return {ErrorCode::OK, "ok"}; }
    Result findReport(const std::string&, const std::string&, GradeReport*) const override {
        return {ErrorCode::ATTEMPT_NOT_FOUND, "not used"};
    }
    Result listGradedAttempts(const std::string& quiz_id, std::vector<Attempt>* attempts) const override {
        attempts->clear();
        const auto it = by_quiz_.find(quiz_id);
        if (it != by_quiz_.end()) {
            *attempts = it->second;
        }
        return {ErrorCode::OK, "ok"};
    }
    Result listAttemptsForStudent(const std::string&, std::vector<Attempt>*) const override {
        return {ErrorCode::OK, "ok"};
    }

    void addGradedAttempt(const Attempt& attempt) { by_quiz_[attempt.quiz_id].push_back(attempt); }

private:
    std::unordered_map<std::string, std::vector<Attempt>> by_quiz_;
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

Attempt makeGradedAttempt(const std::string& attempt_id, const std::string& quiz_id, const std::string& student_id,
                          QuizMode mode, double score, std::vector<Answer> answers,
                          std::vector<std::string> question_order) {
    Attempt attempt;
    attempt.attempt_id = attempt_id;
    attempt.quiz_id = quiz_id;
    attempt.student_id = student_id;
    attempt.mode = mode;
    attempt.started_at = "2026-10-24T00:00:00Z";
    attempt.submitted_at = "2026-10-24T00:10:00Z";
    attempt.question_order = std::move(question_order);
    attempt.answers = std::move(answers);
    attempt.submitted = true;
    attempt.score = score;
    return attempt;
}

class AnalyticsModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        bank_ = createQuestionBank(std::make_unique<InMemoryQuestionRepository>());
        ASSERT_TRUE(bank_->load().ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q1", "Math", 1)).ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q2", "Science", 0)).ok());

        Quiz quiz;
        quiz.quiz_id = "quiz-1";
        quiz.title = "Sample Quiz";
        quiz.created_by = "instructor-1";
        quiz.created_at = "2026-10-24T00:00:00Z";
        quiz.time_limit_minutes = 10;
        quiz.scoring = ScoringScheme{1.0, 0.0};
        quiz.question_ids = {"Q1", "Q2"};
        quizzes_.saveQuiz(quiz);

        analytics_ = createAnalyticsModule(*bank_, quizzes_, attempts_);
    }

    std::unique_ptr<QuestionBank> bank_;
    FakeQuizRepository quizzes_;
    FakeAttemptRepository attempts_;
    std::unique_ptr<AnalyticsModule> analytics_;
};

// UT-ANLY-03/04 (BL-24): ranked graded attempts of one quiz; ties share a rank; practice excluded.
TEST_F(AnalyticsModuleTest, LeaderboardRanksGradedAttemptsDescendingWithSharedTiesAndSkippedRanks) {
    attempts_.addGradedAttempt(makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a3", "quiz-1", "carol", QuizMode::Graded, 1.0, {}, {"Q1", "Q2"}));
    // Practice attempt with the highest score of all - must never appear (BR-4).
    attempts_.addGradedAttempt(makeGradedAttempt("a4", "quiz-1", "dave", QuizMode::Practice, 99.0, {}, {"Q1", "Q2"}));

    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->leaderboard("quiz-1", &entries);

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(entries.size(), 3u);  // dave (practice) excluded

    // alice and bob tie for rank 1 (order between them is unspecified); carol is rank 3, not 2.
    EXPECT_EQ(entries[0].rank, 1);
    EXPECT_EQ(entries[1].rank, 1);
    EXPECT_DOUBLE_EQ(entries[0].score, 2.0);
    EXPECT_DOUBLE_EQ(entries[1].score, 2.0);
    EXPECT_EQ(entries[2].student_id, "carol");
    EXPECT_EQ(entries[2].rank, 3);
    EXPECT_DOUBLE_EQ(entries[2].score, 1.0);

    for (const auto& e : entries) {
        EXPECT_NE(e.student_id, "dave");
    }
}

TEST_F(AnalyticsModuleTest, LeaderboardForUnknownQuizReturnsQuizNotFound) {
    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->leaderboard("missing-quiz", &entries);
    EXPECT_EQ(result.code, ErrorCode::QUIZ_NOT_FOUND);
}

TEST_F(AnalyticsModuleTest, LeaderboardWithNoGradedAttemptsIsEmptyNotAnError) {
    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->leaderboard("quiz-1", &entries);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(entries.empty());
}

// UT-ANLY-01 (BL-26): percent of attempts that answered a question correctly, with known data.
TEST_F(AnalyticsModuleTest, DifficultyIndexComputesPercentCorrectFromKnownData) {
    // Q1's correct option is 1. Three attempts see it: two correct, one wrong.
    attempts_.addGradedAttempt(makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 1.0,
                                                  {Answer{"Q1", 1, false}}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 0.0,
                                                  {Answer{"Q1", 0, false}}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a3", "quiz-1", "carol", QuizMode::Practice, 1.0,
                                                  {Answer{"Q1", 1, false}}, {"Q1", "Q2"}));

    double percent = -1.0;
    const Result result = analytics_->difficultyIndex("Q1", &percent);

    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(percent, 100.0 * 2.0 / 3.0);  // practice attempts DO count for item analysis
}

TEST_F(AnalyticsModuleTest, DifficultyIndexTreatsABlankAnswerAsNotCorrect) {
    // Q1 appears in the attempt's question_order but has no Answer entry at all (skipped).
    attempts_.addGradedAttempt(makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 0.0, {}, {"Q1", "Q2"}));

    double percent = -1.0;
    const Result result = analytics_->difficultyIndex("Q1", &percent);

    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(percent, 0.0);
}

TEST_F(AnalyticsModuleTest, DifficultyIndexForAnUnattemptedQuestionIsZeroNotAnError) {
    double percent = -1.0;
    const Result result = analytics_->difficultyIndex("Q2", &percent);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(percent, 0.0);
}

TEST_F(AnalyticsModuleTest, DifficultyIndexForAnUnknownQuestionReturnsQuestionNotFound) {
    double percent = -1.0;
    const Result result = analytics_->difficultyIndex("missing-question", &percent);
    EXPECT_EQ(result.code, ErrorCode::QUESTION_NOT_FOUND);
}

}  // namespace