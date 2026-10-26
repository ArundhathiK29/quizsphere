#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "common_types.hpp"
#include "console_menu.hpp"
#include "import_logger.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"

namespace qs = quizsphere;
namespace fs = std::filesystem;

namespace {

const std::string kHeader =
    "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available";

// ------------------------------------------------------------------ helpers

fs::path uniqueTempDir() {
    static std::uint64_t counter = 0;
    std::random_device rd;
    const std::string token = std::to_string(static_cast<unsigned long long>(rd())) + "_" + std::to_string(++counter);
    const fs::path path = fs::temp_directory_path() / ("quizsphere_tests_" + token);
    fs::create_directories(path);
    return path;
}

qs::Session instructorSession() { return {"instructor-1", qs::Role::Instructor, "2026-10-24T00:00:00Z"}; }
qs::Session adminSession() { return {"admin-1", qs::Role::Admin, "2026-10-24T00:00:00Z"}; }
qs::Session studentSession() { return {"student-1", qs::Role::Student, "2026-10-24T00:00:00Z"}; }
qs::Session invalidSession() { return {"", qs::Role::Instructor, ""}; }

qs::Question baseQuestion(const std::string& id = "Q1") {
    return qs::Question{id, "Math", qs::Difficulty::Easy, qs::QuestionType::MCQ, "What is 2+2?", {"3", "4"}, 1, true};
}

// File access in tests goes through the repository too (the project forbids <fstream> outside the
// repository/logging layer, see cmake/check_no_fstream.cmake).
std::string readFile(const fs::path& path) {
    std::string content;
    if (!qs::createFileQuestionRepository("unused.tsv")->readTextFile(path.string(), &content).ok()) {
        return std::string();
    }
    return content;
}

void writeFile(const fs::path& path, const std::string& content) {
    const qs::Result written = qs::createFileQuestionRepository("unused.tsv")->writeTextFileAtomic(path.string(), content, true);
    EXPECT_TRUE(written.ok()) << written.message;
}

std::size_t countLines(const std::string& text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

std::string csvRow(const std::string& id, const std::string& subject, const std::string& difficulty,
                   const std::string& text, const std::string& options, const std::string& correct,
                   const std::string& available = "true") {
    return id + "," + subject + "," + difficulty + ",MCQ," + text + "," + options + "," + correct + "," + available + "\n";
}

class RecordingLogger final : public qs::ImportLogger {
public:
    bool logRejectedRow(std::size_t row_number, const std::string& reason) override {
        rows.emplace_back(row_number, reason);
        return !fail;
    }
    std::vector<std::pair<std::size_t, std::string>> rows;
    bool fail{false};
};

// Shares state with the test so it can inspect what the bank stored.
struct SharedState {
    std::map<std::string, std::string> files;
    std::vector<qs::Question> stored;
    std::unordered_set<std::string> referenced;
    bool fail_save{false};
};

class InMemoryRepository final : public qs::QuestionRepository {
public:
    explicit InMemoryRepository(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}

    qs::Result loadAll(std::vector<qs::Question>* questions) override {
        *questions = state_->stored;
        return {qs::ErrorCode::OK, "ok"};
    }
    qs::Result saveAll(const std::vector<qs::Question>& questions) override {
        if (state_->fail_save) {
            return {qs::ErrorCode::IO_ERROR, "save failed"};
        }
        state_->stored = questions;
        return {qs::ErrorCode::OK, "ok"};
    }
    qs::Result readTextFile(const std::string& path, std::string* content) const override {
        const auto it = state_->files.find(path);
        if (it == state_->files.end()) {
            return {qs::ErrorCode::IO_ERROR, "missing"};
        }
        *content = it->second;
        return {qs::ErrorCode::OK, "ok"};
    }
    qs::Result writeTextFileAtomic(const std::string& path, const std::string& content, bool overwrite) const override {
        if (state_->files.count(path) != 0 && !overwrite) {
            return {qs::ErrorCode::CONFIRMATION_REQUIRED, "overwrite required"};
        }
        state_->files[path] = content;
        return {qs::ErrorCode::OK, "ok"};
    }
    [[nodiscard]] bool isReferenced(const std::string& question_id) const override {
        return state_->referenced.count(question_id) != 0;
    }

private:
    std::shared_ptr<SharedState> state_;
};

std::unique_ptr<qs::QuestionBank> makeFileBank(const fs::path& storage, const fs::path& log) {
    return qs::createQuestionBank(qs::createFileQuestionRepository(storage.string()),
                                  qs::createFileImportLogger(log.string()));
}

class QuestionBankFixture : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = uniqueTempDir();
        storage_path_ = root_ / "questions.tsv";
        log_path_ = root_ / "logs" / "import.log";
        bank_ = makeFileBank(storage_path_, log_path_);
        ASSERT_TRUE(bank_->load().ok());
    }
    void TearDown() override {
        std::error_code ec;
        fs::permissions(root_ / "readonly", fs::perms::owner_all, fs::perm_options::replace, ec);
        fs::remove_all(root_, ec);
    }

    std::unique_ptr<qs::QuestionBank> reload() {
        auto bank = makeFileBank(storage_path_, log_path_);
        EXPECT_TRUE(bank->load().ok());
        return bank;
    }
    qs::ImportReport importCsv(const std::string& content, const std::string& name = "in.csv") {
        const fs::path csv = root_ / name;
        writeFile(csv, content);
        return bank_->importFromCsv(instructorSession(), csv.string());
    }
    std::vector<qs::Question> listAll(qs::QuestionBank& bank) {
        std::vector<qs::Question> out;
        EXPECT_TRUE(bank.listQuestions(instructorSession(), &out, false, std::nullopt, std::nullopt).ok());
        return out;
    }

    fs::path root_;
    fs::path storage_path_;
    fs::path log_path_;
    std::unique_ptr<qs::QuestionBank> bank_;
};

// ------------------------------------------------------------------ Test Plan IDs UT-QB-01..11

TEST_F(QuestionBankFixture, UT_QB_01_AddPersistReload) {
    ASSERT_EQ(bank_->addQuestion(instructorSession(), baseQuestion("Q_PERSIST")).code, qs::ErrorCode::OK);

    auto reloaded = reload();
    qs::Question found;
    ASSERT_EQ(reloaded->findQuestionById(instructorSession(), "Q_PERSIST", &found).code, qs::ErrorCode::OK);
    EXPECT_EQ(found.subject_tag, "Math");
    EXPECT_EQ(found.difficulty, qs::Difficulty::Easy);
    EXPECT_EQ(found.options.size(), 2U);
    EXPECT_EQ(found.correct_option, 1U);
    EXPECT_TRUE(found.is_available);
}

TEST_F(QuestionBankFixture, UT_QB_02_EditQuestion) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_EDIT")).ok());

    auto edited = baseQuestion("Q_EDIT");
    edited.question_text = "What is 3+3?";
    edited.options = {"5", "6", "7"};
    edited.correct_option = 1;
    edited.difficulty = qs::Difficulty::Hard;
    ASSERT_EQ(bank_->updateQuestion(instructorSession(), edited).code, qs::ErrorCode::OK);

    auto reloaded = reload();
    qs::Question found;
    ASSERT_TRUE(reloaded->findQuestionById(instructorSession(), "Q_EDIT", &found).ok());
    EXPECT_EQ(found.question_text, "What is 3+3?");
    EXPECT_EQ(found.options.size(), 3U);
    EXPECT_EQ(found.difficulty, qs::Difficulty::Hard);

    EXPECT_EQ(bank_->updateQuestion(instructorSession(), baseQuestion("NOPE")).code, qs::ErrorCode::QUESTION_NOT_FOUND);
}

TEST_F(QuestionBankFixture, UT_QB_03_DeleteWithAndWithoutConfirmation) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_DEL")).ok());

    EXPECT_EQ(bank_->deleteQuestion(instructorSession(), "Q_DEL", false).code, qs::ErrorCode::CONFIRMATION_REQUIRED);
    EXPECT_EQ(listAll(*bank_).size(), 1U);

    EXPECT_EQ(bank_->deleteQuestion(instructorSession(), "Q_DEL", true).code, qs::ErrorCode::OK);
    EXPECT_TRUE(listAll(*bank_).empty());
    EXPECT_TRUE(listAll(*reload()).empty());

    EXPECT_EQ(bank_->deleteQuestion(instructorSession(), "Q_DEL", true).code, qs::ErrorCode::QUESTION_NOT_FOUND);
}

TEST_F(QuestionBankFixture, UT_QB_04_InvalidOrMissingCorrectAnswer) {
    auto q = baseQuestion("Q_COR");
    q.correct_option = q.options.size();
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
    q.correct_option = static_cast<std::size_t>(-1);
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
    EXPECT_TRUE(listAll(*bank_).empty());
}

TEST_F(QuestionBankFixture, UT_QB_05_MissingSubjectTag) {
    auto q = baseQuestion("Q_SUB");
    q.subject_tag.clear();
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
    q.subject_tag = "   ";
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
    EXPECT_TRUE(listAll(*bank_).empty());
}

TEST_F(QuestionBankFixture, UT_QB_06_FewerThanTwoOptions) {
    auto q = baseQuestion("Q_OPT");
    q.options = {"only one"};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
    q.options.clear();
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuestionBankFixture, UT_QB_07_ValidCsvImport) {
    const auto report = importCsv(kHeader + "\n" + csvRow("Q1", "Math", "Easy", "One", "1|2", "0") +
                                  csvRow("Q2", "Science", "Medium", "Two", "a|b|c", "2") +
                                  csvRow("Q3", "Math", "Hard", "Three", "x|y", "1", "false"));
    ASSERT_EQ(report.result.code, qs::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, 3U);
    EXPECT_EQ(report.skipped_count, 0U);
    EXPECT_EQ(listAll(*reload()).size(), 3U);
}

TEST_F(QuestionBankFixture, UT_QB_08_CsvWithTwoInvalidRowsOfTen) {
    std::string csv = kHeader + "\n";
    for (int i = 1; i <= 10; ++i) {
        const std::string id = "Q" + std::to_string(i);
        if (i == 4) {
            csv += csvRow(id, "Math", "Easy", "bad correct", "1|2", "x");
        } else if (i == 7) {
            csv += csvRow(id, "Math", "Impossible", "bad difficulty", "1|2", "0");
        } else {
            csv += csvRow(id, "Math", "Easy", "ok " + id, "1|2", "0");
        }
    }
    const auto report = importCsv(csv);
    ASSERT_EQ(report.result.code, qs::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, 8U);
    EXPECT_EQ(report.skipped_count, 2U);
    ASSERT_EQ(report.invalid_rows.size(), 2U);
    EXPECT_EQ(report.invalid_rows[0].row_number, 5U);  // header is line 1, Q4 is line 5
    EXPECT_EQ(report.invalid_rows[1].row_number, 8U);
    EXPECT_EQ(listAll(*bank_).size(), 8U);
}

TEST_F(QuestionBankFixture, UT_QB_09_ExportFullAndFiltered) {
    auto q1 = baseQuestion("E1");
    auto q2 = baseQuestion("E2");
    q2.difficulty = qs::Difficulty::Hard;
    auto q3 = baseQuestion("E3");
    q3.subject_tag = "Science";
    auto q4 = baseQuestion("E4");
    q4.subject_tag = "Science";
    q4.difficulty = qs::Difficulty::Hard;
    for (const auto& q : {q1, q2, q3, q4}) {
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), q).ok());
    }

    const auto exportRows = [&](const std::optional<std::string>& subject,
                                const std::optional<qs::Difficulty>& difficulty) {
        static int export_counter = 0;
        const std::string out = (root_ / ("out_" + std::to_string(++export_counter) + ".csv")).string();
        EXPECT_EQ(bank_->exportToCsv(instructorSession(), out, subject, difficulty, false).code, qs::ErrorCode::OK);
        const std::string content = readFile(out);
        EXPECT_EQ(content.rfind(kHeader + "\n", 0), 0U);
        return countLines(content) - 1;
    };

    EXPECT_EQ(exportRows(std::nullopt, std::nullopt), 4U);
    EXPECT_EQ(exportRows(std::string("math"), std::nullopt), 2U);
    EXPECT_EQ(exportRows(std::nullopt, qs::Difficulty::Hard), 2U);
    EXPECT_EQ(exportRows(std::string("MATH"), qs::Difficulty::Hard), 1U);
}

TEST_F(QuestionBankFixture, UT_QB_10_DisableViaSetAvailability) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_DIS")).ok());
    ASSERT_EQ(bank_->setAvailability(instructorSession(), "Q_DIS", false).code, qs::ErrorCode::OK);

    std::vector<qs::PublicQuestion> available;
    ASSERT_TRUE(bank_->listAvailable(&available, std::nullopt, std::nullopt).ok());
    EXPECT_TRUE(available.empty());

    const auto all = listAll(*bank_);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_FALSE(all[0].is_available);
    EXPECT_FALSE(listAll(*reload())[0].is_available);

    ASSERT_EQ(bank_->setAvailability(instructorSession(), "Q_DIS", true).code, qs::ErrorCode::OK);
    ASSERT_TRUE(bank_->listAvailable(&available, std::nullopt, std::nullopt).ok());
    EXPECT_EQ(available.size(), 1U);
}

TEST_F(QuestionBankFixture, UT_QB_11_RejectedRowLoggingWithTimestampAndReason) {
    const auto report = importCsv(kHeader + "\n" + csvRow("Q1", "Math", "Easy", "ok", "1|2", "0") +
                                  csvRow("Q2", "Math", "Easy", "bad", "1|2", "x") +
                                  csvRow("Q3", "Math", "Nope", "bad", "1|2", "0"));
    ASSERT_EQ(report.skipped_count, 2U);
    EXPECT_FALSE(report.log_failed);

    const std::string log = readFile(log_path_);
    ASSERT_EQ(countLines(log), 2U);
    const std::regex line_format(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z \| row (\d+) \| (.+))");
    std::istringstream lines(log);
    std::string line;
    std::vector<std::string> rows;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // the log is written in text mode: CRLF on Windows
        }
        std::smatch match;
        ASSERT_TRUE(std::regex_match(line, match, line_format)) << line;
        rows.push_back(match[1]);
    }
    EXPECT_EQ(rows[0], "3");
    EXPECT_EQ(rows[1], "4");
}

// ------------------------------------------------------------------ extra behaviour

TEST_F(QuestionBankFixture, EXTRA_DuplicateIdRejected) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("DUP")).ok());
    EXPECT_EQ(bank_->addQuestion(instructorSession(), baseQuestion("DUP")).code, qs::ErrorCode::DUPLICATE_ID);
    EXPECT_EQ(listAll(*bank_).size(), 1U);
}

TEST_F(QuestionBankFixture, EXTRA_IdsAndSubjectsAreNormalisedAndMatchedCaseInsensitively) {
    auto q = baseQuestion("  Q_N  ");
    q.subject_tag = "Math  ";
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), q).ok());
    EXPECT_EQ(bank_->addQuestion(instructorSession(), baseQuestion("Q_N")).code, qs::ErrorCode::DUPLICATE_ID);

    qs::Question found;
    ASSERT_TRUE(bank_->findQuestionById(instructorSession(), " Q_N ", &found).ok());
    EXPECT_EQ(found.question_id, "Q_N");
    EXPECT_EQ(found.subject_tag, "Math");

    std::vector<qs::PublicQuestion> out;
    ASSERT_TRUE(bank_->listAvailable(&out, std::string(" mAtH "), std::nullopt).ok());
    EXPECT_EQ(out.size(), 1U);
    ASSERT_TRUE(bank_->listAvailable(&out, std::string("Science"), std::nullopt).ok());
    EXPECT_TRUE(out.empty());
}

TEST_F(QuestionBankFixture, EXTRA_ListAvailableFilterCombinations) {
    auto a = baseQuestion("A");  // Math, Easy, available
    auto b = baseQuestion("B");  // Science, Medium, available
    b.subject_tag = "Science";
    b.difficulty = qs::Difficulty::Medium;
    auto c = baseQuestion("C");  // math, Medium, DISABLED
    c.subject_tag = "math";
    c.difficulty = qs::Difficulty::Medium;
    c.is_available = false;
    auto d = baseQuestion("D");  // Math, Medium, available
    d.difficulty = qs::Difficulty::Medium;
    for (const auto& q : {a, b, c, d}) {
        ASSERT_TRUE(bank_->addQuestion(instructorSession(), q).ok());
    }

    const auto count = [&](const std::optional<std::string>& subject, const std::optional<qs::Difficulty>& diff) {
        std::vector<qs::PublicQuestion> out;
        EXPECT_TRUE(bank_->listAvailable(&out, subject, diff).ok());
        return out.size();
    };
    EXPECT_EQ(count(std::nullopt, std::nullopt), 3U);                                   // A, B, D
    EXPECT_EQ(count(std::string("math"), std::nullopt), 2U);                            // A, D (C disabled)
    EXPECT_EQ(count(std::nullopt, qs::Difficulty::Medium), 2U);                         // B, D
    EXPECT_EQ(count(std::string("MATH"), qs::Difficulty::Medium), 1U);                  // D
    EXPECT_EQ(count(std::string("Science"), qs::Difficulty::Easy), 0U);
}

TEST_F(QuestionBankFixture, EXTRA_ValidationLimitsCountCharactersNotBytes) {
    auto q = baseQuestion("L1");
    q.question_id = std::string(36, 'a');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::OK);
    q = baseQuestion(std::string(37, 'a'));
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("L2");
    q.subject_tag = std::string(41, 'b');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    std::string accented40;
    std::string accented41;
    for (int i = 0; i < 40; ++i) {
        accented40 += "\xC3\xA9";
    }
    accented41 = accented40 + "\xC3\xA9";
    q = baseQuestion("L3");
    q.subject_tag = accented40;  // 40 characters, 80 bytes
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::OK);
    q = baseQuestion("L4");
    q.subject_tag = accented41;
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("L5");
    q.question_text = std::string(500, 'c');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::OK);
    q = baseQuestion("L6");
    q.question_text = std::string(501, 'c');
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("L7");
    q.options = {std::string(200, 'o'), "x"};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::OK);
    q = baseQuestion("L8");
    q.options = {std::string(201, 'o'), "x"};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("L9");
    q.subject_tag = std::string("bad") + '\xC3';  // truncated UTF-8 sequence
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuestionBankFixture, EXTRA_ControlCharactersAndDuplicateOptionsRejected) {
    auto q = baseQuestion("C1");
    q.subject_tag = std::string("a") + '\t' + "b";
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("C2");
    q.options = {std::string("a") + '\n' + "b", "x"};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion(std::string("C") + '\x01' + "3");
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("C4");
    q.question_text = std::string("ok") + '\x01' + "bad";
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("C5");
    q.question_text = "line1\nline2\ttabbed\r\nline3";  // newline and tab are allowed in question text
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::OK);

    q = baseQuestion("C6");
    q.options = {"dup", " DUP "};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);

    q = baseQuestion("C7");
    q.options = {"ok", "   "};
    EXPECT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::VALIDATION_FAILED);
}

// Special characters must survive TSV (save + load) and CSV (export + import) exactly.
TEST_F(QuestionBankFixture, EXTRA_SpecialCharactersRoundTripTsvAndCsv) {
    std::string accented = "caf";
    accented += "\xC3\xA9";
    const std::vector<std::string> texts = {
        "line1\nline2", "carriage\rreturn", "tab\tvalue", "back\\slash", "pipe|value", "escaped\\|pipe",
        "\"quoted\"", "comma,value", accented, "  spaced  ", "'; DROP TABLE questions;--", "=1+1", "'=x",
        "C:\\new\\table\\run", "ends with backslash\\", "two\\\\slashes", "literal \\n \\t \\r text"};
    const std::vector<std::string> shorts = {
        "back\\slash", "pipe|value", "escaped\\|pipe", "\"quoted\"", "comma,value", accented,
        "in ner", "'; DROP TABLE t;--", "=SUM(A1)", "'+x", "@home", "-5", "''=y",
        "C:\\new\\table", "x\\", "\\\\", "\\n\\t\\r"};

    std::vector<qs::Question> expected;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        auto q = baseQuestion("Q_SP_" + std::to_string(i));
        q.subject_tag = shorts[i];
        q.question_text = texts[i];
        q.options = {shorts[(i + 1) % shorts.size()], "zz-other"};
        ASSERT_EQ(bank_->addQuestion(instructorSession(), q).code, qs::ErrorCode::OK) << "sample " << i;
        expected.push_back(q);
    }

    const auto check = [&](const qs::QuestionBank& bank, const char* label) {
        for (const auto& want : expected) {
            qs::Question got;
            ASSERT_EQ(bank.findQuestionById(instructorSession(), want.question_id, &got).code, qs::ErrorCode::OK)
                << label << " " << want.question_id;
            EXPECT_EQ(got.subject_tag, want.subject_tag) << label << " " << want.question_id;
            EXPECT_EQ(got.question_text, want.question_text) << label << " " << want.question_id;
            EXPECT_EQ(got.options, want.options) << label << " " << want.question_id;
        }
    };

    check(*reload(), "TSV");

    const fs::path csv = root_ / "special.csv";
    ASSERT_EQ(bank_->exportToCsv(instructorSession(), csv.string(), std::nullopt, std::nullopt, false).code,
              qs::ErrorCode::OK);
    auto csv_bank = makeFileBank(root_ / "csv.tsv", root_ / "csv.log");
    ASSERT_TRUE(csv_bank->load().ok());
    const auto report = csv_bank->importFromCsv(instructorSession(), csv.string());
    ASSERT_EQ(report.result.code, qs::ErrorCode::OK);
    ASSERT_EQ(report.skipped_count, 0U);
    EXPECT_EQ(report.imported_count, expected.size());
    check(*csv_bank, "CSV");
}

TEST_F(QuestionBankFixture, EXTRA_FormulaInjectionIsNeutralisedOnExportAndLosslessOnImport) {
    auto q = baseQuestion("F1");
    q.subject_tag = "=cmd";
    q.question_text = "+1+1";
    q.options = {"@sum", "-2"};
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), q).ok());

    const fs::path out = root_ / "formula.csv";
    ASSERT_TRUE(bank_->exportToCsv(instructorSession(), out.string(), std::nullopt, std::nullopt, false).ok());
    const std::string content = readFile(out);
    EXPECT_NE(content.find(",'=cmd,"), std::string::npos);
    EXPECT_NE(content.find(",'+1+1,"), std::string::npos);
    EXPECT_NE(content.find(",'@sum|-2,"), std::string::npos);
}

TEST_F(QuestionBankFixture, EXTRA_CsvBomCrlfMultilineAndFormulaImport) {
    const std::string csv = std::string("\xEF\xBB\xBF") + kHeader + "\r\n" +
                            "QF,\"=Math\",Easy,MCQ,\"Line1\nLine2\",\"'+one|two\",0,true\r\n";
    const auto report = importCsv(csv, "bom.csv");
    ASSERT_EQ(report.result.code, qs::ErrorCode::OK);
    ASSERT_EQ(report.imported_count, 1U);
    ASSERT_EQ(report.skipped_count, 0U);

    qs::Question found;
    ASSERT_TRUE(bank_->findQuestionById(instructorSession(), "QF", &found).ok());
    EXPECT_EQ(found.subject_tag, "=Math");
    EXPECT_EQ(found.question_text, "Line1\nLine2");
    ASSERT_EQ(found.options.size(), 2U);
    EXPECT_EQ(found.options[0], "+one");
    EXPECT_EQ(found.options[1], "two");
}

TEST_F(QuestionBankFixture, ST_SEC_05_StructuralCsvFailuresRejectWholeFileAndLeaveBankIdentical) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_ORIG")).ok());
    const std::string before = readFile(storage_path_);

    const auto expectRejected = [&](const std::string& content, const char* label) {
        const auto report = importCsv(content, "structural.csv");
        EXPECT_EQ(report.result.code, qs::ErrorCode::CSV_STRUCTURE_INVALID) << label;
        EXPECT_EQ(report.imported_count, 0U) << label;
        EXPECT_EQ(report.skipped_count, 0U) << label;
        EXPECT_EQ(readFile(storage_path_), before) << label;
    };

    expectRejected("", "empty file");
    expectRejected("\n\n   \n", "blank file");
    expectRejected("a,b,c\n", "wrong header");
    expectRejected(kHeader + ",extra\n", "header with extra column");
    expectRejected("question_id,subject_tag\n" + csvRow("Q9", "Math", "Easy", "x", "1|2", "0"), "short header");
    expectRejected(std::string(10U * 1024U * 1024U + 1U, 'x'), "file over 10 MB");

    const auto missing = bank_->importFromCsv(instructorSession(), (root_ / "does_not_exist.csv").string());
    EXPECT_EQ(missing.result.code, qs::ErrorCode::CSV_STRUCTURE_INVALID);
    EXPECT_EQ(readFile(storage_path_), before);

    const auto header_only = importCsv(kHeader + "\n", "header_only.csv");
    EXPECT_EQ(header_only.result.code, qs::ErrorCode::OK);
    EXPECT_EQ(header_only.imported_count, 0U);
    EXPECT_EQ(header_only.skipped_count, 0U);
}

TEST_F(QuestionBankFixture, EXTRA_CsvRowLevelProblemsAreSkippedAndReported) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_EXIST")).ok());
    const auto report = importCsv(kHeader + "\n" + csvRow("A1", "Math", "Easy", "ok", "1|2", "0") +
                                  "A2,Math\n" +                                                  // wrong column count
                                  csvRow("A3", "Math", "Easy", "bad \"quote", "1|2", "0") +      // quote inside unquoted text
                                  csvRow("A1", "Math", "Easy", "dup in file", "1|2", "0") +      // duplicate in file
                                  csvRow("Q_EXIST", "Math", "Easy", "dup in bank", "1|2", "0") + // duplicate existing
                                  csvRow("A4", "Math", "Easy", "bad flag", "1|2", "0", "maybe") +
                                  csvRow("A5", "Math", "Easy", "neg", "1|2", "-1") +
                                  csvRow("A6", "Math", "Easy", "same options", "x|X", "0") +
                                  csvRow("A7", "Math", "Easy", "fine", "1|2", "1"));
    ASSERT_EQ(report.result.code, qs::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, 2U);  // A1 and A7
    EXPECT_EQ(report.skipped_count, 7U);
    EXPECT_EQ(listAll(*bank_).size(), 3U);
}

TEST_F(QuestionBankFixture, EXTRA_UnterminatedQuoteOnlyAffectsItsOwnRecord) {
    const auto report = importCsv(kHeader + "\n" + csvRow("Q1", "Math", "Easy", "ok", "1|2", "0") +
                                  "Q2,Math,Easy,MCQ,\"oops,1|2,0,true\n" +
                                  csvRow("Q3", "Math", "Easy", "ok3", "1|2", "0") +
                                  csvRow("Q4", "Math", "Easy", "ok4", "1|2", "0"));
    ASSERT_EQ(report.result.code, qs::ErrorCode::OK);
    EXPECT_EQ(report.imported_count, 3U);
    ASSERT_EQ(report.invalid_rows.size(), 1U);
    EXPECT_EQ(report.invalid_rows[0].row_number, 3U);
}

TEST_F(QuestionBankFixture, EXTRA_TextAfterClosingQuoteIsMalformed) {
    const auto report = importCsv(kHeader + "\n" + "Q1,Math,Easy,MCQ,\"abc\"def,1|2,0,true\n" +
                                  csvRow("Q2", "Math", "Easy", "ok", "1|2", "0"));
    EXPECT_EQ(report.imported_count, 1U);
    EXPECT_EQ(report.skipped_count, 1U);
}

TEST_F(QuestionBankFixture, EXTRA_RowNumbersArePhysicalLines) {
    const std::string csv = kHeader + "\n" +                                                  // line 1
                            "\n" +                                                            // line 2 blank
                            "Q1,Math,Easy,MCQ,\"two\nlines\",1|2,0,true\n" +                  // lines 3-4
                            csvRow("Q2", "Math", "Easy", "bad", "1|2", "x");                  // line 5
    const auto report = importCsv(csv);
    ASSERT_EQ(report.imported_count, 1U);
    ASSERT_EQ(report.invalid_rows.size(), 1U);
    EXPECT_EQ(report.invalid_rows[0].row_number, 5U);
}

TEST_F(QuestionBankFixture, EXTRA_LoadFailsOnMalformedStoredFileAndLeavesFileAlone) {
    const std::string good_header =
        "question_id\tsubject_tag\tdifficulty\tquestion_type\tquestion_text\toptions\tcorrect_option\tis_available\n";
    const std::vector<std::pair<std::string, std::string>> bad_files = {
        {"wrong header", "nope\n"},
        {"short row", good_header + "Q1\tMath\n"},
        {"bad difficulty", good_header + "Q1\tMath\tHardest\tMCQ\ttext\ta|b\t0\ttrue\n"},
        {"bad type", good_header + "Q1\tMath\tEasy\tTrueFalse\ttext\ta|b\t0\ttrue\n"},
        {"bad index", good_header + "Q1\tMath\tEasy\tMCQ\ttext\ta|b\tx\ttrue\n"},
        {"bad flag", good_header + "Q1\tMath\tEasy\tMCQ\ttext\ta|b\t0\tmaybe\n"},
        {"invalid question", good_header + "Q1\tMath\tEasy\tMCQ\ttext\tonly\t0\ttrue\n"},
        {"duplicate id", good_header + "Q1\tMath\tEasy\tMCQ\ta\tx|y\t0\ttrue\nQ1\tMath\tEasy\tMCQ\tb\tx|y\t0\ttrue\n"}};

    for (const auto& [label, content] : bad_files) {
        writeFile(storage_path_, content);
        auto bank = makeFileBank(storage_path_, log_path_);
        EXPECT_FALSE(bank->load().ok()) << label;
        EXPECT_TRUE(listAll(*bank).empty()) << label;
        EXPECT_EQ(readFile(storage_path_), content) << label;
    }
}

TEST_F(QuestionBankFixture, EXTRA_StorageFileWithCrlfLoads) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_CRLF")).ok());
    std::string content = readFile(storage_path_);
    std::string crlf;
    for (const char c : content) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    writeFile(storage_path_, crlf);
    EXPECT_EQ(listAll(*reload()).size(), 1U);
}

TEST_F(QuestionBankFixture, ST_SEC_RoleDenialOnEveryFunction) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_SEC")).ok());
    const std::string before = readFile(storage_path_);
    const fs::path csv = root_ / "x.csv";
    writeFile(csv, kHeader + "\n");

    qs::Question found;
    std::vector<qs::Question> listed;
    for (const qs::Session& denied : {studentSession(), invalidSession()}) {
        EXPECT_EQ(bank_->addQuestion(denied, baseQuestion("Q_NEW")).code, qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->updateQuestion(denied, baseQuestion("Q_SEC")).code, qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->deleteQuestion(denied, "Q_SEC", true).code, qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->setAvailability(denied, "Q_SEC", false).code, qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->findQuestionById(denied, "Q_SEC", &found).code, qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->listQuestions(denied, &listed, false, std::nullopt, std::nullopt).code,
                  qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->exportToCsv(denied, (root_ / "denied.csv").string(), std::nullopt, std::nullopt, true).code,
                  qs::ErrorCode::NOT_AUTHORIZED);
        EXPECT_EQ(bank_->importFromCsv(denied, csv.string()).result.code, qs::ErrorCode::NOT_AUTHORIZED);
    }
    EXPECT_EQ(readFile(storage_path_), before);
    EXPECT_FALSE(fs::exists(root_ / "denied.csv"));

    // Admin is allowed everywhere an Instructor is.
    EXPECT_EQ(bank_->addQuestion(adminSession(), baseQuestion("Q_ADMIN")).code, qs::ErrorCode::OK);
    EXPECT_EQ(bank_->findQuestionById(adminSession(), "Q_ADMIN", &found).code, qs::ErrorCode::OK);
}

TEST_F(QuestionBankFixture, ST_SEC_QuizViewNeverExposesCorrectAnswerToStudents) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_PUB")).ok());
    qs::Question full;
    EXPECT_EQ(bank_->findQuestionById(studentSession(), "Q_PUB", &full).code, qs::ErrorCode::NOT_AUTHORIZED);

    qs::PublicQuestion pub;
    ASSERT_EQ(bank_->getQuestionForQuiz("Q_PUB", &pub).code, qs::ErrorCode::OK);
    EXPECT_EQ(pub.options.size(), 2U);  // PublicQuestion has no correct_option member by design
    EXPECT_EQ(bank_->getQuestionForQuiz("MISSING", &pub).code, qs::ErrorCode::QUESTION_NOT_FOUND);
}

TEST_F(QuestionBankFixture, ST_SEC_05_ReadOnlyDirectoryKeepsOriginalFileByteIdentical) {
#if defined(_WIN32)
    GTEST_SKIP() << "Directory permission test is not portable to Windows";
#else
    if (geteuid() == 0) {
        GTEST_SKIP() << "Running as root: read-only directories are not enforced";
    }
    const fs::path dir = root_ / "readonly";
    fs::create_directories(dir);
    auto bank = makeFileBank(dir / "questions.tsv", log_path_);
    ASSERT_TRUE(bank->load().ok());
    ASSERT_TRUE(bank->addQuestion(instructorSession(), baseQuestion("Q_R")).ok());
    const std::string original = readFile(dir / "questions.tsv");

    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    auto changed = baseQuestion("Q_R");
    changed.question_text = "will fail";
    EXPECT_EQ(bank->updateQuestion(instructorSession(), changed).code, qs::ErrorCode::IO_ERROR);
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);

    EXPECT_EQ(readFile(dir / "questions.tsv"), original);
    qs::Question still;
    ASSERT_TRUE(bank->findQuestionById(instructorSession(), "Q_R", &still).ok());
    EXPECT_EQ(still.question_text, "What is 2+2?");  // in-memory state was rolled back too
#endif
}

// ------------------------------------------------------------------ in-memory repository tests

TEST(QuestionBankUnit, EXTRA_InUseQuestionBlocksUpdateAndDelete) {
    auto state = std::make_shared<SharedState>();
    state->stored.push_back(baseQuestion("Q_LOCK"));
    state->referenced.insert("Q_LOCK");
    auto bank = qs::createQuestionBank(std::make_unique<InMemoryRepository>(state), std::make_unique<RecordingLogger>());
    ASSERT_TRUE(bank->load().ok());

    auto changed = baseQuestion("Q_LOCK");
    changed.question_text = "changed";
    EXPECT_EQ(bank->updateQuestion(instructorSession(), changed).code, qs::ErrorCode::QUESTION_IN_USE);
    EXPECT_EQ(bank->deleteQuestion(instructorSession(), "Q_LOCK", true).code, qs::ErrorCode::QUESTION_IN_USE);
    EXPECT_EQ(state->stored.size(), 1U);
    EXPECT_EQ(state->stored[0].question_text, "What is 2+2?");
}

TEST(QuestionBankUnit, EXTRA_SaveFailureRollsBackEveryOperation) {
    auto state = std::make_shared<SharedState>();
    state->stored.push_back(baseQuestion("Q_ONE"));
    auto bank = qs::createQuestionBank(std::make_unique<InMemoryRepository>(state), std::make_unique<RecordingLogger>());
    ASSERT_TRUE(bank->load().ok());
    state->fail_save = true;

    EXPECT_EQ(bank->addQuestion(instructorSession(), baseQuestion("Q_TWO")).code, qs::ErrorCode::IO_ERROR);
    auto changed = baseQuestion("Q_ONE");
    changed.question_text = "changed";
    EXPECT_EQ(bank->updateQuestion(instructorSession(), changed).code, qs::ErrorCode::IO_ERROR);
    EXPECT_EQ(bank->setAvailability(instructorSession(), "Q_ONE", false).code, qs::ErrorCode::IO_ERROR);
    EXPECT_EQ(bank->deleteQuestion(instructorSession(), "Q_ONE", true).code, qs::ErrorCode::IO_ERROR);

    std::vector<qs::Question> all;
    ASSERT_TRUE(bank->listQuestions(instructorSession(), &all, false, std::nullopt, std::nullopt).ok());
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].question_id, "Q_ONE");
    EXPECT_EQ(all[0].question_text, "What is 2+2?");
    EXPECT_TRUE(all[0].is_available);
}

TEST(QuestionBankUnit, EXTRA_ImportPersistFailureRollsBackAndReportsError) {
    auto state = std::make_shared<SharedState>();
    state->files["in.csv"] = kHeader + "\n" + csvRow("A", "Math", "Easy", "x", "1|2", "0") +
                             csvRow("B", "Math", "Easy", "y", "1|2", "0");
    auto bank = qs::createQuestionBank(std::make_unique<InMemoryRepository>(state), std::make_unique<RecordingLogger>());
    ASSERT_TRUE(bank->load().ok());
    state->fail_save = true;

    const auto report = bank->importFromCsv(instructorSession(), "in.csv");
    EXPECT_EQ(report.result.code, qs::ErrorCode::IO_ERROR);
    EXPECT_EQ(report.imported_count, 0U);
    std::vector<qs::Question> all;
    ASSERT_TRUE(bank->listQuestions(instructorSession(), &all, false, std::nullopt, std::nullopt).ok());
    EXPECT_TRUE(all.empty());
}

TEST(QuestionBankUnit, EXTRA_ImportLogFailureIsReportedNotSilent) {
    auto state = std::make_shared<SharedState>();
    state->files["in.csv"] = kHeader + "\n" + csvRow("A", "Math", "Easy", "x", "1|2", "bad");
    auto logger = std::make_unique<RecordingLogger>();
    logger->fail = true;
    auto bank = qs::createQuestionBank(std::make_unique<InMemoryRepository>(state), std::move(logger));
    ASSERT_TRUE(bank->load().ok());

    const auto report = bank->importFromCsv(instructorSession(), "in.csv");
    EXPECT_EQ(report.skipped_count, 1U);
    EXPECT_TRUE(report.log_failed);
}

// ------------------------------------------------------------------ console menu

class MenuFixture : public QuestionBankFixture {
protected:
    std::string runMenu(const qs::Session& session, const std::string& script) {
        std::istringstream in(script);
        std::ostringstream out;
        auto menu = qs::createConsoleMenu(session, "Tester", *bank_, in, out);
        menu->run();
        return out.str();
    }
};

TEST_F(MenuFixture, ST_UI_InvalidOptionThenExit) {
    const std::string out = runMenu(instructorSession(), "9\nabc\n0\n");
    EXPECT_NE(out.find("invalid option"), std::string::npos);
    EXPECT_NE(out.find("User: Tester (Instructor)"), std::string::npos);
}

TEST_F(MenuFixture, ST_UI_EndOfInputExitsCleanly) {
    const std::string out = runMenu(instructorSession(), "");
    EXPECT_NE(out.find("Input ended"), std::string::npos);
    const std::string out2 = runMenu(instructorSession(), "3\nQ1\n");  // EOF in the middle of adding
    EXPECT_NE(out2.find("Input ended"), std::string::npos);
    EXPECT_TRUE(listAll(*bank_).empty());
}

TEST_F(MenuFixture, ST_UI_DeleteNeedsYesAndNoKeepsQuestion) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_MENU")).ok());

    const std::string declined = runMenu(instructorSession(), "5\nQ_MENU\nno\n0\n");
    EXPECT_NE(declined.find("Delete cancelled"), std::string::npos);
    EXPECT_EQ(listAll(*bank_).size(), 1U);

    runMenu(instructorSession(), "5\nQ_MENU\nyes\n0\n");
    EXPECT_TRUE(listAll(*bank_).empty());
}

TEST_F(MenuFixture, ST_UI_AddQuestionThroughMenu) {
    const std::string out = runMenu(instructorSession(), "3\nQ_NEW\nMath\nhard\nWhat is 1+1?\n1|2|3\n1\n0\n");
    EXPECT_NE(out.find("Success: Question added."), std::string::npos);
    qs::Question found;
    ASSERT_TRUE(bank_->findQuestionById(instructorSession(), "Q_NEW", &found).ok());
    EXPECT_EQ(found.difficulty, qs::Difficulty::Hard);
    EXPECT_EQ(found.options.size(), 3U);
}

TEST_F(MenuFixture, ST_UI_InvalidDifficultyCancelsInsteadOfDefaultingToEasy) {
    const std::string out = runMenu(instructorSession(), "3\nQ_BAD\nMath\nHrd\nText\n1|2\n0\n0\n");
    EXPECT_NE(out.find("difficulty must be Easy, Medium, or Hard"), std::string::npos);
    EXPECT_TRUE(listAll(*bank_).empty());
}

TEST_F(MenuFixture, ST_UI_CorrectOptionMustBeAWholeNumber) {
    for (const char* bad : {"1abc", "-1", "", "x", "1.5"}) {
        const std::string out = runMenu(instructorSession(), std::string("3\nQ_X\nMath\nEasy\nText\n1|2\n") + bad + "\n0\n");
        EXPECT_NE(out.find("whole number"), std::string::npos) << bad;
    }
    EXPECT_TRUE(listAll(*bank_).empty());
}

TEST_F(MenuFixture, ST_UI_EnableDisableUsesExactWordsAndConfirmation) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_T")).ok());
    runMenu(instructorSession(), "6\nQ_T\nEN\n0\n");  // old shortcut is gone
    EXPECT_TRUE(listAll(*bank_)[0].is_available);
    runMenu(instructorSession(), "6\nQ_T\ndisable\nno\n0\n");
    EXPECT_TRUE(listAll(*bank_)[0].is_available);
    runMenu(instructorSession(), "6\nQ_T\ndisable\nyes\n0\n");
    EXPECT_FALSE(listAll(*bank_)[0].is_available);
}

TEST_F(MenuFixture, ST_UI_StudentSeesNoActionsAndIsDenied) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_S")).ok());
    const std::string out = runMenu(studentSession(), "1\n5\n0\n");
    EXPECT_EQ(out.find("1. List all questions"), std::string::npos);
    EXPECT_NE(out.find("No question bank actions are available"), std::string::npos);
    EXPECT_NE(out.find("Not authorized"), std::string::npos);
    EXPECT_EQ(out.find("Q_S"), std::string::npos);
    EXPECT_EQ(listAll(*bank_).size(), 1U);
}

TEST_F(MenuFixture, ST_UI_ExportAsksBeforeOverwriting) {
    ASSERT_TRUE(bank_->addQuestion(instructorSession(), baseQuestion("Q_E")).ok());
    const fs::path target = root_ / "export.csv";
    writeFile(target, "old content");

    const std::string declined = runMenu(instructorSession(), "7\n" + target.string() + "\n\n\nno\n0\n");
    EXPECT_NE(declined.find("Export cancelled"), std::string::npos);
    EXPECT_EQ(readFile(target), "old content");

    const std::string accepted = runMenu(instructorSession(), "7\n" + target.string() + "\n\n\nyes\n0\n");
    EXPECT_NE(accepted.find("Success: Export written."), std::string::npos);
    EXPECT_EQ(readFile(target).rfind(kHeader, 0), 0U);
}

TEST_F(MenuFixture, ST_UI_ImportShowsSkippedRowsWithLineNumbers) {
    const fs::path csv = root_ / "menu_import.csv";
    writeFile(csv, kHeader + "\n" + csvRow("Q1", "Math", "Easy", "ok", "1|2", "0") +
                       csvRow("Q2", "Math", "Easy", "bad", "1|2", "x"));
    const std::string out = runMenu(instructorSession(), "8\n" + csv.string() + "\n0\n");
    EXPECT_NE(out.find("imported=1, skipped=1"), std::string::npos);
    EXPECT_NE(out.find("line 3"), std::string::npos);
}

}  // namespace
