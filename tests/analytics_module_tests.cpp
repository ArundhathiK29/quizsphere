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
    Result findReportForAttempt(const std::string& attempt_id, GradeReport* report) const override {
        const auto it = reports_by_attempt_.find(attempt_id);
        if (it == reports_by_attempt_.end()) {
            return {ErrorCode::ATTEMPT_NOT_FOUND, "No report for attempt."};
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
        reports_by_attempt_[attempt.attempt_id] = report;
    }

private:
    std::unordered_map<std::string, GradeReport> reports_;
    std::unordered_map<std::string, GradeReport> reports_by_attempt_;
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

GradeReport makeReportWithTopics(double score, double max_score, std::vector<TopicScore> topics) {
    GradeReport report;
    report.score = score;
    report.max_score = max_score;
    report.topic_breakdown = std::move(topics);
    return report;
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

TEST_F(AnalyticsModuleTest, StudentRankingsHandlesEmptyResultsInvalidInputAndBack) {
    const Session student{"alice", Role::Student, "2026-11-15T00:00:00Z"};
    std::istringstream input("invalid\n1\nmissing-quiz\n2\nMath\n3\n0\n");
    std::ostringstream output;
    runStudentRankings(student, *analytics_, quizzes_, input, output);
    const std::string text = output.str();
    EXPECT_NE(text.find("Invalid option."), std::string::npos);
    EXPECT_NE(text.find("Could not compute leaderboard"), std::string::npos);
    EXPECT_NE(text.find("Topic leaderboard: Math"), std::string::npos);
    EXPECT_NE(text.find("All-time leaderboard"), std::string::npos);
    EXPECT_NE(text.find("No graded attempts yet."), std::string::npos);
    EXPECT_EQ(text.find("Only instructors and administrators"), std::string::npos);
    EXPECT_EQ(input.peek(), std::char_traits<char>::eof());
}

TEST_F(AnalyticsModuleTest, StudentRankingsRejectsOtherRolesAndExitsOnEndOfInput) {
    std::istringstream input("3\n0\n");
    std::ostringstream output;
    runStudentRankings(instructorSession(), *analytics_, quizzes_, input, output);
    EXPECT_NE(output.str().find("requires a Student session"), std::string::npos);
    EXPECT_EQ(output.str().find("All-time leaderboard"), std::string::npos);
    EXPECT_EQ(input.peek(), '3');

    const Session student{"alice", Role::Student, "2026-11-15T00:00:00Z"};
    std::istringstream empty_input;
    std::ostringstream empty_output;
    runStudentRankings(student, *analytics_, quizzes_, empty_input, empty_output);
    EXPECT_NE(empty_output.str().find("=== Rankings ==="), std::string::npos);
}

TEST_F(AnalyticsModuleTest, AnalyticsConsoleShowsQuizTopicAndAllTimeLeaderboards) {
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(2.0, 2.0, "Math", 2.0));
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 1.0, {}, {"Q1", "Q2"}),
        makeReportWithOneTopic(1.0, 2.0, "Math", 1.0));

    // Analytics 1 = rankings; then 1 = quiz leaderboard (quiz-1), 2 = topic leaderboard (Math),
    // 3 = all-time, 0 = back to analytics, 0 = back.
    std::istringstream input("1\n1\nquiz-1\n2\nMath\n3\n0\n0\n");
    std::ostringstream output;
    runAnalyticsMenu(instructorSession(), *analytics_, quizzes_, input, output);

    const std::string text = output.str();
    EXPECT_NE(text.find("Leaderboard: quiz-1"), std::string::npos);
    EXPECT_NE(text.find("Topic leaderboard: Math"), std::string::npos);
    EXPECT_NE(text.find("All-time leaderboard"), std::string::npos);
    EXPECT_NE(text.find("  1. alice  2.00"), std::string::npos);
    EXPECT_NE(text.find("  2. bob  1.00"), std::string::npos);
}

// UT-ANLY-01 (BL-26): percent of GRADED attempts that answered a question correctly, with known
// data. Q1's correct option is 1. One correct + one wrong graded attempt = 50%; a correct PRACTICE
// attempt must leave it at 50% (BR-4: practice never feeds analytics).
TEST_F(AnalyticsModuleTest, DifficultyIndexComputesPercentCorrectFromGradedAttemptsOnly) {
    attempts_.addGradedAttempt(makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 1.0,
                                                  {Answer{"Q1", 1, false}}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 0.0,
                                                  {Answer{"Q1", 0, false}}, {"Q1", "Q2"}));

    double percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);

    attempts_.addGradedAttempt(makeGradedAttempt("a3", "quiz-1", "carol", QuizMode::Practice, 1.0,
                                                  {Answer{"Q1", 1, false}}, {"Q1", "Q2"}));

    percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);  // unchanged: the practice attempt does not count
}

TEST_F(AnalyticsModuleTest, DifficultyIndexTreatsABlankAnswerAsIncorrect) {
    // Q1 appears in the attempt's question_order but has no Answer entry at all (skipped).
    attempts_.addGradedAttempt(makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 0.0, {}, {"Q1", "Q2"}));
    attempts_.addGradedAttempt(makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 1.0,
                                                  {Answer{"Q1", 1, false}}, {"Q1", "Q2"}));

    double percent = -1.0;
    ASSERT_TRUE(analytics_->difficultyIndex("Q1", &percent).ok());
    EXPECT_DOUBLE_EQ(percent, 50.0);  // blank counts in the denominator, not the numerator
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

// ---- BL-41 / FR-020: topic breakdown and topic analytics ----

// Fixture: quiz-1 has Math (2 q) and Science (1 q). alice: Math 2/2, Science 0/1. bob: Math 1/2,
// Science 1/1. A practice report for carol (Math 0/2, Science 0/1) must not move the class average.
void seedTopicData(FakeAttemptRepository* attempts) {
    attempts->addGradedAttemptWithReport(
        makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}),
        makeReportWithTopics(2.0, 3.0, {TopicScore{"Math", 2, 2, 2.0}, TopicScore{"Science", 0, 1, 0.0}}));
    attempts->addGradedAttemptWithReport(
        makeGradedAttempt("a2", "quiz-1", "bob", QuizMode::Graded, 2.0, {}, {"Q1", "Q2"}),
        makeReportWithTopics(2.0, 3.0, {TopicScore{"Math", 1, 2, 1.0}, TopicScore{"Science", 1, 1, 1.0}}));
    attempts->addGradedAttemptWithReport(
        makeGradedAttempt("a3", "quiz-1", "carol", QuizMode::Practice, 0.0, {}, {"Q1", "Q2"}),
        makeReportWithTopics(0.0, 3.0, {TopicScore{"Math", 0, 2, 0.0}, TopicScore{"Science", 0, 1, 0.0}}));
}

TEST_F(AnalyticsModuleTest, TopicBreakdownReturnsTheStudentsOwnGradedTopicsOrAttemptNotFound) {
    seedTopicData(&attempts_);

    std::vector<TopicScore> topics;
    ASSERT_TRUE(analytics_->topicBreakdown("quiz-1", "alice", &topics).ok());
    ASSERT_EQ(topics.size(), 2u);
    EXPECT_EQ(topics[0].subject_tag, "Math");
    EXPECT_EQ(topics[0].correct, 2);
    EXPECT_EQ(topics[1].subject_tag, "Science");
    EXPECT_EQ(topics[1].correct, 0);

    // carol only has a practice report: no graded breakdown exists for her (BR-4).
    EXPECT_EQ(analytics_->topicBreakdown("quiz-1", "carol", &topics).code, ErrorCode::ATTEMPT_NOT_FOUND);
    EXPECT_EQ(analytics_->topicBreakdown("quiz-1", "nobody", &topics).code, ErrorCode::ATTEMPT_NOT_FOUND);
    EXPECT_EQ(analytics_->topicBreakdown("missing-quiz", "alice", &topics).code, ErrorCode::QUIZ_NOT_FOUND);
}

TEST_F(AnalyticsModuleTest, TopicAnalyticsGivesStudentVsClassAverageWeakestFirstExcludingPractice) {
    seedTopicData(&attempts_);

    std::vector<TopicAnalytics> topics;
    const Session alice{"alice", Role::Student, "2026-11-16T00:00:00Z"};
    ASSERT_TRUE(analytics_->topicAnalytics(alice, "quiz-1", "alice", &topics).ok());
    ASSERT_EQ(topics.size(), 2u);

    // Weakest first: Science 0% then Math 100%.
    EXPECT_EQ(topics[0].subject_tag, "Science");
    EXPECT_DOUBLE_EQ(topics[0].student_percent, 0.0);
    EXPECT_TRUE(topics[0].weakest);
    // Class average over the two GRADED attempts only: alice 0% + bob 100% = 50%; carol's
    // practice 0% would have dragged it to 33.3%.
    EXPECT_DOUBLE_EQ(topics[0].class_average_percent, 50.0);
    EXPECT_EQ(topics[0].class_attempts, 2);

    EXPECT_EQ(topics[1].subject_tag, "Math");
    EXPECT_DOUBLE_EQ(topics[1].student_percent, 100.0);
    EXPECT_FALSE(topics[1].weakest);
    EXPECT_DOUBLE_EQ(topics[1].class_average_percent, 75.0);  // alice 100 + bob 50
    EXPECT_EQ(topics[1].class_attempts, 2);
    EXPECT_EQ(topics[1].student_correct, 2);
    EXPECT_EQ(topics[1].student_total, 2);
    EXPECT_DOUBLE_EQ(topics[1].student_marks, 2.0);
}

TEST_F(AnalyticsModuleTest, TopicAnalyticsFlagsEveryTiedWeakestTopic) {
    attempts_.addGradedAttemptWithReport(
        makeGradedAttempt("a1", "quiz-1", "alice", QuizMode::Graded, 0.0, {}, {"Q1", "Q2"}),
        makeReportWithTopics(0.0, 2.0, {TopicScore{"Math", 0, 1, 0.0}, TopicScore{"Science", 0, 1, 0.0}}));

    std::vector<TopicAnalytics> topics;
    ASSERT_TRUE(analytics_->topicAnalytics(instructorSession(), "quiz-1", "alice", &topics).ok());
    ASSERT_EQ(topics.size(), 2u);
    EXPECT_TRUE(topics[0].weakest);
    EXPECT_TRUE(topics[1].weakest);
}

TEST_F(AnalyticsModuleTest, TopicAnalyticsEnforcesStudentOwnOnlyAndInstructorAdminAnyone) {
    seedTopicData(&attempts_);
    std::vector<TopicAnalytics> topics;

    const Session alice{"alice", Role::Student, "2026-11-16T00:00:00Z"};
    EXPECT_TRUE(analytics_->topicAnalytics(alice, "quiz-1", "alice", &topics).ok());
    EXPECT_EQ(analytics_->topicAnalytics(alice, "quiz-1", "bob", &topics).code, ErrorCode::NOT_AUTHORIZED);

    EXPECT_TRUE(analytics_->topicAnalytics(instructorSession(), "quiz-1", "bob", &topics).ok());
    const Session admin{"root", Role::Admin, "2026-11-16T00:00:00Z"};
    EXPECT_TRUE(analytics_->topicAnalytics(admin, "quiz-1", "bob", &topics).ok());

    // A student with only a practice attempt has no analytics to show.
    EXPECT_EQ(analytics_->topicAnalytics(instructorSession(), "quiz-1", "carol", &topics).code,
              ErrorCode::ATTEMPT_NOT_FOUND);
    EXPECT_EQ(analytics_->topicAnalytics(alice, "quiz-1", "alice", nullptr).code, ErrorCode::VALIDATION_FAILED);
}

TEST_F(AnalyticsModuleTest, AnalyticsConsoleShowsTopicAnalyticsForAStudentToInstructors) {
    seedTopicData(&attempts_);

    // Analytics 2 = topic analytics; quiz-1; alice; then 0 = back.
    std::istringstream input("2\nquiz-1\nalice\n0\n");
    std::ostringstream output;
    runAnalyticsMenu(instructorSession(), *analytics_, quizzes_, input, output);

    const std::string text = output.str();
    EXPECT_NE(text.find("Topic analytics: alice on quiz-1"), std::string::npos);
    EXPECT_NE(text.find("Science: 0/1 correct (0.0%), class average 50.0%  <- weakest"), std::string::npos);
    EXPECT_NE(text.find("Math: 2/2 correct (100.0%), class average 75.0%"), std::string::npos);
    EXPECT_NE(text.find("Weakest topic: Science"), std::string::npos);

    // A student id with no graded result is reported, not crashed on.
    std::istringstream missing("2\nquiz-1\ncarol\n0\n");
    std::ostringstream missing_output;
    runAnalyticsMenu(instructorSession(), *analytics_, quizzes_, missing, missing_output);
    EXPECT_NE(missing_output.str().find("Could not compute topic analytics"), std::string::npos);
}

}  // namespace
