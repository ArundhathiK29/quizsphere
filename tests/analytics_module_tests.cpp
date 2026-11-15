#include "analytics_module.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "analytics_console.hpp"
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
    Result findReport(const std::string& quiz_id, const std::string& student_id, GradeReport* report) const override {
        const auto it = reports_.find(quiz_id + "|" + student_id);
        if (it == reports_.end()) {
            return {ErrorCode::ATTEMPT_NOT_FOUND, "No graded report."};
        }
        *report = it->second;
        return {ErrorCode::OK, "ok"};
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
    // For topicLeaderboard tests: a graded attempt plus the GradeReport (with topic_breakdown)
    // that findReport should return for it.
    void addGradedAttemptWithReport(const Attempt& attempt, const GradeReport& report) {
        by_quiz_[attempt.quiz_id].push_back(attempt);
        reports_[attempt.quiz_id + "|" + attempt.student_id] = report;
    }

private:
    std::unordered_map<std::string, GradeReport> reports_;
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

Quiz makeQuiz(const std::string& quiz_id, const std::string& title, std::vector<std::string> question_ids) {
    Quiz quiz;
    quiz.quiz_id = quiz_id;
    quiz.title = title;
    quiz.created_by = "instructor-1";
    quiz.created_at = "2026-10-24T00:00:00Z";
    quiz.time_limit_minutes = 10;
    quiz.scoring = ScoringScheme{1.0, 0.0};
    quiz.question_ids = std::move(question_ids);
    return quiz;
}

GradeReport makeReportWithOneTopic(double score, double max_score, const std::string& subject_tag, double marks) {
    GradeReport report;
    report.score = score;
    report.max_score = max_score;
    report.topic_breakdown.push_back(TopicScore{subject_tag, marks > 0.0 ? 1 : 0, 1, marks});
    return report;
}

class AnalyticsModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        bank_ = createQuestionBank(std::make_unique<InMemoryQuestionRepository>());
        ASSERT_TRUE(bank_->load().ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q1", "Math", 1)).ok());
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), makeQuestion("Q2", "Science", 0)).ok());

        quizzes_.saveQuiz(makeQuiz("quiz-1", "Sample Quiz", {"Q1", "Q2"}));

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

// BL-24 (extended scope): students ranked by total Math marks, summed across quiz-1 and a
// second quiz-2; ties share a rank; a practice attempt never counts (BR-4).
TEST_F(AnalyticsModuleTest, TopicLeaderboardSumsMarksAcrossQuizzesWithSharedTiesAndPracticeExcluded) {
    quizzes_.saveQuiz(makeQuiz("quiz-2", "Second Quiz", {"Q1"}));

    // alice: 2.0 Math marks on quiz-1 + 1.0 Math marks on quiz-2 = 3.0 total.
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(2.0, 2.0, "Math", 2.0));
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a2", "quiz-2", "alice", QuizMode::Graded, 1.0, {}, {"Q1"}),
        makeReportWithOneTopic(1.0, 1.0, "Math", 1.0));
    // bob: 3.0 Math marks on quiz-1 alone - ties alice's total of 3.0.
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a3", "quiz-1", "bob", QuizMode::Graded, 3.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(3.0, 3.0, "Math", 3.0));
    // carol: 1.0 Math marks - behind both.
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a4", "quiz-1", "carol", QuizMode::Graded, 1.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(1.0, 1.0, "Math", 1.0));
    // dave: a practice attempt with a huge Math score - must never appear (BR-4).
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a5", "quiz-1", "dave", QuizMode::Practice, 99.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(99.0, 99.0, "Math", 99.0));

    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->topicLeaderboard("Math", &entries);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(entries.size(), 3u);  // dave (practice) excluded

    for (const auto& e : entries) {
        EXPECT_NE(e.student_id, "dave");
        if (e.student_id == "alice" || e.student_id == "bob") {
            EXPECT_DOUBLE_EQ(e.score, 3.0);
            EXPECT_EQ(e.rank, 1);
        } else {
            EXPECT_EQ(e.student_id, "carol");
            EXPECT_DOUBLE_EQ(e.score, 1.0);
            EXPECT_EQ(e.rank, 3);
        }
    }
}

TEST_F(AnalyticsModuleTest, TopicLeaderboardForAnUnusedSubjectIsEmptyNotAnError) {
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(2.0, 2.0, "Math", 2.0));

    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->topicLeaderboard("History", &entries);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(entries.empty());
}

// BL-24 (extended scope): students ranked by total score summed across every graded attempt,
// across every quiz; ties share a rank; a practice attempt never counts (BR-4).
TEST_F(AnalyticsModuleTest, AllTimeLeaderboardSumsScoreAcrossQuizzesWithSharedTiesAndPracticeExcluded) {
    quizzes_.saveQuiz(makeQuiz("quiz-2", "Second Quiz", {"Q1"}));

    // alice: 2.0 on quiz-1 + 1.0 on quiz-2 = 3.0 total.
    attempts_.addGradedAttempt(makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a2", "quiz-2", "alice", QuizMode::Graded, 1.0, {}, {"Q1"}));
    // bob: 3.0 on quiz-1 alone - ties alice's total of 3.0.
    attempts_.addGradedAttempt(makeGradedAttempt("a3", "quiz-1", "bob", QuizMode::Graded, 3.0, {}, {"Q1", "Q2"}));
    // carol: 1.0 - behind both.
    attempts_.addGradedAttempt(makeGradedAttempt("a4", "quiz-1", "carol", QuizMode::Graded, 1.0, {}, {"Q1", "Q2"}));
    // dave: a practice attempt with a huge score - must never appear (BR-4).
    attempts_.addGradedAttempt(makeGradedAttempt("a5", "quiz-1", "dave", QuizMode::Practice, 99.0, {}, {"Q1", "Q2"}));

    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->allTimeLeaderboard(&entries);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(entries.size(), 3u);  // dave (practice) excluded

    for (const auto& e : entries) {
        EXPECT_NE(e.student_id, "dave");
        if (e.student_id == "alice" || e.student_id == "bob") {
            EXPECT_DOUBLE_EQ(e.score, 3.0);
            EXPECT_EQ(e.rank, 1);
        } else {
            EXPECT_EQ(e.student_id, "carol");
            EXPECT_DOUBLE_EQ(e.score, 1.0);
            EXPECT_EQ(e.rank, 3);
        }
    }
}

TEST_F(AnalyticsModuleTest, AllTimeLeaderboardWithNoGradedAttemptsIsEmptyNotAnError) {
    std::vector<LeaderboardEntry> entries;
    const Result result = analytics_->allTimeLeaderboard(&entries);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(entries.empty());
}

// BL-24: the rankings are reachable from the console (Instructor/Admin); students are refused.
TEST_F(AnalyticsModuleTest, AnalyticsConsoleRefusesStudents) {
    std::istringstream input("3\n0\n");
    std::ostringstream output;
    const Session student{"alice", Role::Student, "2026-10-24T00:00:00Z"};
    runAnalyticsMenu(student, *analytics_, quizzes_, input, output);
    EXPECT_NE(output.str().find("Only instructors and administrators"), std::string::npos);
    EXPECT_EQ(output.str().find("All-time leaderboard"), std::string::npos);
}

TEST_F(AnalyticsModuleTest, AnalyticsConsoleShowsQuizTopicAndAllTimeLeaderboards) {
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(2.0, 2.0, "Math", 2.0));
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 1.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(1.0, 2.0, "Math", 1.0));

    // 1 = quiz leaderboard (quiz-1), 2 = topic leaderboard (Math), 3 = all-time, 0 = back.
    std::istringstream input("1\nquiz-1\n2\nMath\n3\n0\n");
    std::ostringstream output;
    runAnalyticsMenu(instructorSession(), *analytics_, quizzes_, input, output);

    const std::string text = output.str();
    EXPECT_NE(text.find("Leaderboard: quiz-1"), std::string::npos);
    EXPECT_NE(text.find("Topic leaderboard: Math"), std::string::npos);
    EXPECT_NE(text.find("All-time leaderboard"), std::string::npos);
    EXPECT_NE(text.find("  1. alice  2.00"), std::string::npos);
    EXPECT_NE(text.find("  2. bob  1.00"), std::string::npos);
}

}  // namespace
