
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdio>
#include <string>
#include <vector>

#include "data_access_layer.hpp"

namespace quizsphere {
namespace {

class DataAccessLayerTest : public ::testing::Test {
protected:
    std::string db_path = "dal_test_database.sqlite";
    std::unique_ptr<DataAccessLayer> dal;

    void SetUp() override {
        std::remove(db_path.c_str());
        std::remove((db_path + "-journal").c_str());
        std::remove((db_path + "-wal").c_str());
        std::remove((db_path + "-shm").c_str());

        std::string error;
        dal = createSqliteDataAccessLayer(db_path, &error);

        ASSERT_NE(dal, nullptr) << error;
        ASSERT_NE(dal->quizzes, nullptr);
        ASSERT_NE(dal->attempts, nullptr);
    }

    void TearDown() override {
        dal.reset();
        std::remove(db_path.c_str());
        std::remove((db_path + "-journal").c_str());
        std::remove((db_path + "-wal").c_str());
        std::remove((db_path + "-shm").c_str());
    }

    Quiz makeQuiz() {
        Quiz quiz;
        quiz.quiz_id = "quiz-1";
        quiz.title = "Digital Design";
        quiz.created_by = "instructor-1";
        quiz.created_at = "2026-10-26T10:00:00Z";
        quiz.time_limit_minutes = 30;
        quiz.scoring = {2.0, 0.5};
        quiz.question_ids = {"q1", "q2", "q3"};
        return quiz;
    }

    Attempt makeAttempt() {
        Attempt attempt;
        attempt.attempt_id = "attempt-1";
        attempt.quiz_id = "quiz-1";
        attempt.student_id = "student-1";
        attempt.mode = QuizMode::Graded;
        attempt.started_at = "2026-10-26T10:01:00Z";
        attempt.question_order = {"q2", "q1"};
        attempt.answers = {
            {"q2", 1, true},
            {"q1", kBlankAnswer, false}
        };
        return attempt;
    }

    GradeReport makeReport() {
        GradeReport report;
        report.score = 2.0;
        report.max_score = 6.0;
        report.itemised_breakdown = {
            {"q2", 1, 1, true, false, 2.0},
            {"q1", kBlankAnswer, 0, false, true, 0.0}
        };
        report.topic_breakdown = {
            {"Digital Logic", 1, 2, 2.0}
        };
        return report;
    }

    bool executeSql(const char* sql) {
        sqlite3* db = nullptr;
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return false;
        }

        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return rc == SQLITE_OK;
    }
};

TEST_F(DataAccessLayerTest, SavesAndRetrievesQuiz) {
    const Quiz original = makeQuiz();

    ASSERT_TRUE(dal->quizzes->saveQuiz(original).ok());

    Quiz loaded;
    ASSERT_TRUE(dal->quizzes->findQuiz("quiz-1", &loaded).ok());

    EXPECT_EQ(loaded.quiz_id, original.quiz_id);
    EXPECT_EQ(loaded.title, original.title);
    EXPECT_EQ(loaded.created_by, original.created_by);
    EXPECT_EQ(loaded.created_at, original.created_at);
    EXPECT_EQ(loaded.time_limit_minutes, original.time_limit_minutes);
    EXPECT_DOUBLE_EQ(loaded.scoring.marks_per_question, 2.0);
    EXPECT_DOUBLE_EQ(loaded.scoring.negative_mark, 0.5);
    EXPECT_EQ(loaded.question_ids, original.question_ids);
}

TEST_F(DataAccessLayerTest, ListsSavedQuizzes) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    std::vector<Quiz> quizzes;
    ASSERT_TRUE(dal->quizzes->listQuizzes(&quizzes).ok());

    ASSERT_EQ(quizzes.size(), 1U);
    EXPECT_EQ(quizzes[0].quiz_id, "quiz-1");
}

TEST_F(DataAccessLayerTest, MissingQuizReturnsQuizNotFound) {
    Quiz loaded;

    const Result result = dal->quizzes->findQuiz("missing", &loaded);

    EXPECT_EQ(result.code, ErrorCode::QUIZ_NOT_FOUND);
}

TEST_F(DataAccessLayerTest, SavesAndRetrievesInProgressAttempt) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    const Attempt original = makeAttempt();
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(original).ok());

    Attempt loaded;
    ASSERT_TRUE(
        dal->attempts->findAttempt("attempt-1", &loaded).ok());

    EXPECT_EQ(loaded.quiz_id, original.quiz_id);
    EXPECT_EQ(loaded.student_id, original.student_id);
    EXPECT_EQ(loaded.mode, QuizMode::Graded);
    EXPECT_EQ(loaded.question_order, original.question_order);

    ASSERT_EQ(loaded.answers.size(), 2U);
    EXPECT_EQ(loaded.answers[0].question_id, "q2");
    EXPECT_EQ(loaded.answers[0].selected_option, 1);
    EXPECT_TRUE(loaded.answers[0].flagged_for_review);
    EXPECT_EQ(loaded.answers[1].selected_option, kBlankAnswer);
}

TEST_F(DataAccessLayerTest, FindsOpenAttempt) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    ASSERT_TRUE(
        dal->attempts->saveInProgressAttempt(makeAttempt()).ok());

    Attempt loaded;
    ASSERT_TRUE(
        dal->attempts->findOpenAttempt(
            "quiz-1", "student-1", &loaded).ok());

    EXPECT_EQ(loaded.attempt_id, "attempt-1");
    EXPECT_FALSE(loaded.submitted);
}

TEST_F(DataAccessLayerTest, MissingAttemptReturnsAttemptNotFound) {
    Attempt loaded;

    const Result result =
        dal->attempts->findAttempt("missing", &loaded);

    EXPECT_EQ(result.code, ErrorCode::ATTEMPT_NOT_FOUND);
}

TEST_F(DataAccessLayerTest, SavesAndRetrievesGradeReport) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    Attempt attempt = makeAttempt();
    attempt.submitted = true;
    attempt.submitted_at = "2026-10-26T10:10:00Z";
    attempt.score = 2.0;

    const GradeReport report = makeReport();

    ASSERT_TRUE(
        dal->attempts->saveGradedAttempt(attempt, report).ok());

    GradeReport loaded;
    ASSERT_TRUE(
        dal->attempts->findReport(
            "quiz-1", "student-1", &loaded).ok());

    EXPECT_DOUBLE_EQ(loaded.score, 2.0);
    EXPECT_DOUBLE_EQ(loaded.max_score, 6.0);
    ASSERT_EQ(loaded.itemised_breakdown.size(), 2U);
    EXPECT_EQ(loaded.itemised_breakdown[0].question_id, "q2");
    EXPECT_TRUE(loaded.itemised_breakdown[0].correct);
    ASSERT_EQ(loaded.topic_breakdown.size(), 1U);
    EXPECT_EQ(
        loaded.topic_breakdown[0].subject_tag, "Digital Logic");
}

TEST_F(DataAccessLayerTest, RejectsDuplicateGradedSubmission) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    Attempt attempt = makeAttempt();
    attempt.submitted = true;
    attempt.submitted_at = "2026-10-26T10:10:00Z";

    const GradeReport report = makeReport();

    ASSERT_TRUE(
        dal->attempts->saveGradedAttempt(attempt, report).ok());

    const Result second =
        dal->attempts->saveGradedAttempt(attempt, report);

    EXPECT_EQ(
        second.code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
}

TEST_F(DataAccessLayerTest, QuizSurvivesClosingAndReopeningDatabase) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    dal.reset();

    std::string error;
    dal = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal, nullptr) << error;

    Quiz loaded;
    ASSERT_TRUE(dal->quizzes->findQuiz("quiz-1", &loaded).ok());

    EXPECT_EQ(loaded.title, "Digital Design");
    EXPECT_EQ(
        loaded.question_ids,
        (std::vector<std::string>{"q1", "q2", "q3"}));
}

TEST_F(DataAccessLayerTest, FailedGradeWriteRollsBackAttemptAndReport) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    const Attempt original = makeAttempt();
    ASSERT_TRUE(
        dal->attempts->saveInProgressAttempt(original).ok());

    // Force a primary-key conflict when the DAL writes its first
    // item result, after it has attempted to update the attempt
    // and insert the grade report inside its transaction.
    ASSERT_TRUE(executeSql(
        "INSERT INTO items "
        "(attempt,pos,qid,selected,correct_option,correct,blank,marks) "
        "VALUES('attempt-1',0,'forced-conflict',0,0,0,0,0.0);"));

    Attempt submitted = original;
    submitted.submitted = true;
    submitted.submitted_at = "2026-10-26T10:10:00Z";
    submitted.score = 2.0;

    GradeReport report = makeReport();

    const Result result =
        dal->attempts->saveGradedAttempt(submitted, report);

    EXPECT_FALSE(result.ok());

    // The attempt update must have rolled back.
    Attempt loaded;
    ASSERT_TRUE(
        dal->attempts->findAttempt("attempt-1", &loaded).ok());
    EXPECT_FALSE(loaded.submitted);
    EXPECT_TRUE(loaded.submitted_at.empty());

    // The report insert must also have rolled back.
    GradeReport loaded_report;
    const Result report_result =
        dal->attempts->findReport(
            "quiz-1", "student-1", &loaded_report);

    EXPECT_EQ(
        report_result.code, ErrorCode::ATTEMPT_NOT_FOUND);
}

}  // namespace
}  // namespace quizsphere
