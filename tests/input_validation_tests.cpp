#include "input_validation.hpp"
#include "console_input.hpp"
#include "admin_user_menu.hpp"
#include "analytics_console.hpp"
#include "console_menu.hpp"
#include "quiz_console.hpp"
#include "question_repository.hpp"

#include <filesystem>
#include <random>
#include <sstream>
#include <gtest/gtest.h>

namespace quizsphere {
namespace {

TEST(InputValidation, ST_SEC_01_BoundsDrainsAndDoesNotExecuteAnOversizedSuffix) {
    std::istringstream input(std::string(100000, 'x') + "yes\nnext\n");
    std::ostringstream output;
    std::string value = "old";
    EXPECT_FALSE(readConsoleLine(input, output, value));
    EXPECT_TRUE(value.empty());
    ASSERT_TRUE(readConsoleLine(input, output, value));
    EXPECT_EQ(value, "next");
    EXPECT_EQ(output.str().find("xxxxx"), std::string::npos);
}

TEST(InputValidation, ST_SEC_02_RejectsControlsAndInvalidUtf8PreservesValidUnicode) {
    for (const auto& bad : {std::string("a\0b", 3), std::string("a\x1B[2J"),
                           std::string("\xC0\xAF"), std::string("\xED\xA0\x80"),
                           std::string("\xF4\x90\x80\x80"), std::string("\xE2\x82")}) {
        EXPECT_FALSE(validInputText(bad, 200));
        std::istringstream input(bad + "\n");
        std::ostringstream output;
        std::string value;
        EXPECT_FALSE(readConsoleLine(input, output, value));
        EXPECT_TRUE(value.empty());
    }
    EXPECT_TRUE(validInputText("caf\xC3\xA9", 4));
    EXPECT_FALSE(validInputText("caf\xC3\xA9", 3));
    EXPECT_FALSE(validInputText("two\nlines", 20));
    EXPECT_TRUE(validInputText("two\nlines", 20, true));
}

TEST(InputValidation, ST_SEC_03_PasswordBoundaryNeverReturnsAPartialSecret) {
    for (const std::size_t length : {1024U, 1025U}) {
        std::istringstream input(std::string(length, 's') + "\nnext\n");
        std::ostringstream output;
        std::string value;
        EXPECT_EQ(readConsolePassword(input, output, "Password: ", value), length == 1024);
        EXPECT_EQ(value.size(), length == 1024 ? 1024U : 0U);
        EXPECT_EQ(output.str().find("ssss"), std::string::npos);
        ASSERT_TRUE(readConsoleLine(input, output, value));
        EXPECT_EQ(value, "next");
    }
}

class SecurityMenus : public ::testing::Test {
protected:
    std::filesystem::path path;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grader;
    std::unique_ptr<QuizEngine> engine;
    std::unique_ptr<AnalyticsModule> analytics;
    Session instructor{"teacher", Role::Instructor, "2026-11-18T00:00:00Z"};
    void SetUp() override {
        path = std::filesystem::temp_directory_path() / ("qs-validation-" + std::to_string(std::random_device{}()) + ".sqlite");
        std::string error;
        dal = createSqliteDataAccessLayer(path.string(), &error);
        ASSERT_NE(dal, nullptr) << error;
        auto files = createSqliteQuestionRepository(path.string(), &error);
        ASSERT_NE(files, nullptr) << error;
        bank = createQuestionBank(std::move(files));
        ASSERT_TRUE(bank->load().ok());
        grader = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grader);
        analytics = createAnalyticsModule(*bank, *dal->quizzes, *dal->attempts);
    }
    void TearDown() override {
        analytics.reset(); engine.reset(); grader.reset(); bank.reset(); dal.reset();
        std::error_code ignored;
        for (const auto* suffix : {"", "-journal", "-wal", "-shm"}) std::filesystem::remove(path.string() + suffix, ignored);
    }
};

TEST_F(SecurityMenus, ST_SEC_04_QuestionMenuRejectsMalformedAndOversizedDataBeforeStorage) {
    for (const auto& command : {"3\nq\nMath\nINVALID\n0\n", "3\nq\nMath\nEasy\nText\nA|B\n-1\n0\n",
                               "3\nq\nMath\nEasy\nText\nA|B\n9999999999999999999999999999999\n0\n"}) {
        std::istringstream input(command);
        std::ostringstream output;
        createConsoleMenu(instructor, "teacher", *bank, input, output)->run();
    }
    std::istringstream oversized("3\n" + std::string(100000, 'x') + "\n0\n");
    std::ostringstream output;
    createConsoleMenu(instructor, "teacher", *bank, oversized, output)->run();
    std::vector<Question> questions;
    ASSERT_TRUE(bank->listQuestions(instructor, &questions, false, {}, {}).ok());
    EXPECT_TRUE(questions.empty());
}

TEST_F(SecurityMenus, RejectedQuestionMenuLineDoesNotEndTheMenu) {
    for (const auto& rejected : {std::string("bad\x1B"), std::string(100000, 'x'),
                                 std::string("\xFF")}) {
        // Listing questions proves the valid command after rejection was executed.
        std::istringstream input(rejected + "\n1\n\n\n0\n");
        std::ostringstream output;
        createConsoleMenu(instructor, "teacher", *bank, input, output)->run();
        EXPECT_NE(output.str().find("Invalid input"), std::string::npos);
        EXPECT_NE(output.str().find("Subject filter"), std::string::npos);
        EXPECT_EQ(output.str().find("Input ended"), std::string::npos);
    }
}

TEST_F(SecurityMenus, ST_SEC_05_QuizCreationValidatesDomainTextAndMenuNumbers) {
    for (const auto& title : {std::string(201, 'x'), std::string("bad\x1Btitle"), std::string("\xFF"), std::string(" ")}) {
        QuizConfig config;
        config.title = title; config.question_count = 1; config.time_limit_minutes = 1;
        config.scoring = {1, 0};
        Quiz quiz;
        EXPECT_EQ(engine->createQuiz(instructor, config, &quiz).code, ErrorCode::VALIDATION_FAILED);
    }
    for (const auto& input_text : {"Quiz\n-1\n", "Quiz\n1x\n", "Quiz\n9999999999999999999999\n",
                                   "Quiz\n1\n0\n\n\n1\n0\n", "Quiz\n1\n1\n\n\nnan\n0\n",
                                   "Quiz\n1\n1\n\n\n1\n-1\n"}) {
        std::istringstream input(input_text);
        std::ostringstream output;
        runQuizCreation(instructor, *engine, input, output);
    }
    std::vector<Quiz> quizzes;
    ASSERT_TRUE(dal->quizzes->listQuizzes(&quizzes).ok());
    EXPECT_TRUE(quizzes.empty());
}

TEST_F(SecurityMenus, ST_SEC_06_AdminAndAnalyticsRejectOversizedCommands) {
    const auto created = bootstrapAdmin(*dal->users, "root_admin", "FixturePass123!");
    ASSERT_TRUE(created.ok());
    const Session admin{created.user_id, Role::Admin, "2026-11-18T00:00:00Z"};
    std::istringstream input("2\n" + std::string(100000, 'x') + "\n");
    std::ostringstream output;
    runAdminUserMenu(admin, *dal->users, input, output);
    EXPECT_NE(output.str().find("Invalid input"), std::string::npos);
    int count = 0;
    ASSERT_TRUE(dal->users->countAdmins(count));
    EXPECT_EQ(count, 1);
    std::istringstream analytics_input(std::string(100000, 'x') + "\n");
    std::ostringstream analytics_output;
    runAnalyticsMenu(instructor, *analytics, *dal->quizzes, analytics_input, analytics_output);
    EXPECT_NE(analytics_output.str().find("Invalid input"), std::string::npos);
}

TEST(InputValidation, ST_SEC_07_ImportExportRejectsMalformedPathsWithoutTruncation) {
    auto files = createFileQuestionRepository("unused.tsv");
    for (const auto& path : {std::string(), std::string(" "), std::string("target\0.csv", 11),
                             std::string(4097, 'x'), std::string("bad\x1B.csv")}) {
        std::string content;
        EXPECT_EQ(files->readTextFile(path, &content).code, ErrorCode::VALIDATION_FAILED);
        EXPECT_EQ(files->writeTextFileAtomic(path, "data", true).code, ErrorCode::VALIDATION_FAILED);
    }
}

}  // namespace
}  // namespace quizsphere
