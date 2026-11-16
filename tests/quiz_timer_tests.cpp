#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <future>
#include <random>
#include <sstream>

#include "question_repository.hpp"
#include "quiz_console.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace quizsphere {
namespace {

using namespace std::chrono_literals;

struct TestClock {
    std::chrono::system_clock::time_point utc{1794484800s};
    std::chrono::steady_clock::time_point ticks{};
    QuizClock source() {
        return {[this] { return utc; }, [this] { return ticks; }};
    }
    void advance(std::chrono::seconds elapsed) { utc += elapsed; ticks += elapsed; }
};

class TimedQuizTest : public ::testing::Test {
protected:
    const Session student{"student", Role::Student, "2026-11-12T12:00:00Z"};
    const Session instructor{"teacher", Role::Instructor, "2026-11-12T12:00:00Z"};
    std::string path;
    TestClock clock;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grader;
    std::unique_ptr<QuizEngine> engine;

    void SetUp() override {
        path = (std::filesystem::temp_directory_path() /
                ("quizsphere-timer-" + std::to_string(std::random_device{}()) + ".db")).string();
        open();
        ASSERT_NE(engine, nullptr);
        for (const auto* id : {"q1", "q2"}) {
            ASSERT_TRUE(bank->addQuestion(instructor, Question{id, "Timers", Difficulty::Easy,
                QuestionType::MCQ, id, {"wrong", "right", "other"}, 1, true}).ok());
        }
        const Quiz quiz{"quiz", "Timed quiz", instructor.user_id, student.logged_in_at,
                        1, {4, 1}, {"q1", "q2"}};
        ASSERT_TRUE(dal->quizzes->saveQuiz(quiz).ok());
    }

    void TearDown() override {
        close();
        std::error_code ignored;
        for (const auto* suffix : {"", "-journal", "-wal", "-shm"})
            std::filesystem::remove(path + suffix, ignored);
    }

    void open() {
        std::string error;
        dal = createSqliteDataAccessLayer(path, &error);
        ASSERT_NE(dal, nullptr) << error;
        auto repository = createSqliteQuestionRepository(path, &error);
        ASSERT_NE(repository, nullptr) << error;
        bank = createQuestionBank(std::move(repository));
        ASSERT_TRUE(bank->load().ok());
        grader = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grader, 42, clock.source());
    }

    void close() {
        engine.reset();
        grader.reset();
        bank.reset();
        dal.reset();
    }

    void answerCorrectly(QuizAttemptSession& session, AttemptQuestionView* view) {
        ASSERT_TRUE(session.question(view).ok());
        const auto right = std::find(view->original_option_indices.begin(),
                                     view->original_option_indices.end(), 1);
        ASSERT_NE(right, view->original_option_indices.end());
        ASSERT_TRUE(session.select(static_cast<int>(right - view->original_option_indices.begin())).ok());
        ASSERT_TRUE(session.flag(true).ok());
    }

    void sql(const char* statement) {
        sqlite3* db = nullptr;
        const auto open_result = sqlite3_open(path.c_str(), &db);
        const auto result = open_result == SQLITE_OK
            ? sqlite3_exec(db, statement, nullptr, nullptr, nullptr) : open_result;
        sqlite3_close(db);
        ASSERT_EQ(result, SQLITE_OK) << statement;
    }
};

TEST_F(TimedQuizTest, UT_QE_06_DeadlineDecreasesSurvivesRestartAndGradesOriginalChoices) {
    QuizAttemptSession session(student, *engine, *bank);
    ASSERT_TRUE(session.start("quiz", QuizMode::Graded).ok());
    GradeReport report;
    EXPECT_EQ(session.submitExpired(&report).code, ErrorCode::VALIDATION_FAILED);
    const auto original = session.attempt();
    AttemptQuestionView first;
    answerCorrectly(session, &first);
    std::chrono::milliseconds remaining;
    ASSERT_TRUE(session.remainingTime(&remaining).ok());
    EXPECT_EQ(remaining, 60s);
    clock.advance(21s);
    ASSERT_TRUE(session.next().ok());
    ASSERT_TRUE(session.remainingTime(&remaining).ok());
    EXPECT_EQ(remaining, 39s);

    close();
    clock.ticks = {};
    open();
    QuizAttemptSession resumed(student, *engine, *bank);
    ASSERT_TRUE(resumed.start("quiz", QuizMode::Graded).ok());
    EXPECT_EQ(resumed.attempt().attempt_id, original.attempt_id);
    EXPECT_EQ(resumed.attempt().started_at, original.started_at);
    EXPECT_EQ(resumed.attempt().question_order, original.question_order);
    AttemptQuestionView restored;
    ASSERT_TRUE(resumed.question(&restored).ok());
    EXPECT_EQ(restored.question.options, first.question.options);
    EXPECT_TRUE(restored.flagged);
    EXPECT_EQ(restored.original_option_indices[static_cast<std::size_t>(restored.selected_display_option)], 1U);
    ASSERT_TRUE(resumed.remainingTime(&remaining).ok());
    EXPECT_EQ(remaining, 39s);
    clock.advance(39s);
    EXPECT_EQ(resumed.select(0).code, ErrorCode::TIME_EXPIRED);
    EXPECT_EQ(resumed.submitExpired(&report).code, ErrorCode::TIME_EXPIRED);
    EXPECT_TRUE(report.auto_submitted);
    EXPECT_EQ(report.score, 4);
    EXPECT_EQ(report.max_score, 8);
    ASSERT_EQ(report.itemised_breakdown.size(), 2U);
    EXPECT_TRUE(report.itemised_breakdown[0].correct);
    EXPECT_TRUE(report.itemised_breakdown[1].blank);
    EXPECT_EQ(resumed.attempt().answers.size(), 1U);
    EXPECT_EQ(resumed.attempt().attempt_id, original.attempt_id);

    close();
    open();
    ASSERT_TRUE(grader->getReport("quiz", student.user_id, &report).ok());
    EXPECT_EQ(report.score, 4);
    EXPECT_TRUE(report.auto_submitted);
    QuizAttemptSession repeat(student, *engine, *bank);
    EXPECT_EQ(repeat.start("quiz", QuizMode::Graded).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
}

TEST_F(TimedQuizTest, WallClockRollbackDoesNotResetRunningDeadline) {
    QuizAttemptSession session(student, *engine, *bank);
    ASSERT_TRUE(session.start("quiz", QuizMode::Graded).ok());
    clock.ticks += 45s;
    clock.utc -= 6s;
    ASSERT_TRUE(session.start("quiz", QuizMode::Graded).ok());
    std::chrono::milliseconds remaining;
    ASSERT_TRUE(session.remainingTime(&remaining).ok());
    EXPECT_EQ(remaining, 15s);
    clock.ticks += 15s;
    GradeReport report;
    EXPECT_EQ(session.submit(true, &report).code, ErrorCode::TIME_EXPIRED);
    EXPECT_TRUE(report.auto_submitted);
}

TEST_F(TimedQuizTest, ST_QE_02_ConsoleTimeoutAtQuestionOrSubmitConfirmation) {
    for (const bool confirmation : {false, true}) {
        const Session candidate{confirmation ? "confirmation" : "question", Role::Student, student.logged_in_at};
        std::istringstream input("quiz\ng\n");
        std::ostringstream output;
        int reads = 0;
        runQuizAttempts(candidate, *engine, *bank, input, output, [&](ConsoleDeadline, std::string* line) {
            if (confirmation && reads++ == 0) {
                *line = "s";
                return TimedRead::Line;
            }
            clock.advance(60s);
            return TimedRead::Timeout;
        });
        const auto text = output.str();
        EXPECT_NE(text.find("Time remaining: 60 seconds"), std::string::npos);
        EXPECT_NE(text.find("Time expired. Attempt auto-submitted."), std::string::npos);
        EXPECT_EQ(text.find("Submit this attempt?") != std::string::npos, confirmation);
        GradeReport report;
        ASSERT_TRUE(grader->getReport("quiz", candidate.user_id, &report).ok());
        EXPECT_TRUE(report.auto_submitted);
        EXPECT_EQ(report.score, 0);
    }
}

TEST_F(TimedQuizTest, ExpiredRestartFinalizesBeforeReadingAnAnswer) {
    Attempt attempt;
    ASSERT_TRUE(engine->startAttempt(student, "quiz", QuizMode::Graded, &attempt).ok());
    const auto id = attempt.attempt_id;
    close();
    clock.advance(61s);
    clock.ticks = {};
    open();
    int reads = 0;
    std::istringstream input("quiz\ng\n");
    std::ostringstream output;
    runQuizAttempts(student, *engine, *bank, input, output, [&](ConsoleDeadline, std::string*) {
        ++reads;
        return TimedRead::Closed;
    });
    EXPECT_EQ(reads, 0);
    EXPECT_NE(output.str().find("Attempt auto-submitted."), std::string::npos);
    ASSERT_TRUE(dal->attempts->findAttempt(id, &attempt).ok());
    EXPECT_TRUE(attempt.submitted);
    EXPECT_TRUE(attempt.auto_submitted);
    EXPECT_TRUE(attempt.answers.empty());
}

TEST_F(TimedQuizTest, REL_01_FailedTimeoutGradeLocksAnswersAndCanRetryAfterRestart) {
    QuizAttemptSession session(student, *engine, *bank);
    ASSERT_TRUE(session.start("quiz", QuizMode::Graded).ok());
    AttemptQuestionView first;
    answerCorrectly(session, &first);
    const auto attempt = session.attempt();
    clock.advance(60s);
    sql("CREATE TRIGGER fail_grade BEFORE INSERT ON topics BEGIN SELECT RAISE(ABORT,'injected grade error'); END");
    GradeReport report;
    EXPECT_EQ(session.submitExpired(&report).code, ErrorCode::IO_ERROR);
    EXPECT_EQ(grader->getReport("quiz", student.user_id, &report).code, ErrorCode::ATTEMPT_NOT_FOUND);
    Attempt stored;
    ASSERT_TRUE(dal->attempts->findAttempt(attempt.attempt_id, &stored).ok());
    EXPECT_FALSE(stored.submitted);
    EXPECT_TRUE(stored.auto_submitted);
    ASSERT_EQ(stored.answers.size(), 1U);
    EXPECT_EQ(stored.answers.front().selected_option, 1);
    EXPECT_EQ(dal->attempts->saveInProgressAttempt(attempt).code, ErrorCode::TIME_EXPIRED);

    sql("DROP TRIGGER fail_grade");
    close();
    open();
    QuizAttemptSession retry(student, *engine, *bank);
    ASSERT_TRUE(retry.start("quiz", QuizMode::Graded).ok());
    EXPECT_EQ(retry.submitExpired(&report).code, ErrorCode::TIME_EXPIRED);
    EXPECT_TRUE(report.auto_submitted);
    EXPECT_EQ(report.score, 4);
}

TEST_F(TimedQuizTest, ManualAndTimeoutOnSeparateConnectionsFinalizeOnce) {
    QuizAttemptSession manual(student, *engine, *bank);
    ASSERT_TRUE(manual.start("quiz", QuizMode::Graded).ok());
    AttemptQuestionView first;
    answerCorrectly(manual, &first);
    GradeReport report;
    EXPECT_EQ(manual.submit(false, &report).code, ErrorCode::CONFIRMATION_REQUIRED);

    std::string error;
    auto other = createSqliteDataAccessLayer(path, &error);
    ASSERT_NE(other, nullptr) << error;
    auto other_bank = createQuestionBank(createSqliteQuestionRepository(path, &error));
    ASSERT_TRUE(other_bank->load().ok());
    auto other_grader = createGradingModule(*other_bank, *other->quizzes, *other->attempts);
    auto timeout_clock = clock;
    timeout_clock.advance(60s);
    auto other_engine = createQuizEngine(*other_bank, *other->quizzes, *other->attempts, *other_grader, 42,
                                         timeout_clock.source());
    QuizAttemptSession expired(student, *other_engine, *other_bank);
    ASSERT_TRUE(expired.start("quiz", QuizMode::Graded).ok());
    std::promise<void> start;
    auto gate = start.get_future();
    GradeReport timeout_report;
    auto timeout = std::async(std::launch::async, [&] {
        gate.wait();
        return expired.submitExpired(&timeout_report);
    });
    start.set_value();
    const auto manual_result = manual.submit(true, &report);
    const auto timeout_result = timeout.get();
    const auto finalized = [](const Result& result, const GradeReport& grade) {
        return result.ok() || (result.code == ErrorCode::TIME_EXPIRED && grade.auto_submitted);
    };
    ASSERT_NE(finalized(manual_result, report), finalized(timeout_result, timeout_report))
        << manual_result.message << " / " << timeout_result.message;
    EXPECT_EQ(finalized(manual_result, report) ? timeout_result.code : manual_result.code,
              ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    const bool saved_auto = finalized(manual_result, report) ? report.auto_submitted : timeout_report.auto_submitted;
    ASSERT_TRUE(grader->getReport("quiz", student.user_id, &report).ok());
    EXPECT_EQ(report.auto_submitted, saved_auto);
    EXPECT_EQ(report.score, 4);
    EXPECT_EQ(grader->gradeAttempt("quiz", student.user_id, {}, &report).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
}

TEST_F(TimedQuizTest, ManualSubmitAndUntimedPracticeKeepTheirExistingRules) {
    QuizAttemptSession manual(student, *engine, *bank);
    ASSERT_TRUE(manual.start("quiz", QuizMode::Graded).ok());
    GradeReport report;
    ASSERT_TRUE(manual.submit(true, &report).ok());
    EXPECT_FALSE(report.auto_submitted);
    clock.advance(120s);
    EXPECT_EQ(manual.submitExpired(&report).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    QuizAttemptSession practice(student, *engine, *bank);
    ASSERT_TRUE(practice.start("quiz", QuizMode::Practice).ok());
    clock.advance(120s);
    EXPECT_EQ(practice.submitExpired(&report).code, ErrorCode::VALIDATION_FAILED);
    ASSERT_TRUE(practice.select(0).ok());
    ASSERT_TRUE(practice.submit(true, &report).ok());
    std::vector<Attempt> stored;
    ASSERT_TRUE(dal->attempts->listAttemptsForStudent(student.user_id, &stored).ok());
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_FALSE(stored.front().auto_submitted);
}

#ifndef _WIN32
TEST(TimedConsoleInputTest, IncompleteInputDoesNotBlockDeadlineAndNextLineCanBeRead) {
    ASSERT_TRUE(prepareTimedConsoleInput());
    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    const int original = dup(STDIN_FILENO);
    ASSERT_GE(original, 0);
    ASSERT_EQ(dup2(descriptors[0], STDIN_FILENO), STDIN_FILENO);
    close(descriptors[0]);

    const auto deadline = [] {
        return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    };
    EXPECT_EQ(write(descriptors[1], "partial", 7), 7);
    std::string line;
    const auto start = deadline();
    EXPECT_EQ(readConsoleLineUntil(start + 80ms, &line), TimedRead::Timeout);
    EXPECT_GE(deadline() - start, 80ms);
    EXPECT_EQ(write(descriptors[1], "next\n", 5), 5);
    EXPECT_EQ(readConsoleLineUntil(deadline() + 1s, &line), TimedRead::Line);
    EXPECT_EQ(line, "next");
    close(descriptors[1]);
    EXPECT_EQ(readConsoleLineUntil(deadline() + 1s, &line), TimedRead::Closed);
    EXPECT_EQ(dup2(original, STDIN_FILENO), STDIN_FILENO);
    close(original);
}
#endif

}  // namespace
}  // namespace quizsphere
