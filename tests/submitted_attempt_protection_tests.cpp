// BL-43 (#43): a submitted graded attempt and its stored result are immutable through every
// module API, and that holds after the database is closed and reopened (restart).
//
// This goes through the REAL flow - QuizAttemptSession -> QuizEngine -> GradingModule -> SQLite
// DAL - rather than hand-built Attempt rows, then tries every write route a caller could use:
//   - QuizEngine::saveAnswer / submitAttempt on the submitted attempt (and stale copies of it)
//   - QuizAttemptSession::select / flag / submit after submission
//   - GradingModule::gradeAttempt again (re-grade)
//   - AttemptRepository::saveInProgressAttempt / saveGradedAttempt with changed data
//   - QuizEngine::startAttempt for a second graded attempt on the same quiz
//   - raw SQL UPDATE/DELETE on attempts / answers / reports (schema triggers)
// After every one of those, the stored attempt and GradeReport must be byte-for-byte what grading
// wrote. The DAL-level unit tests in data_access_layer_tests.cpp cover the storage layer in
// isolation; this file proves the guarantee end to end.
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sqlite3.h>

#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"
#include "quiz_attempt.hpp"
#include "quiz_engine.hpp"
#include "quiz_types.hpp"

namespace quizsphere {
namespace {

class SubmittedAttemptProtectionTest : public ::testing::Test {
protected:
    std::string path;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grading;
    std::unique_ptr<QuizEngine> engine;
    const Session instructor{"1", Role::Instructor, "2026-11-16T00:00:00Z"};
    const Session student{"7", Role::Student, "2026-11-16T00:00:00Z"};
    Quiz quiz;

    void open() {
        std::string error;
        dal = createSqliteDataAccessLayer(path, &error);
        ASSERT_NE(dal, nullptr) << error;
        auto repo = createSqliteQuestionRepository(path, &error);
        ASSERT_NE(repo, nullptr) << error;
        bank = createQuestionBank(std::move(repo));
        ASSERT_TRUE(bank->load().ok());
        grading = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grading, 42);
    }
    void close() {
        engine.reset();
        grading.reset();
        bank.reset();
        dal.reset();
    }
    void restart() {
        close();
        open();
    }

    void SetUp() override {
        path = (std::filesystem::temp_directory_path() /
                ("quizsphere-bl43-" + std::to_string(std::random_device{}()) + ".sqlite"))
                   .string();
        open();
        for (int i = 0; i < 2; ++i) {
            Question q{"q" + std::to_string(i), "Math", Difficulty::Easy, QuestionType::MCQ,
                       "Choose the first option", {"A", "B", "C"}, 0, true};
            ASSERT_TRUE(bank->addQuestion(instructor, q).ok());
        }
        QuizConfig config{"Protected Quiz", 15, 2, "Math", Difficulty::Easy, {4, 1}};
        ASSERT_TRUE(engine->createQuiz(instructor, config, &quiz).ok());
    }
    void TearDown() override {
        close();
        std::error_code ignored;
        for (const std::string suffix : {"", "-journal", "-wal", "-shm"}) {
            std::filesystem::remove(path + suffix, ignored);
        }
    }

    bool executeSql(const std::string& sql) {
        sqlite3* db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return false;
        }
        const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return rc == SQLITE_OK;
    }

    // Takes the quiz through the real student flow: answer q at position 0 correctly, leave the
    // other blank, submit with confirmation. Returns the submitted attempt as stored.
    Attempt submitThroughRealFlow(GradeReport* report) {
        QuizAttemptSession session(student, *engine, *bank);
        EXPECT_TRUE(session.start(quiz.quiz_id, QuizMode::Graded).ok());
        AttemptQuestionView view;
        EXPECT_TRUE(session.question(&view).ok());
        // Pick the displayed option whose text is "A" (the correct one in ORIGINAL order).
        int displayed = -1;
        for (std::size_t i = 0; i < view.question.options.size(); ++i) {
            if (view.question.options[i] == "A") displayed = static_cast<int>(i);
        }
        EXPECT_GE(displayed, 0);
        EXPECT_TRUE(session.select(displayed).ok());
        EXPECT_TRUE(session.submit(true, report).ok());
        Attempt stored;
        EXPECT_TRUE(dal->attempts->findAttempt(session.attempt().attempt_id, &stored).ok());
        return stored;
    }

    void expectUnchanged(const Attempt& original, const GradeReport& original_report) {
        Attempt now;
        ASSERT_TRUE(dal->attempts->findAttempt(original.attempt_id, &now).ok());
        EXPECT_TRUE(now.submitted);
        EXPECT_EQ(now.submitted_at, original.submitted_at);
        EXPECT_EQ(now.question_order, original.question_order);
        ASSERT_EQ(now.answers.size(), original.answers.size());
        for (std::size_t i = 0; i < now.answers.size(); ++i) {
            EXPECT_EQ(now.answers[i].question_id, original.answers[i].question_id);
            EXPECT_EQ(now.answers[i].selected_option, original.answers[i].selected_option);
        }
        EXPECT_DOUBLE_EQ(now.score, original.score);

        GradeReport report;
        ASSERT_TRUE(dal->attempts->findReport(quiz.quiz_id, student.user_id, &report).ok());
        EXPECT_DOUBLE_EQ(report.score, original_report.score);
        EXPECT_DOUBLE_EQ(report.max_score, original_report.max_score);
        ASSERT_EQ(report.itemised_breakdown.size(), original_report.itemised_breakdown.size());
        for (std::size_t i = 0; i < report.itemised_breakdown.size(); ++i) {
            EXPECT_DOUBLE_EQ(report.itemised_breakdown[i].marks_awarded,
                             original_report.itemised_breakdown[i].marks_awarded);
        }
    }

    // The engine refuses a submitted attempt either as ATTEMPT_ALREADY_SUBMITTED (it still holds the
    // attempt) or as VALIDATION_FAILED "start or resume first" (it has already dropped it after
    // submit - and resuming is itself refused, see route 7). Both are a hard refusal; nothing else is.
    static void expectEngineRefusal(const Result& result) {
        EXPECT_FALSE(result.ok());
        EXPECT_TRUE(result.code == ErrorCode::ATTEMPT_ALREADY_SUBMITTED || result.code == ErrorCode::VALIDATION_FAILED)
            << result.message;
    }

    // Every API route that could mutate the submitted attempt. Called before AND after restart.
    void everyMutationRouteIsRefused(const Attempt& original, const GradeReport& original_report) {
        // 1. QuizEngine::saveAnswer on the stored submitted attempt.
        Attempt copy = original;
        Answer changed{original.answers[0].question_id, 2, false};
        expectEngineRefusal(engine->saveAnswer(&copy, changed));

        // 2. QuizEngine::saveAnswer on a STALE copy that claims to still be open.
        Attempt stale = original;
        stale.submitted = false;
        stale.submitted_at.clear();
        expectEngineRefusal(engine->saveAnswer(&stale, changed));

        // 3. QuizEngine::submitAttempt again (double submission), both on the real and stale copy.
        GradeReport again;
        copy = original;
        expectEngineRefusal(engine->submitAttempt(student, &copy, false, &again));
        stale = original;
        stale.submitted = false;
        stale.submitted_at.clear();
        stale.answers[0].selected_option = 2;  // tampered answer
        expectEngineRefusal(engine->submitAttempt(student, &stale, false, &again));

        // 4. GradingModule::gradeAttempt again with different answers (re-grade).
        std::vector<Answer> tampered = {Answer{original.answers[0].question_id, 2, false}};
        EXPECT_EQ(grading->gradeAttempt(quiz.quiz_id, student.user_id, tampered, &again).code,
                  ErrorCode::ATTEMPT_ALREADY_SUBMITTED);

        // 5. AttemptRepository::saveInProgressAttempt with the attempt "reopened" and answers cleared.
        stale = original;
        stale.submitted = false;
        stale.submitted_at.clear();
        stale.answers.clear();
        EXPECT_EQ(dal->attempts->saveInProgressAttempt(stale).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);

        // 6. AttemptRepository::saveGradedAttempt again with a better score.
        Attempt better = original;
        better.score = 99;
        GradeReport better_report = original_report;
        better_report.score = 99;
        EXPECT_EQ(dal->attempts->saveGradedAttempt(better, better_report).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);

        // 7. A second graded attempt on the same quiz (which could otherwise shadow the first).
        Attempt second;
        EXPECT_EQ(engine->startAttempt(student, quiz.quiz_id, QuizMode::Graded, &second).code,
                  ErrorCode::ATTEMPT_ALREADY_SUBMITTED);

        // 8. Raw SQL bypassing every module: schema triggers must refuse update and delete.
        const std::string id = original.attempt_id;
        EXPECT_FALSE(executeSql("UPDATE attempts SET score=99 WHERE id='" + id + "'"));
        EXPECT_FALSE(executeSql("UPDATE attempts SET submitted=0 WHERE id='" + id + "'"));
        EXPECT_FALSE(executeSql("UPDATE answers SET selected=2 WHERE attempt='" + id + "'"));
        EXPECT_FALSE(executeSql("DELETE FROM answers WHERE attempt='" + id + "'"));
        EXPECT_FALSE(executeSql("DELETE FROM attempts WHERE id='" + id + "'"));
        EXPECT_FALSE(executeSql("DELETE FROM reports WHERE attempt='" + id + "'"));
        EXPECT_FALSE(executeSql("UPDATE reports SET score=99 WHERE attempt='" + id + "'"));

        expectUnchanged(original, original_report);
    }
};

TEST_F(SubmittedAttemptProtectionTest, SubmittedGradedAttemptCannotBeChangedOrDeletedThroughAnyApi) {
    GradeReport report;
    const Attempt original = submitThroughRealFlow(&report);
    ASSERT_TRUE(original.submitted);
    ASSERT_EQ(original.answers.size(), 1U);               // the blank question has no stored answer row
    ASSERT_EQ(report.itemised_breakdown.size(), 2U);      // but it IS graded (as 0)
    EXPECT_DOUBLE_EQ(report.score, 4.0);                  // one correct (+4), one blank (0)

    everyMutationRouteIsRefused(original, report);
}

TEST_F(SubmittedAttemptProtectionTest, ProtectionHoldsAfterRestart) {
    GradeReport report;
    const Attempt original = submitThroughRealFlow(&report);

    restart();
    ASSERT_NE(engine, nullptr);
    expectUnchanged(original, report);
    everyMutationRouteIsRefused(original, report);

    // And a second restart after the refused writes: nothing leaked through a half-applied write.
    restart();
    ASSERT_NE(engine, nullptr);
    expectUnchanged(original, report);
}

TEST_F(SubmittedAttemptProtectionTest, QuizAttemptSessionRefusesEveryActionAfterSubmit) {
    QuizAttemptSession session(student, *engine, *bank);
    ASSERT_TRUE(session.start(quiz.quiz_id, QuizMode::Graded).ok());
    ASSERT_TRUE(session.select(0).ok());
    GradeReport report;
    ASSERT_TRUE(session.submit(true, &report).ok());

    EXPECT_EQ(session.select(1).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    EXPECT_EQ(session.select(kBlankAnswer).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    EXPECT_EQ(session.flag(true).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    EXPECT_EQ(session.next().code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    EXPECT_EQ(session.jump(1).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    GradeReport again;
    EXPECT_EQ(session.submit(true, &again).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);

    Attempt stored;
    ASSERT_TRUE(dal->attempts->findAttempt(session.attempt().attempt_id, &stored).ok());
    EXPECT_TRUE(stored.submitted);
    EXPECT_DOUBLE_EQ(stored.score, report.score);
}

}  // namespace
}  // namespace quizsphere
