#include "quizsphere.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

quizsphere::Question makeValidQuestion(const std::string& id = "Q1") {
    return quizsphere::Question{
        id,
        "Math",
        "Easy",
        "MCQ",
        "What is 2 + 2?",
        {"3", "4", "5"},
        1,
        true,
    };
}

class QuestionBankTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto nanos = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        temp_root_ = std::filesystem::temp_directory_path() /
                     ("quizsphere_tests_" + std::to_string(nanos));
        std::filesystem::create_directories(temp_root_);
        storage_path_ = (temp_root_ / "questions.tsv").string();

        instructor_ = {"i1", "Instructor", quizsphere::Role::Instructor, true};
        student_ = {"s1", "Student", quizsphere::Role::Student, true};
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(temp_root_, ec);
    }

    std::filesystem::path temp_root_;
    std::string storage_path_;
    quizsphere::Session instructor_;
    quizsphere::Session student_;
};

TEST_F(QuestionBankTest, MissingSubjectValidation) {
    auto bank = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(bank->load().success);

    auto question = makeValidQuestion("Q_SUBJECT");
    question.subject_tag.clear();

    const auto result = bank->addQuestion(instructor_, question);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Subject tag is required"), std::string::npos);
}

TEST_F(QuestionBankTest, InvalidOrMissingCorrectAnswerValidation) {
    auto bank = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(bank->load().success);

    auto question = makeValidQuestion("Q_CORRECT");
    question.correct_option = question.options.size();

    const auto result = bank->addQuestion(instructor_, question);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Correct option index"), std::string::npos);
}

TEST_F(QuestionBankTest, FewerThanTwoOptionsValidation) {
    auto bank = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(bank->load().success);

    auto question = makeValidQuestion("Q_OPTIONS");
    question.options = {"Only one"};

    const auto result = bank->addQuestion(instructor_, question);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("At least two options"), std::string::npos);
}

TEST_F(QuestionBankTest, AddAndPersistenceReload) {
    {
        auto bank = quizsphere::createQuestionBank(storage_path_);
        ASSERT_TRUE(bank->load().success);

        const auto add_result = bank->addQuestion(instructor_, makeValidQuestion("Q_PERSIST"));
        ASSERT_TRUE(add_result.success) << add_result.message;
    }

    auto reloaded = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(reloaded->load().success);

    const auto found = reloaded->findQuestionById("Q_PERSIST");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->subject_tag, "Math");
    EXPECT_TRUE(found->is_available);
}

TEST_F(QuestionBankTest, DisableExcludesFromAvailableOnlyListing) {
    auto bank = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(bank->load().success);
    ASSERT_TRUE(bank->addQuestion(instructor_, makeValidQuestion("Q_DISABLE")).success);

    const auto disable_result = bank->setAvailability(instructor_, "Q_DISABLE", false);
    ASSERT_TRUE(disable_result.success) << disable_result.message;

    const auto all_questions = bank->listQuestions(false);
    const auto available_questions = bank->listQuestions(true);

    EXPECT_EQ(all_questions.size(), 1U);
    EXPECT_EQ(available_questions.size(), 0U);

    const auto found = bank->findQuestionById("Q_DISABLE");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->is_available);
}

TEST_F(QuestionBankTest, CsvExportImportRoundTrip) {
    auto source = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(source->load().success);
    ASSERT_TRUE(source->addQuestion(instructor_, makeValidQuestion("Q_CSV")).success);

    const std::string csv_path = (temp_root_ / "questions.csv").string();
    const auto export_result = source->exportToCsv(instructor_, csv_path);
    ASSERT_TRUE(export_result.success) << export_result.message;

    const std::string second_storage = (temp_root_ / "imported.tsv").string();
    auto imported = quizsphere::createQuestionBank(second_storage);
    ASSERT_TRUE(imported->load().success);

    const auto import_report = imported->importFromCsv(instructor_, csv_path);
    ASSERT_TRUE(import_report.success) << import_report.message;
    EXPECT_EQ(import_report.imported_count, 1U);
    EXPECT_EQ(import_report.skipped_count, 0U);

    const auto found = imported->findQuestionById("Q_CSV");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->options.size(), 3U);
}

TEST_F(QuestionBankTest, AuthorizationEnforcedInsideQuestionBank) {
    auto bank = quizsphere::createQuestionBank(storage_path_);
    ASSERT_TRUE(bank->load().success);

    const auto result = bank->addQuestion(student_, makeValidQuestion("Q_AUTH"));
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Only Instructor or Administrator"), std::string::npos);
}

}  // namespace
