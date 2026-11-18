#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include "quiz_engine.hpp"
#include "question_repository.hpp"

namespace quizsphere {
namespace {
class Part3Acceptance : public ::testing::Test {
protected:
    std::filesystem::path directory;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grader;
    std::unique_ptr<QuizEngine> engine;
    Session teacher{"teacher", Role::Instructor, "2026-11-18T00:00:00Z"};
    void open() {
        std::string error;
        dal = createSqliteDataAccessLayer((directory / "data.sqlite").string(), &error);
        ASSERT_NE(dal, nullptr) << error;
        auto repository = createSqliteQuestionRepository((directory / "data.sqlite").string(), &error);
        ASSERT_NE(repository, nullptr) << error;
        bank = createQuestionBank(std::move(repository));
        ASSERT_TRUE(bank->load().ok());
        grader = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grader);
    }
    void close() { engine.reset(); grader.reset(); bank.reset(); dal.reset(); }
    void SetUp() override {
        directory = std::filesystem::temp_directory_path() / ("qs-part3-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directory(directory);
        open();
        for (int i = 0; i < 20; ++i) {
            Question q{"q" + std::to_string(i), "Math", Difficulty::Easy, QuestionType::MCQ,
                       "Choose the first option", {"A", "B", "C", "D"}, 0, true};
            ASSERT_TRUE(bank->addQuestion(teacher, q).ok());
        }
    }
    void TearDown() override {
        close();
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

TEST_F(Part3Acceptance, PERF_01_LoadAndStartTwentyQuestionQuizWithinTwoSeconds) {
    Quiz quiz;
    ASSERT_TRUE(engine->createQuiz(teacher, {"Performance", 5, 20, {}, {}, {1, 0}}, &quiz).ok());
    double maximum = 0;
    for (int run = 0; run < 5; ++run) {
        close();
        const auto began = std::chrono::steady_clock::now();
        open();
        Attempt attempt;
        const Session student{"perf-" + std::to_string(run), Role::Student, "2026-11-18T00:00:00Z"};
        ASSERT_TRUE(engine->startAttempt(student, quiz.quiz_id, QuizMode::Graded, &attempt).ok());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        ASSERT_EQ(attempt.question_order.size(), 20U);
        maximum = std::max(maximum, ms);
        std::cout << "PERF-01 run " << run + 1 << ": " << ms << " ms\n";
    }
    RecordProperty("maximum_load_start_ms", std::to_string(maximum));
    EXPECT_LT(maximum, 2000.0);
}

TEST_F(Part3Acceptance, UT_DAL_02_QuestionFallbackRoundTripMatchesSqlite) {
    std::vector<Question> expected;
    ASSERT_TRUE(bank->listQuestions(teacher, &expected, false, {}, {}).ok());
    auto fallback = createFileQuestionRepository((directory / "fallback.tsv").string());
    ASSERT_TRUE(fallback->saveAll(expected).ok());
    std::vector<Question> actual;
    ASSERT_TRUE(fallback->loadAll(&actual).ok());
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i].question_id, expected[i].question_id);
        EXPECT_EQ(actual[i].options, expected[i].options);
        EXPECT_EQ(actual[i].question_text, expected[i].question_text);
        EXPECT_EQ(actual[i].correct_option, expected[i].correct_option);
        EXPECT_EQ(actual[i].is_available, expected[i].is_available);
    }
}

TEST_F(Part3Acceptance, SeededMalformedCsvFuzzPreservesTheQuestionBank) {
    std::mt19937 random(20261118);
    const auto csv = directory / "fuzz.csv";
    const std::string header = "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available\n";
    for (int run = 0; run < 100; ++run) {
        std::string payload;
        for (unsigned n = 0; n < random() % 256U; ++n) payload.push_back(static_cast<char>(random() % 256U));
        {
            std::ofstream file(csv, std::ios::binary);
            file << header << "bad,,Easy,MCQ," << payload << ",A|B,not-a-number,true\n";
        }
        const auto imported = bank->importFromCsv(teacher, csv.string());
        EXPECT_EQ(imported.imported_count, 0U);
        std::vector<Question> after;
        ASSERT_TRUE(bank->listQuestions(teacher, &after, false, {}, {}).ok());
        EXPECT_EQ(after.size(), 20U);
    }
    RecordProperty("fuzz_seed", "20261118");
    RecordProperty("generated_cases", "100");
}
}  // namespace
}  // namespace quizsphere
