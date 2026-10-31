#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>
#include <unordered_set>

#include "quiz_engine.hpp"
#include "question_repository.hpp"

namespace quizsphere {
namespace {

class QuizEngineSqliteTest : public ::testing::Test {
protected:
    std::string path;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grading;
    std::unique_ptr<QuizEngine> engine;
    const Session instructor{"1", Role::Instructor, "2026-10-31T00:00:00Z"};

    void open() {
        std::string error;
        dal = createSqliteDataAccessLayer(path, &error);
        ASSERT_NE(dal, nullptr) << error;
        auto repo = createSqliteQuestionRepository(path, &error);
        ASSERT_NE(repo, nullptr) << error;
        bank = createQuestionBank(std::move(repo));
        ASSERT_TRUE(bank->load().ok());
        grading = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grading);
    }
    void close() {
        engine.reset(); grading.reset(); bank.reset(); dal.reset();
    }
    void SetUp() override {
        path = (std::filesystem::temp_directory_path() /
            ("quizsphere-engine-" + std::to_string(std::random_device{}()) + ".sqlite")).string();
        open();
        ASSERT_NE(engine, nullptr);
        for (int i=0; i<4; ++i) {
            Question q{"q" + std::to_string(i), i == 3 ? "Science" : "Math", Difficulty::Easy,
                QuestionType::MCQ, "Choose the first option", {"A", "B"}, 0, i != 2};
            ASSERT_TRUE(bank->addQuestion(instructor, q).ok());
        }
    }
    void TearDown() override {
        close();
        std::error_code ignored;
        for (const std::string suffix : {"", "-journal", "-wal", "-shm"})
            std::filesystem::remove(path + suffix, ignored);
    }
    QuizConfig config() const {
        return {"  Math Quiz  ", 15, 2, "Math", Difficulty::Easy, {4, 1}};
    }
};

TEST_F(QuizEngineSqliteTest, UT_QE_02_UT_QE_04_FilteredSelectionAndScoringSurviveRestart) {
    Quiz quiz;
    ASSERT_TRUE(engine->createQuiz(instructor, config(), &quiz).ok());
    EXPECT_EQ(quiz.title, "Math Quiz");
    EXPECT_EQ(std::unordered_set<std::string>(quiz.question_ids.begin(), quiz.question_ids.end()),
              (std::unordered_set<std::string>{"q0", "q1"}));
    close(); open();
    ASSERT_NE(engine, nullptr);
    Quiz stored;
    ASSERT_TRUE(engine->getQuiz(quiz.quiz_id, &stored).ok());
    EXPECT_EQ(stored.question_ids, quiz.question_ids);
    EXPECT_EQ(stored.created_by, "1");
    EXPECT_EQ(stored.created_at, quiz.created_at);
    EXPECT_EQ(stored.time_limit_minutes, 15);
    EXPECT_DOUBLE_EQ(stored.scoring.marks_per_question, 4);
    EXPECT_DOUBLE_EQ(stored.scoring.negative_mark, 1);
    Question q;
    ASSERT_TRUE(bank->findQuestionById(instructor, "q0", &q).ok());
    q.options[0] = "Changed";
    EXPECT_EQ(bank->updateQuestion(instructor, q).code, ErrorCode::QUESTION_IN_USE);
    EXPECT_EQ(bank->deleteQuestion(instructor, "q0", true).code, ErrorCode::QUESTION_IN_USE);
}

TEST_F(QuizEngineSqliteTest, UT_QE_03_InvalidConfigurationNeverPersistsAPartialQuiz) {
    Quiz quiz;
    auto invalid = config();
    invalid.question_count = 3;
    EXPECT_EQ(engine->createQuiz(instructor, invalid, &quiz).code, ErrorCode::VALIDATION_FAILED);
    invalid = config(); invalid.subject_filter = "Unknown";
    EXPECT_EQ(engine->createQuiz(instructor, invalid, &quiz).code, ErrorCode::VALIDATION_FAILED);
    invalid = config(); invalid.time_limit_minutes = 0;
    EXPECT_EQ(engine->createQuiz(instructor, invalid, &quiz).code, ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(engine->createQuiz({"2", Role::Student, "2026-10-31T00:00:00Z"}, config(), &quiz).code,
              ErrorCode::NOT_AUTHORIZED);
    close(); open();
    std::vector<Quiz> quizzes;
    ASSERT_TRUE(engine->listQuizzes(&quizzes).ok());
    EXPECT_TRUE(quizzes.empty());
}

TEST_F(QuizEngineSqliteTest, UT_QE_05_ZeroPenaltyRemainsDisabledAfterRestart) {
    auto cfg = config(); cfg.scoring.negative_mark = 0;
    Quiz quiz;
    ASSERT_TRUE(engine->createQuiz(instructor, cfg, &quiz).ok());
    close(); open();
    Quiz stored;
    ASSERT_TRUE(engine->getQuiz(quiz.quiz_id, &stored).ok());
    EXPECT_DOUBLE_EQ(stored.scoring.negative_mark, 0);
}

TEST_F(QuizEngineSqliteTest, CreatedQuizSupportsTransactionalAttemptAndAnswerPersistence) {
    Quiz quiz;
    ASSERT_TRUE(engine->createQuiz(instructor, config(), &quiz).ok());
    Attempt attempt;
    attempt.attempt_id = "attempt"; attempt.quiz_id = quiz.quiz_id;
    attempt.student_id = "2"; attempt.started_at = "2026-10-31T00:01:00Z";
    attempt.question_order = quiz.question_ids;
    attempt.answers = {{quiz.question_ids[0], 0, true}};
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    attempt.answers = {{quiz.question_ids[1], kBlankAnswer, false}};
    ASSERT_TRUE(dal->attempts->saveInProgressAttempt(attempt).ok());
    close(); open();
    Attempt stored;
    ASSERT_TRUE(dal->attempts->findAttempt("attempt", &stored).ok());
    EXPECT_EQ(stored.quiz_id, quiz.quiz_id);
    EXPECT_EQ(stored.question_order, quiz.question_ids);
    ASSERT_EQ(stored.answers.size(), 1U);
    EXPECT_EQ(stored.answers[0].question_id, quiz.question_ids[1]);
    EXPECT_EQ(stored.answers[0].selected_option, kBlankAnswer);
}

}  // namespace
}  // namespace quizsphere
