#pragma once

#include <cstddef>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace quizsphere {

enum class Role {
    Student,
    Instructor,
    Administrator
};

struct Session {
    std::string user_id;
    std::string display_name;
    Role role{Role::Student};
    bool authenticated{false};
};

struct Question {
    std::string question_id;
    std::string subject_tag;
    std::string difficulty;
    std::string question_type;
    std::string question_text;
    std::vector<std::string> options;
    std::size_t correct_option{0};
    bool is_available{true};
};

struct OperationResult {
    bool success{false};
    std::string message;
};

struct ImportIssue {
    std::size_t row_number{0};
    std::string reason;
};

struct ImportReport {
    bool success{false};
    std::string message;
    std::size_t imported_count{0};
    std::size_t skipped_count{0};
    std::vector<ImportIssue> invalid_rows;
};

class QuestionValidator {
public:
    virtual ~QuestionValidator() = default;
    virtual OperationResult validate(const Question& question) const = 0;
};

class QuestionBank {
public:
    virtual ~QuestionBank() = default;

    virtual OperationResult load() = 0;
    virtual OperationResult addQuestion(const Session& session, const Question& question) = 0;
    virtual OperationResult updateQuestion(const Session& session, const Question& question) = 0;
    virtual OperationResult deleteQuestion(const Session& session, const std::string& question_id, bool confirmed) = 0;
    virtual OperationResult setAvailability(const Session& session, const std::string& question_id, bool is_available) = 0;

    virtual std::vector<Question> listQuestions(bool available_only = false) const = 0;
    virtual std::vector<Question> listQuestionsBySubject(const std::string& subject_tag,
                                                         bool available_only = false) const = 0;
    virtual std::optional<Question> findQuestionById(const std::string& question_id) const = 0;

    virtual ImportReport importFromCsv(const Session& session, const std::string& csv_path) = 0;
    virtual OperationResult exportToCsv(const Session& session,
                                        const std::string& csv_path,
                                        const std::optional<std::string>& subject_filter = std::nullopt) const = 0;
};

class ConsoleMenu {
public:
    virtual ~ConsoleMenu() = default;
    virtual void run() = 0;
};

std::string roleToString(Role role);

std::unique_ptr<QuestionValidator> createQuestionValidator();
std::unique_ptr<QuestionBank> createQuestionBank(std::string storage_path = "data/questions.tsv");
std::unique_ptr<ConsoleMenu> createConsoleMenu(Session session,
                                               QuestionBank& question_bank,
                                               std::istream& input,
                                               std::ostream& output);

}  // namespace quizsphere
