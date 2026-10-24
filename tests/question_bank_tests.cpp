#include "console_menu.hpp"
#include "question_bank.hpp"

#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::filesystem::path uniqueTempDir() {
    static std::uint64_t counter = 0;
    std::random_device rd;
    const auto token = std::to_string(static_cast<unsigned long long>(rd())) + "_" + std::to_string(++counter);
    const auto path = std::filesystem::temp_directory_path() / ("quizsphere_tests_" + token);
    std::filesystem::create_directories(path);
    return path;
}

quizsphere::Session instructorSession() {
    return {"instructor-1", quizsphere::Role::Instructor, "2026-10-24T00:00:00Z"};
}

quizsphere::Session studentSession() {
    return {"student-1", quizsphere::Role::Student, "2026-10-24T00:00:00Z"};
}

quizsphere::Session invalidSession() {
    return {"", quizsphere::Role::Instructor, ""};
}

quizsphere::Question baseQuestion(const std::string& id = "Q1") {
    return quizsphere::Question{
        id,
        "Math",
        quizsphere::Difficulty::Easy,
        quizsphere::QuestionType::MCQ,
        "What is 2+2?",
        {"3", "4"},
        1,
        true,
    };
}

class InMemoryRepository final : public quizsphere::QuestionRepository {
public:
    quizsphere::Result loadAll(std::vector<quizsphere::Question>* questions) override {
        *questions = stored_;
        return {quizsphere::ErrorCode::OK, "ok"};
    }

    quizsphere::Result saveAll(const std::vector<quizsphere::Question>& questions) override {
        if (fail_save_) {
            return {quizsphere::ErrorCode::IO_ERROR, "save failed"};
        }
        stored_ = questions;
        return {quizsphere::ErrorCode::OK, "ok"};
    }

    quizsphere::Result readTextFile(const std::string& path, std::string* content) const override {
        const auto it = files_.find(path);
        if (it == files_.end()) {
            return {quizsphere::ErrorCode::IO_ERROR, "missing"};
        }
        *content = it->second;
        return {quizsphere::ErrorCode::OK, "ok"};
    }

    quizsphere::Result writeTextFileAtomic(const std::string& path,
                                           const std::string& content,
                                           bool overwrite) const override {
        const bool exists = files_.find(path) != files_.end();
        if (exists && !overwrite) {
            return {quizsphere::ErrorCode::CONFIRMATION_REQUIRED, "overwrite required"};
        }
        files_[path] = content;
        return {quizsphere::ErrorCode::OK, "ok"};
    }

    [[nodiscard]] bool isReferenced(const std::string& question_id) const override {
        return referenced_.find(question_id) != referenced_.end();
    }

    mutable std::unordered_map<std::string, std::string> files_;
    std::vector<quizsphere::Question> stored_;
    std::unordered_set<std::string> referenced_;
    bool fail_save_{false};
};

class QuestionBankFixture : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = uniqueTempDir();
        storage_path_ = (root_ / "questions.tsv").string();
        bank_ = quizsphere::createQuestionBank(quizsphere::createFileQuestionRepository(storage_path_));
        ASSERT_TRUE(bank_->load().ok());
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    std::filesystem::path root_;
    std::string storage_path_;
    std::unique_ptr<quizsphere::QuestionBank> bank_;
};

TEST_F(QuestionBankFixture, UT_QB_01_MissingSubjectValidation) {
    auto q = baseQuestion("Q_SUB");
    q.subject_tag = "   ";
    const auto result = bank_->addQuestion(instructorSession(), q);
    EXPECT_EQ(result.code, quizsphere::ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuestionBankFixture, UT_QB_02_InvalidCorrectAnswerValidation) {
    auto q = baseQuestion("Q_COR");
    q.correct_option = 2;
    const auto result = bank_->addQuestion(instructorSession(), q);
    EXPECT_EQ(result.code, quizsphere::ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuestionBankFixture, UT_QB_03_FewerThanTwoOptionsValidation) {
    auto q = baseQuestion("Q_OPT");
    q.options = {"only"};
    const auto result = bank_->addQuestion(instructorSession(), q);
    EXPECT_EQ(result.code, quizsphere::ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuestionBankFixture, UT_QB_04_SpecialCharacterRoundTrip_TsvAndCsv) {
    std::vector<std::string> samples = {
        "line1\nline2", "carriage\rreturn", "tab\tvalue", "back\\slash", "pipe|value",
        "escaped\\|pipe", "\"quoted\"", "comma,value", "Üñîçødé", "  spaced  "
    };

    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto q = baseQuestion("Q_SP_" + std::to_string(i));
        q.subject_tag = samples[i];
        q.question_text = samples[i];
        q.options = {samples[i], "safe"};
        ASSERT_EQ(bank_->addQuestion(instructorSession(), q).code, quizsphere::ErrorCode::OK);
    }

    auto loaded_bank = quizsphere::createQuestionBank(quizsphere::createFileQuestionRepository(storage_path_));
    ASSERT_TRUE(loaded_bank->load().ok());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        quizsphere::Question found;
        ASSERT_EQ(loaded_bank->findQuestionById(instructorSession(), "Q_SP_" + std::to_string(i), &found).code,
                  quizsphere::ErrorCode::OK);
        EXPECT_EQ(found.subject_tag, samples[i]);
        EXPECT_EQ(found.question_text, samples[i]);
        EXPECT_EQ(found.options[0], samples[i]);
    }

    const std::string csv = (root_ / "round.csv").string();
    ASSERT_EQ(loaded_bank->exportToCsv(instructorSession(), csv, std::nullopt, std::nullopt, true).code,
              quizsphere::ErrorCode::OK);

    auto csv_bank = quizsphere::createQuestionBank(quizsphere::createFileQuestionRepository((root_ / "csv.tsv").string()));
    ASSERT_TRUE(csv_bank->load().ok());
    const auto report = csv_bank->importFromCsv(instructorSession(), csv);
    ASSERT_EQ(report.result.code, quizsphere::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, samples.size());

    for (std::size_t i = 0; i < samples.size(); ++i) {
        quizsphere::Question found;
        ASSERT_EQ(csv_bank->findQuestionById(instructorSession(), "Q_SP_" + std::to_string(i), &found).code,
                  quizsphere::ErrorCode::OK);
        EXPECT_EQ(found.subject_tag, samples[i]);
        EXPECT_EQ(found.question_text, samples[i]);
        EXPECT_EQ(found.options[0], samples[i]);
    }
}

TEST_F(QuestionBankFixture, UT_QB_05_ListAvailable_FilterCombinations) {
    auto q1 = baseQuestion("Q_A");
    q1.subject_tag = "Math";
    q1.difficulty = quizsphere::Difficulty::Easy;
    auto q2 = baseQuestion("Q_B");
    q2.subject_tag = "Science";
    q2.difficulty = quizsphere::Difficulty::Medium;
    auto q3 = baseQuestion("Q_C");
    q3.subject_tag = "math";
    q3.difficulty = quizsphere::Difficulty::Medium;
    q3.is_available = false;

    ASSERT_TRUE(bank_->addQuestion(instructorSession(), q1).ok());
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), q2).ok());
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), q3).ok());

    std::vector<quizsphere::PublicQuestion> out;
    ASSERT_TRUE(bank_->listAvailable(&out, std::nullopt, std::nullopt).ok());
    EXPECT_EQ(out.size(), 2U);

    ASSERT_TRUE(bank_->listAvailable(&out, std::optional<std::string>{"math"}, std::nullopt).ok());
    EXPECT_EQ(out.size(), 1U);

    ASSERT_TRUE(bank_->listAvailable(&out, std::nullopt, std::optional<quizsphere::Difficulty>{quizsphere::Difficulty::Medium}).ok());
    EXPECT_EQ(out.size(), 1U);

    ASSERT_TRUE(bank_->listAvailable(&out,
                                     std::optional<std::string>{"science"},
                                     std::optional<quizsphere::Difficulty>{quizsphere::Difficulty::Medium})
                    .ok());
    EXPECT_EQ(out.size(), 1U);
}

TEST_F(QuestionBankFixture, UT_QB_06_DeleteWithAndWithoutConfirmation) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_DEL")).ok());
    EXPECT_EQ(bank_->deleteQuestion(instructorSession(), "Q_DEL", false).code,
              quizsphere::ErrorCode::CONFIRMATION_REQUIRED);
    EXPECT_EQ(bank_->deleteQuestion(instructorSession(), "Q_DEL", true).code, quizsphere::ErrorCode::OK);
}

TEST(QuestionBankUnit, UT_QB_07_QuestionInUseBlocksUpdateDeleteAndRollbackOnPersistFailure) {
    auto repo = std::make_unique<InMemoryRepository>();
    repo->stored_.push_back(baseQuestion("Q_LOCK"));
    repo->referenced_.insert("Q_LOCK");
    auto* repo_ptr = repo.get();

    auto bank = quizsphere::createQuestionBank(std::move(repo));
    ASSERT_TRUE(bank->load().ok());

    auto q = baseQuestion("Q_LOCK");
    q.question_text = "changed";
    EXPECT_EQ(bank->updateQuestion(instructorSession(), q).code, quizsphere::ErrorCode::QUESTION_IN_USE);
    EXPECT_EQ(bank->deleteQuestion(instructorSession(), "Q_LOCK", true).code, quizsphere::ErrorCode::QUESTION_IN_USE);

    repo_ptr->referenced_.clear();
    repo_ptr->fail_save_ = true;
    q.question_text = "retry";
    EXPECT_EQ(bank->updateQuestion(instructorSession(), q).code, quizsphere::ErrorCode::IO_ERROR);

    quizsphere::Question loaded;
    ASSERT_EQ(bank->findQuestionById(instructorSession(), "Q_LOCK", &loaded).code, quizsphere::ErrorCode::OK);
    EXPECT_EQ(loaded.question_text, "What is 2+2?");
}

TEST_F(QuestionBankFixture, UT_QB_08_ImportMixedRows) {
    auto seed = baseQuestion("Q_EXIST");
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), seed).ok());

    const std::string csv_path = (root_ / "mixed.csv").string();
    std::string csv =
        "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available\n"
        "Q1,Math,Easy,MCQ,One,1|2,0,true\n"
        "Q2,Math,Medium,MCQ,Two,1|2,1,true\n"
        "Q3,Math,Hard,MCQ,Three,1|2,1,true\n"
        "Q4,Science,Easy,MCQ,Four,1|2,0,true\n"
        "Q5,Science,Medium,MCQ,Five,1|2,1,true\n"
        "Q6,Science,Hard,MCQ,Six,1|2,0,true\n"
        "Q7,Math,Easy,MCQ,Seven,1|2,0,true\n"
        "Q_EXIST,Math,Easy,MCQ,dup,1|2,0,true\n"
        "Q8,Math,Easy,MCQ,bad correct,1|2,x,true\n"
        "Q9,Math,Easy,MCQ,Nine,1|2,0,true\n";
    ASSERT_EQ(quizsphere::createFileQuestionRepository(storage_path_)->writeTextFileAtomic(csv_path, csv, true).code,
              quizsphere::ErrorCode::OK);

    const auto report = bank_->importFromCsv(instructorSession(), csv_path);
    EXPECT_EQ(report.result.code, quizsphere::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, 8U);
    EXPECT_EQ(report.skipped_count, 2U);
    EXPECT_EQ(report.invalid_rows.size(), 2U);
}

TEST_F(QuestionBankFixture, UT_QB_09_ImportStructureRules_EmptyWrongHeaderWrongColumnsMalformedQuoting) {
    auto repo = quizsphere::createFileQuestionRepository(storage_path_);

    const std::string empty_path = (root_ / "empty.csv").string();
    ASSERT_EQ(repo->writeTextFileAtomic(empty_path, "", true).code, quizsphere::ErrorCode::OK);
    auto report = bank_->importFromCsv(instructorSession(), empty_path);
    EXPECT_EQ(report.result.code, quizsphere::ErrorCode::CSV_STRUCTURE_INVALID);
    EXPECT_EQ(report.imported_count, 0U);
    EXPECT_EQ(report.skipped_count, 0U);

    const std::string header_path = (root_ / "bad_header.csv").string();
    ASSERT_EQ(repo->writeTextFileAtomic(header_path, "a,b,c\n", true).code, quizsphere::ErrorCode::OK);
    report = bank_->importFromCsv(instructorSession(), header_path);
    EXPECT_EQ(report.result.code, quizsphere::ErrorCode::CSV_STRUCTURE_INVALID);

    const std::string row_path = (root_ / "bad_row.csv").string();
    ASSERT_EQ(repo->writeTextFileAtomic(
                  row_path,
                  "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available\n"
                  "Q1,Math,Easy,MCQ,ok,1|2,0,true\n"
                  "Q2,Math\n"
                  "Q3,Math,Easy,MCQ,bad \"quote,1|2,0,true\n",
                  true)
                  .code,
              quizsphere::ErrorCode::OK);
    report = bank_->importFromCsv(instructorSession(), row_path);
    EXPECT_EQ(report.result.code, quizsphere::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, 1U);
    EXPECT_EQ(report.skipped_count, 2U);
}

TEST_F(QuestionBankFixture, ST_SEC_05_BankUnchangedAfterRejectedImportAndPersistFailureKeepsOriginal) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_ORIG")).ok());
    auto repo = quizsphere::createFileQuestionRepository(storage_path_);

    std::string before;
    ASSERT_EQ(repo->readTextFile(storage_path_, &before).code, quizsphere::ErrorCode::OK);

    const std::string wrong_header_path = (root_ / "wrong.csv").string();
    ASSERT_EQ(repo->writeTextFileAtomic(wrong_header_path, "wrong,header\n", true).code, quizsphere::ErrorCode::OK);
    const auto report = bank_->importFromCsv(instructorSession(), wrong_header_path);
    ASSERT_EQ(report.result.code, quizsphere::ErrorCode::CSV_STRUCTURE_INVALID);

    std::string after;
    ASSERT_EQ(repo->readTextFile(storage_path_, &after).code, quizsphere::ErrorCode::OK);
    EXPECT_EQ(before, after);

    const auto ro_dir = root_ / "readonly";
    std::filesystem::create_directories(ro_dir);
    const auto ro_path = ro_dir / "questions.tsv";
    auto ro_bank = quizsphere::createQuestionBank(quizsphere::createFileQuestionRepository(ro_path.string()));
    ASSERT_TRUE(ro_bank->load().ok());
    ASSERT_TRUE(ro_bank->addQuestion(instructorSession(), baseQuestion("Q_R")).ok());

    std::string original;
    ASSERT_EQ(quizsphere::createFileQuestionRepository(ro_path.string())->readTextFile(ro_path.string(), &original).code,
              quizsphere::ErrorCode::OK);

    std::filesystem::permissions(ro_dir,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);
    auto q = baseQuestion("Q_R");
    q.question_text = "will fail";
    const auto update_result = ro_bank->updateQuestion(instructorSession(), q);
    EXPECT_EQ(update_result.code, quizsphere::ErrorCode::IO_ERROR);
    std::filesystem::permissions(ro_dir,
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);

    std::string unchanged;
    ASSERT_EQ(quizsphere::createFileQuestionRepository(ro_path.string())->readTextFile(ro_path.string(), &unchanged).code,
              quizsphere::ErrorCode::OK);
    EXPECT_EQ(original, unchanged);
}

TEST_F(QuestionBankFixture, UT_QB_10_BOM_CRLF_MultilineAndFormulaRoundTrip) {
    const std::string csv_path = (root_ / "bom.csv").string();
    std::string csv =
        "\xEF\xBB\xBFquestion_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available\r\n"
        "QF,""=Math"",Easy,MCQ,""Line1\nLine2"",""'+one|two"",0,true\r\n";
    ASSERT_EQ(quizsphere::createFileQuestionRepository(storage_path_)->writeTextFileAtomic(csv_path, csv, true).code,
              quizsphere::ErrorCode::OK);

    const auto report = bank_->importFromCsv(instructorSession(), csv_path);
    ASSERT_EQ(report.result.code, quizsphere::ErrorCode::OK);
    ASSERT_EQ(report.imported_count, 1U);

    const std::string out_path = (root_ / "formula.csv").string();
    ASSERT_EQ(bank_->exportToCsv(instructorSession(), out_path, std::nullopt, std::nullopt, true).code,
              quizsphere::ErrorCode::OK);

    std::string out;
    ASSERT_EQ(quizsphere::createFileQuestionRepository(storage_path_)->readTextFile(out_path, &out).code,
              quizsphere::ErrorCode::OK);
    EXPECT_NE(out.find("'""=Math"""), std::string::npos);
}

TEST_F(QuestionBankFixture, UT_QB_11_ValidationLimitsControlCharsAndDuplicates) {
    auto q = baseQuestion("Q_LIMIT");
    q.question_id = std::string(37, 'a');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, quizsphere::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("Q_LIMIT2");
    q.subject_tag = std::string(41, 'b');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, quizsphere::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("Q_LIMIT3");
    q.question_text = std::string(501, 'c');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, quizsphere::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("Q_LIMIT4");
    q.options = {"dup", " DUP "};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, quizsphere::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("Q_LIMIT5");
    q.question_text = "ok\x01bad";
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, quizsphere::ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuestionBankFixture, SEC_RoleDenial_AllFunctions_StudentAndInvalidSession) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_SEC")).ok());

    std::vector<quizsphere::Question> listed;
    quizsphere::Question found;
    std::vector<quizsphere::PublicQuestion> pub;

    EXPECT_EQ(bank_->addQuestion(studentSession(), baseQuestion("Q_STU_ADD")).code, quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->updateQuestion(studentSession(), baseQuestion("Q_SEC")).code, quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->deleteQuestion(studentSession(), "Q_SEC", true).code, quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->setAvailability(studentSession(), "Q_SEC", false).code, quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->findQuestionById(studentSession(), "Q_SEC", &found).code, quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->listQuestions(studentSession(), &listed, false, std::nullopt, std::nullopt).code,
              quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->exportToCsv(studentSession(), (root_ / "x.csv").string(), std::nullopt, std::nullopt, true).code,
              quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->importFromCsv(studentSession(), (root_ / "x.csv").string()).result.code,
              quizsphere::ErrorCode::NOT_AUTHORIZED);

    EXPECT_EQ(bank_->addQuestion(invalidSession(), baseQuestion("Q_INV")).code, quizsphere::ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank_->findQuestionById(invalidSession(), "Q_SEC", &found).code, quizsphere::ErrorCode::NOT_AUTHORIZED);

    EXPECT_EQ(bank_->getQuestionForQuiz("Q_SEC", &found).code, quizsphere::ErrorCode::OK);
    EXPECT_EQ(bank_->listAvailable(&pub, std::nullopt, std::nullopt).code, quizsphere::ErrorCode::OK);
}

TEST_F(QuestionBankFixture, MenuTests_DeleteConfirmationEofAndInvalidOption) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_MENU")).ok());

    std::istringstream scripted_input("9\n5\nQ_MENU\nno\n0\n");
    std::ostringstream output;
    auto menu = quizsphere::createConsoleMenu(instructorSession(), "Instructor", *bank_, scripted_input, output);
    menu->run();
    const std::string text = output.str();
    EXPECT_NE(text.find("invalid option"), std::string::npos);
    EXPECT_NE(text.find("confirmation required"), std::string::npos);

    std::istringstream eof_input("");
    std::ostringstream eof_output;
    auto eof_menu = quizsphere::createConsoleMenu(instructorSession(), "Instructor", *bank_, eof_input, eof_output);
    eof_menu->run();
    EXPECT_NE(eof_output.str().find("Input ended"), std::string::npos);
}

TEST_F(QuestionBankFixture, ImportLogsRejectedRows) {
    const std::string csv_path = (root_ / "log.csv").string();
    std::string csv =
        "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available\n"
        "Q1,Math,Easy,MCQ,ok,1|2,0,true\n"
        "Q2,Math,Easy,MCQ,bad,1|2,x,true\n"
        "Q3,Math,Easy,MCQ,bad2,1|2,x,true\n";
    ASSERT_EQ(quizsphere::createFileQuestionRepository(storage_path_)->writeTextFileAtomic(csv_path, csv, true).code,
              quizsphere::ErrorCode::OK);

    const auto report = bank_->importFromCsv(instructorSession(), csv_path);
    ASSERT_EQ(report.skipped_count, 2U);

    std::string log_content;
    ASSERT_EQ(quizsphere::createFileQuestionRepository(storage_path_)
                  ->readTextFile("logs/question_import.log", &log_content)
                  .code,
              quizsphere::ErrorCode::OK);
    EXPECT_NE(log_content.find("row 3"), std::string::npos);
    EXPECT_NE(log_content.find("row 4"), std::string::npos);
    EXPECT_NE(log_content.find("|"), std::string::npos);
}

}  // namespace
