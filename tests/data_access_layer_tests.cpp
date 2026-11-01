#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "data_access_layer.hpp"
#include "question_repository.hpp"
#include "question_bank.hpp"

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
        auto questions = createSqliteQuestionRepository(db_path, &error);
        ASSERT_NE(questions, nullptr) << error;
        std::vector<Question> seed;
        for (const std::string id : {"q1", "q2", "q3"}) {
            seed.push_back({id, "Digital Logic", Difficulty::Easy, QuestionType::MCQ,
                            "Choose an option", {"A", "B", "C"}, 1, true});
        }
        ASSERT_TRUE(questions->saveAll(seed).ok());
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

// NEW TEST: saving an existing open attempt updates its answers.
TEST_F(DataAccessLayerTest, UpdatesExistingInProgressAttempt) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    Attempt original = makeAttempt();

    ASSERT_TRUE(
        dal->attempts->saveInProgressAttempt(original).ok());

    Attempt updated = original;
    updated.answers = {
        {"q2", 0, false},
        {"q1", 2, true}
    };

    ASSERT_TRUE(
        dal->attempts->saveInProgressAttempt(updated).ok());

    Attempt loaded;

    ASSERT_TRUE(
        dal->attempts->findAttempt("attempt-1", &loaded).ok());

    ASSERT_EQ(loaded.answers.size(), 2U);
    EXPECT_EQ(loaded.answers[0].selected_option, 0);
    EXPECT_FALSE(loaded.answers[0].flagged_for_review);
    EXPECT_EQ(loaded.answers[1].selected_option, 2);
    EXPECT_TRUE(loaded.answers[1].flagged_for_review);
}

// NEW TEST: a submitted attempt cannot be saved as an open attempt.
TEST_F(DataAccessLayerTest, RejectsSavingSubmittedAttemptAsInProgress) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());

    Attempt attempt = makeAttempt();
    attempt.submitted = true;
    attempt.submitted_at = "2026-10-26T10:10:00Z";

    const Result result =
        dal->attempts->saveInProgressAttempt(attempt);

    EXPECT_EQ(result.code, ErrorCode::VALIDATION_FAILED);
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
    attempt.score = 2.0;

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

TEST_F(DataAccessLayerTest, QuestionsCrudAndMetadataSurviveRestart) {
    std::string error;
    auto repo = createSqliteQuestionRepository(db_path, &error);
    ASSERT_NE(repo, nullptr) << error;
    auto bank = createQuestionBank(std::move(repo));
    ASSERT_TRUE(bank->load().ok());
    const Session instructor{"1", Role::Instructor, "2026-10-31T00:00:00Z"};
    Question q{"new", "Math", Difficulty::Hard, QuestionType::MCQ, "Two plus two?", {"3", "4"}, 1, true};
    ASSERT_TRUE(bank->addQuestion(instructor, q).ok());
    q.subject_tag = "Algebra";
    q.options = {"4", "5", "6"};
    q.correct_option = 0;
    ASSERT_TRUE(bank->updateQuestion(instructor, q).ok());
    bank.reset(); dal.reset();
    dal = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal, nullptr) << error;
    bank = createQuestionBank(createSqliteQuestionRepository(db_path, &error));
    ASSERT_TRUE(bank->load().ok());
    Question loaded;
    ASSERT_TRUE(bank->findQuestionById(instructor, "new", &loaded).ok());
    EXPECT_EQ(loaded.subject_tag, "Algebra");
    EXPECT_EQ(loaded.difficulty, Difficulty::Hard);
    EXPECT_EQ(loaded.options, q.options);
    EXPECT_EQ(loaded.correct_option, 0U);
    ASSERT_TRUE(bank->deleteQuestion(instructor, "new", true).ok());
    ASSERT_TRUE(bank->load().ok());
    EXPECT_EQ(bank->findQuestionById(instructor, "new", &loaded).code, ErrorCode::QUESTION_NOT_FOUND);
}

TEST_F(DataAccessLayerTest, ReferencedQuestionEditAndDeleteAreRejectedAcrossRestart) {
    std::string error;
    auto bank = createQuestionBank(createSqliteQuestionRepository(db_path, &error));
    ASSERT_TRUE(bank->load().ok());
    const Session instructor{"1", Role::Instructor, "2026-10-31T00:00:00Z"};
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    Question q;
    ASSERT_TRUE(bank->findQuestionById(instructor, "q1", &q).ok());
    q.question_text = "Changed";
    EXPECT_EQ(bank->updateQuestion(instructor, q).code, ErrorCode::QUESTION_IN_USE);
    EXPECT_EQ(bank->deleteQuestion(instructor, "q1", true).code, ErrorCode::QUESTION_IN_USE);
    EXPECT_TRUE(bank->setAvailability(instructor, "q1", false).ok());
    bank.reset(); dal.reset();
    dal = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal, nullptr) << error;
    bank = createQuestionBank(createSqliteQuestionRepository(db_path, &error));
    ASSERT_TRUE(bank->load().ok());
    ASSERT_TRUE(bank->findQuestionById(instructor, "q1", &q).ok());
    EXPECT_EQ(q.question_text, "Choose an option");
    EXPECT_FALSE(q.is_available);
    Quiz quiz;
    ASSERT_TRUE(dal->quizzes->findQuiz("quiz-1", &quiz).ok());
    EXPECT_EQ(quiz.question_ids, makeQuiz().question_ids);
}

TEST_F(DataAccessLayerTest, DirectRepositoryCannotBypassQuestionProtection) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    std::string error;
    auto repo = createSqliteQuestionRepository(db_path, &error);
    std::vector<Question> questions;
    ASSERT_TRUE(repo->loadAll(&questions).ok());
    questions[0].options[0] = "Changed";
    EXPECT_EQ(repo->saveAll(questions).code, ErrorCode::QUESTION_IN_USE);
    questions.clear();
    EXPECT_EQ(repo->saveAll(questions).code, ErrorCode::QUESTION_IN_USE);
    EXPECT_FALSE(executeSql("DELETE FROM questions WHERE id='q1'"));
    EXPECT_FALSE(executeSql("UPDATE question_options SET text='Changed' WHERE qid='q1'"));
}

TEST_F(DataAccessLayerTest, ClearedAnswersStayClearedAfterReopening) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    auto attempt = makeAttempt();
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    attempt.answers.erase(attempt.answers.begin());
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    Attempt loaded;
    ASSERT_TRUE(dal->attempts->findAttempt(attempt.attempt_id, &loaded).ok());
    ASSERT_EQ(loaded.answers.size(), 1U);
    EXPECT_EQ(loaded.answers[0].question_id, "q1");
    attempt.answers.clear();
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    dal.reset();
    std::string error;
    dal = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal, nullptr) << error;
    ASSERT_TRUE(dal->attempts->findAttempt(attempt.attempt_id, &loaded).ok());
    EXPECT_TRUE(loaded.answers.empty());
    EXPECT_EQ(loaded.question_order, attempt.question_order);
}

TEST_F(DataAccessLayerTest, UT_DAL_01_FailedAnswerSaveRestoresOriginalState) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    auto attempt = makeAttempt();
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    ASSERT_TRUE(executeSql("CREATE TRIGGER fail_answer BEFORE INSERT ON answers WHEN NEW.selected=2 BEGIN SELECT RAISE(ABORT,'forced'); END"));
    attempt.answers[0].selected_option = 2;
    EXPECT_FALSE(dal->attempts->saveInProgressAttempt(attempt).ok());
    Attempt loaded;
    ASSERT_TRUE(dal->attempts->findAttempt(attempt.attempt_id, &loaded).ok());
    ASSERT_EQ(loaded.answers.size(), 2U);
    EXPECT_EQ(loaded.answers[0].selected_option, 1);
    EXPECT_EQ(loaded.question_order, makeAttempt().question_order);
}

TEST_F(DataAccessLayerTest, FailedQuizUpdateRestoresOriginalQuestionsAndScoring) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    auto quiz = makeQuiz();
    quiz.title = "Changed";
    quiz.scoring = {4, 1};
    quiz.question_ids.push_back("missing");
    EXPECT_FALSE(dal->quizzes->saveQuiz(quiz).ok());
    Quiz loaded;
    ASSERT_TRUE(dal->quizzes->findQuiz(quiz.quiz_id, &loaded).ok());
    EXPECT_EQ(loaded.title, "Digital Design");
    EXPECT_EQ(loaded.question_ids, makeQuiz().question_ids);
    EXPECT_DOUBLE_EQ(loaded.scoring.marks_per_question, 2);
}

TEST_F(DataAccessLayerTest, FailedQuestionOptionSaveRollsBackEveryQuestion) {
    std::string error;
    auto repo = createSqliteQuestionRepository(db_path, &error);
    std::vector<Question> questions;
    ASSERT_TRUE(repo->loadAll(&questions).ok());
    ASSERT_TRUE(executeSql("CREATE TRIGGER fail_option BEFORE INSERT ON question_options WHEN NEW.text='FAIL' BEGIN SELECT RAISE(ABORT,'forced'); END"));
    questions[0].subject_tag = "Changed";
    questions[1].options[0] = "FAIL";
    EXPECT_FALSE(repo->saveAll(questions).ok());
    ASSERT_TRUE(repo->loadAll(&questions).ok());
    EXPECT_EQ(questions[0].subject_tag, "Digital Logic");
    EXPECT_EQ(questions[1].options[0], "A");
}

TEST_F(DataAccessLayerTest, SubmittedAttemptAndReportSurviveRestartAndRejectChanges) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    auto attempt = makeAttempt();
    attempt.submitted = true;
    attempt.submitted_at = "2026-10-31T00:10:00Z";
    attempt.score = 2;
    ASSERT_TRUE(dal->attempts->saveGradedAttempt(attempt, makeReport()).ok());
    dal.reset();
    std::string error;
    dal = createSqliteDataAccessLayer(db_path, &error);
    ASSERT_NE(dal, nullptr) << error;
    auto changed = attempt;
    changed.submitted = false;
    changed.submitted_at.clear();
    changed.answers.clear();
    EXPECT_EQ(dal->attempts->saveInProgressAttempt(changed).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    EXPECT_EQ(dal->attempts->saveGradedAttempt(attempt, makeReport()).code, ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    GradeReport report;
    ASSERT_TRUE(dal->attempts->findReport("quiz-1", "student-1", &report).ok());
    EXPECT_DOUBLE_EQ(report.score, 2);
    Attempt loaded;
    ASSERT_TRUE(dal->attempts->findAttempt("attempt-1", &loaded).ok());
    EXPECT_TRUE(loaded.submitted);
    ASSERT_EQ(loaded.answers.size(), 2U);
    EXPECT_EQ(loaded.answers[0].selected_option, 1);
    EXPECT_FALSE(executeSql("INSERT INTO attempt_q VALUES('attempt-1',2,'q3')"));
    EXPECT_FALSE(executeSql("UPDATE answers SET selected=0 WHERE attempt='attempt-1'"));
}

TEST_F(DataAccessLayerTest, ForeignKeysAndLegacyReferenceChecksRejectOrphans) {
    auto attempt = makeAttempt();
    EXPECT_FALSE(dal->attempts->saveInProgressAttempt(attempt).ok());
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(db_path.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_NE(sqlite3_exec(db, "INSERT INTO question_options VALUES('missing',0,'A')", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
    EXPECT_FALSE(executeSql("INSERT INTO answers VALUES('missing','q1',0,0)"));
}

TEST_F(DataAccessLayerTest, AttemptIdentityAndDuplicateAnswersCannotChange) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    auto attempt = makeAttempt();
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    attempt.student_id = "someone-else";
    EXPECT_EQ(dal->attempts->saveInProgressAttempt(attempt).code, ErrorCode::VALIDATION_FAILED);
    attempt = makeAttempt();
    attempt.answers.push_back(attempt.answers[0]);
    EXPECT_EQ(dal->attempts->saveInProgressAttempt(attempt).code, ErrorCode::VALIDATION_FAILED);
}

TEST_F(DataAccessLayerTest, UT_DAL_02_FileAndSqliteQuestionStorageHaveParity) {
    std::string error;
    auto sqlite = createSqliteQuestionRepository(db_path, &error);
    auto file = createFileQuestionRepository(db_path + ".tsv");
    const std::vector<Question> original = {{"quoted';--", "Math", Difficulty::Medium,
        QuestionType::MCQ, "What is 2+2?", {"4|four", "5\\five"}, 0, false}};
    ASSERT_TRUE(sqlite->saveAll(original).ok());
    ASSERT_TRUE(file->saveAll(original).ok());
    std::vector<Question> from_sqlite, from_file;
    ASSERT_TRUE(sqlite->loadAll(&from_sqlite).ok());
    ASSERT_TRUE(file->loadAll(&from_file).ok());
    ASSERT_EQ(from_sqlite.size(), 1U);
    ASSERT_EQ(from_file.size(), 1U);
    EXPECT_EQ(from_sqlite[0].question_id, from_file[0].question_id);
    EXPECT_EQ(from_sqlite[0].options, from_file[0].options);
    EXPECT_EQ(from_sqlite[0].subject_tag, from_file[0].subject_tag);
    EXPECT_EQ(from_sqlite[0].difficulty, from_file[0].difficulty);
    EXPECT_EQ(from_sqlite[0].question_text, from_file[0].question_text);
    EXPECT_EQ(from_sqlite[0].correct_option, from_file[0].correct_option);
    EXPECT_EQ(from_sqlite[0].is_available, from_file[0].is_available);
    std::remove((db_path + ".tsv").c_str());
}

TEST_F(DataAccessLayerTest, LegacyQuizRowsArePreservedWhenIntegrityChecksAreInstalled) {
    const std::string legacy_path = db_path + ".legacy";
    std::remove(legacy_path.c_str());
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(legacy_path.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
        "CREATE TABLE quizzes(id TEXT PRIMARY KEY,title TEXT,creator TEXT,created TEXT,minutes INTEGER,marks REAL,negative REAL);"
        "CREATE TABLE quiz_q(quiz TEXT,pos INTEGER,qid TEXT,PRIMARY KEY(quiz,pos));"
        "INSERT INTO quizzes VALUES('legacy','Old title','1','2026-10-01T00:00:00Z',10,4,1);"
        "INSERT INTO quiz_q VALUES('legacy',0,'legacy-question');", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
    std::string error;
    auto layer = createSqliteDataAccessLayer(legacy_path, &error);
    ASSERT_NE(layer, nullptr) << error;
    Quiz loaded;
    ASSERT_TRUE(layer->quizzes->findQuiz("legacy", &loaded).ok());
    EXPECT_EQ(loaded.question_ids, (std::vector<std::string>{"legacy-question"}));
    EXPECT_DOUBLE_EQ(loaded.scoring.marks_per_question, 4);
    auto repo = createSqliteQuestionRepository(legacy_path, &error);
    ASSERT_NE(repo, nullptr) << error;
    const Question restored{"legacy-question", "Math", Difficulty::Easy, QuestionType::MCQ,
                             "Original legacy content", {"A", "B"}, 0, true};
    ASSERT_TRUE(repo->saveAll({restored}).ok());
    std::vector<Question> questions;
    ASSERT_TRUE(repo->loadAll(&questions).ok());
    ASSERT_EQ(questions.size(), 1U);
    EXPECT_EQ(questions[0].options, restored.options);
    repo.reset();
    layer.reset();
    std::remove(legacy_path.c_str());
}

TEST_F(DataAccessLayerTest, RejectsInvalidScoringAtTheStorageBoundary) {
    for (const double marks : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        auto quiz = makeQuiz();
        quiz.scoring.marks_per_question = marks;
        EXPECT_EQ(dal->quizzes->saveQuiz(quiz).code, ErrorCode::VALIDATION_FAILED);
    }
    for (const double penalty : {-1.0, std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
        auto quiz = makeQuiz();
        quiz.scoring.negative_mark = penalty;
        EXPECT_EQ(dal->quizzes->saveQuiz(quiz).code, ErrorCode::VALIDATION_FAILED);
    }
    std::vector<Quiz> quizzes;
    ASSERT_TRUE(dal->quizzes->listQuizzes(&quizzes).ok());
    EXPECT_TRUE(quizzes.empty());
}

TEST_F(DataAccessLayerTest, HistoricalAttemptKeepsQuestionProtectedAfterQuizPoolChanges) {
    ASSERT_TRUE(dal->quizzes->saveQuiz(makeQuiz()).ok());
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(makeAttempt()).ok());
    auto quiz = makeQuiz();
    quiz.question_ids = {"q3"};
    ASSERT_TRUE(dal->quizzes->saveQuiz(quiz).ok());
    std::string error;
    auto repo = createSqliteQuestionRepository(db_path, &error);
    EXPECT_TRUE(repo->isReferenced("q1"));
    std::vector<Question> questions;
    ASSERT_TRUE(repo->loadAll(&questions).ok());
    questions[0].question_text = "Changed";
    EXPECT_EQ(repo->saveAll(questions).code, ErrorCode::QUESTION_IN_USE);
}

}  // namespace
}  // namespace quizsphere