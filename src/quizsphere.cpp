#include "quizsphere.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quizsphere {
namespace {

bool hasQuestionManagementAccess(const Session& session) {
    return session.authenticated &&
           (session.role == Role::Instructor || session.role == Role::Administrator);
}

OperationResult unauthorizedResult() {
    return {false, "Only Instructor or Administrator can perform this operation."};
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string escapeDelimited(const std::string& input, char delimiter) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const char c : input) {
        switch (c) {
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (c == delimiter) {
                    escaped.push_back('\\');
                }
                escaped.push_back(c);
                break;
        }
    }
    return escaped;
}

std::string unescapeDelimited(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool escaping = false;
    for (const char c : input) {
        if (!escaping) {
            if (c == '\\') {
                escaping = true;
                continue;
            }
            out.push_back(c);
            continue;
        }

        switch (c) {
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                out.push_back(c);
                break;
        }
        escaping = false;
    }
    if (escaping) {
        out.push_back('\\');
    }
    return out;
}

std::vector<std::string> splitEscaped(const std::string& input, char delimiter) {
    std::vector<std::string> result;
    std::string current;
    bool escaping = false;

    for (const char c : input) {
        if (escaping) {
            current.push_back(c);
            escaping = false;
            continue;
        }

        if (c == '\\') {
            escaping = true;
            continue;
        }

        if (c == delimiter) {
            result.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(c);
    }

    if (escaping) {
        current.push_back('\\');
    }

    result.push_back(current);
    return result;
}

std::string joinEscaped(const std::vector<std::string>& values, char delimiter) {
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            output << delimiter;
        }
        output << escapeDelimited(values[i], delimiter);
    }
    return output.str();
}

std::string csvEscape(const std::string& input) {
    const bool needs_quotes = std::any_of(input.begin(), input.end(), [](char c) {
        return c == ',' || c == '"' || c == '\n' || c == '\r';
    });

    if (!needs_quotes) {
        return input;
    }

    std::string quoted;
    quoted.reserve(input.size() + 2);
    quoted.push_back('"');
    for (char c : input) {
        if (c == '"') {
            quoted += "\"\"";
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::pair<bool, std::vector<std::string>> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current.push_back(c);
            }
            continue;
        }

        if (c == ',') {
            fields.push_back(current);
            current.clear();
            continue;
        }

        if (c == '"') {
            if (!current.empty()) {
                return {false, {}};
            }
            in_quotes = true;
            continue;
        }

        current.push_back(c);
    }

    if (in_quotes) {
        return {false, {}};
    }

    fields.push_back(current);
    return {true, fields};
}

bool tryParseBool(const std::string& raw, bool* value) {
    const std::string normalized = trim(raw);
    if (normalized == "1" || normalized == "true" || normalized == "TRUE" || normalized == "yes" || normalized == "Yes") {
        *value = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "FALSE" || normalized == "no" || normalized == "No") {
        *value = false;
        return true;
    }
    return false;
}

bool parseSizeT(const std::string& raw, std::size_t* value) {
    const std::string trimmed = trim(raw);
    if (trimmed.empty()) {
        return false;
    }

    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(trimmed, &consumed);
        if (consumed != trimmed.size()) {
            return false;
        }
        *value = static_cast<std::size_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool isValidDifficulty(const std::string& difficulty) {
    return difficulty == "Easy" || difficulty == "Medium" || difficulty == "Hard";
}

bool isValidQuestionType(const std::string& type) {
    return type == "MCQ" || type == "MultipleChoice" || type == "TrueFalse" || type == "ShortAnswer";
}

class DefaultQuestionValidator : public QuestionValidator {
public:
    OperationResult validate(const Question& question) const override {
        if (trim(question.question_id).empty()) {
            return {false, "Question ID is required."};
        }
        if (trim(question.subject_tag).empty()) {
            return {false, "Subject tag is required."};
        }
        if (!isValidDifficulty(question.difficulty)) {
            return {false, "Difficulty must be one of: Easy, Medium, Hard."};
        }
        if (!isValidQuestionType(question.question_type)) {
            return {false, "Question type must be one of: MCQ, MultipleChoice, TrueFalse, ShortAnswer."};
        }
        if (trim(question.question_text).empty()) {
            return {false, "Question text is required."};
        }
        if (question.options.size() < 2) {
            return {false, "At least two options are required."};
        }
        for (std::size_t i = 0; i < question.options.size(); ++i) {
            if (trim(question.options[i]).empty()) {
                return {false, "Option " + std::to_string(i) + " cannot be empty."};
            }
        }
        if (question.correct_option >= question.options.size()) {
            return {false, "Correct option index must refer to an existing option."};
        }
        return {true, "Question is valid."};
    }
};

class FileQuestionBank : public QuestionBank {
public:
    explicit FileQuestionBank(std::string storage_path)
        : storage_path_(std::move(storage_path)), validator_(createQuestionValidator()) {}

    OperationResult load() override {
        questions_.clear();
        std::ifstream in(storage_path_);
        if (!in.is_open()) {
            return {true, "Question store not found; starting with empty bank."};
        }

        std::string line;
        std::size_t line_number = 0;
        while (std::getline(in, line)) {
            ++line_number;
            if (line.empty()) {
                continue;
            }

            const std::vector<std::string> columns = splitEscaped(line, '\t');
            if (line_number == 1 && !columns.empty() && columns[0] == "question_id") {
                continue;
            }

            if (columns.size() != 8) {
                return {false, "Malformed storage row at line " + std::to_string(line_number) + "."};
            }

            Question question;
            question.question_id = unescapeDelimited(columns[0]);
            question.subject_tag = unescapeDelimited(columns[1]);
            question.difficulty = unescapeDelimited(columns[2]);
            question.question_type = unescapeDelimited(columns[3]);
            question.question_text = unescapeDelimited(columns[4]);
            question.options = splitEscaped(unescapeDelimited(columns[5]), '|');

            if (!parseSizeT(unescapeDelimited(columns[6]), &question.correct_option)) {
                return {false, "Malformed correct option index at line " + std::to_string(line_number) + "."};
            }

            bool is_available = true;
            if (!tryParseBool(unescapeDelimited(columns[7]), &is_available)) {
                return {false, "Malformed availability flag at line " + std::to_string(line_number) + "."};
            }
            question.is_available = is_available;

            const OperationResult validation = validator_->validate(question);
            if (!validation.success) {
                return {false, "Invalid stored question at line " + std::to_string(line_number) + ": " + validation.message};
            }

            questions_[question.question_id] = question;
        }

        return {true, "Question bank loaded."};
    }

    OperationResult addQuestion(const Session& session, const Question& question) override {
        if (!hasQuestionManagementAccess(session)) {
            return unauthorizedResult();
        }

        const OperationResult validation = validator_->validate(question);
        if (!validation.success) {
            return validation;
        }

        if (questions_.find(question.question_id) != questions_.end()) {
            return {false, "Question ID already exists; use a unique ID."};
        }

        questions_[question.question_id] = question;
        const OperationResult persisted = persist();
        if (!persisted.success) {
            questions_.erase(question.question_id);
            return persisted;
        }

        return {true, "Question added."};
    }

    OperationResult updateQuestion(const Session& session, const Question& question) override {
        if (!hasQuestionManagementAccess(session)) {
            return unauthorizedResult();
        }

        const OperationResult validation = validator_->validate(question);
        if (!validation.success) {
            return validation;
        }

        auto existing = questions_.find(question.question_id);
        if (existing == questions_.end()) {
            return {false, "Question not found."};
        }

        Question previous = existing->second;
        existing->second = question;

        const OperationResult persisted = persist();
        if (!persisted.success) {
            existing->second = previous;
            return persisted;
        }

        return {true, "Question updated."};
    }

    OperationResult deleteQuestion(const Session& session,
                                   const std::string& question_id,
                                   bool confirmed) override {
        if (!hasQuestionManagementAccess(session)) {
            return unauthorizedResult();
        }

        if (!confirmed) {
            return {false, "Delete cancelled. Confirmation required for destructive action."};
        }

        auto existing = questions_.find(question_id);
        if (existing == questions_.end()) {
            return {false, "Question not found."};
        }

        const Question previous = existing->second;
        questions_.erase(existing);

        const OperationResult persisted = persist();
        if (!persisted.success) {
            questions_[question_id] = previous;
            return persisted;
        }

        return {true, "Question deleted."};
    }

    OperationResult setAvailability(const Session& session,
                                    const std::string& question_id,
                                    bool is_available) override {
        if (!hasQuestionManagementAccess(session)) {
            return unauthorizedResult();
        }

        auto existing = questions_.find(question_id);
        if (existing == questions_.end()) {
            return {false, "Question not found."};
        }

        const bool previous = existing->second.is_available;
        existing->second.is_available = is_available;

        const OperationResult persisted = persist();
        if (!persisted.success) {
            existing->second.is_available = previous;
            return persisted;
        }

        return {true, is_available ? "Question enabled." : "Question disabled."};
    }

    std::vector<Question> listQuestions(bool available_only = false) const override {
        std::vector<Question> out;
        out.reserve(questions_.size());

        for (const auto& entry : questions_) {
            if (available_only && !entry.second.is_available) {
                continue;
            }
            out.push_back(entry.second);
        }

        std::sort(out.begin(), out.end(), [](const Question& lhs, const Question& rhs) {
            return lhs.question_id < rhs.question_id;
        });

        return out;
    }

    std::vector<Question> listQuestionsBySubject(const std::string& subject_tag,
                                                 bool available_only = false) const override {
        const std::string wanted = trim(subject_tag);
        std::vector<Question> out;

        for (const auto& entry : questions_) {
            if (available_only && !entry.second.is_available) {
                continue;
            }
            if (entry.second.subject_tag == wanted) {
                out.push_back(entry.second);
            }
        }

        std::sort(out.begin(), out.end(), [](const Question& lhs, const Question& rhs) {
            return lhs.question_id < rhs.question_id;
        });

        return out;
    }

    std::optional<Question> findQuestionById(const std::string& question_id) const override {
        const auto it = questions_.find(question_id);
        if (it == questions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    ImportReport importFromCsv(const Session& session, const std::string& csv_path) override {
        if (!hasQuestionManagementAccess(session)) {
            return {false, unauthorizedResult().message, 0, 0, {}};
        }

        std::ifstream in(csv_path);
        if (!in.is_open()) {
            return {false, "Unable to open CSV file for import.", 0, 0, {}};
        }

        std::string line;
        std::size_t line_number = 0;
        std::vector<Question> staged_questions;
        std::vector<ImportIssue> invalid_rows;
        std::unordered_set<std::string> seen_ids;
        for (const auto& item : questions_) {
            seen_ids.insert(item.first);
        }

        while (std::getline(in, line)) {
            ++line_number;

            if (line_number == 1) {
                const auto [ok, header] = parseCsvLine(line);
                if (!ok || header.size() != 8 || header[0] != "question_id" ||
                    header[1] != "subject_tag" || header[2] != "difficulty" ||
                    header[3] != "question_type" || header[4] != "question_text" ||
                    header[5] != "options" || header[6] != "correct_option" ||
                    header[7] != "is_available") {
                    return {false,
                            "CSV header must be: question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available",
                            0,
                            0,
                            {}};
                }
                continue;
            }

            if (trim(line).empty()) {
                continue;
            }

            const auto [ok, columns] = parseCsvLine(line);
            if (!ok || columns.size() != 8) {
                return {false,
                        "Import failed because one or more rows are structurally malformed.",
                        0,
                        1,
                        {{line_number, "Malformed CSV row (check quoting and comma count)."}}};
            }

            Question question;
            question.question_id = columns[0];
            question.subject_tag = columns[1];
            question.difficulty = columns[2];
            question.question_type = columns[3];
            question.question_text = columns[4];
            question.options = splitEscaped(columns[5], '|');

            bool correct_option_valid = parseSizeT(columns[6], &question.correct_option);
            if (!correct_option_valid) {
                question.correct_option = std::numeric_limits<std::size_t>::max();
            }

            bool is_available = true;
            const bool availability_valid = tryParseBool(columns[7], &is_available);
            question.is_available = is_available;

            ImportIssue* issue = nullptr;
            ImportIssue issue_data{line_number, ""};

            if (!correct_option_valid) {
                issue_data.reason = "Correct option must be a valid numeric index.";
                issue = &issue_data;
            } else if (!availability_valid) {
                issue_data.reason = "is_available must be true/false, yes/no, or 1/0.";
                issue = &issue_data;
            } else if (seen_ids.find(question.question_id) != seen_ids.end()) {
                issue_data.reason = "Duplicate question ID; existing records are not overwritten.";
                issue = &issue_data;
            } else {
                const OperationResult validation = validator_->validate(question);
                if (!validation.success) {
                    issue_data.reason = validation.message;
                    issue = &issue_data;
                }
            }

            if (issue != nullptr) {
                invalid_rows.push_back(*issue);
                continue;
            }

            seen_ids.insert(question.question_id);
            staged_questions.push_back(question);
        }

        if (staged_questions.empty() && !invalid_rows.empty()) {
            return {false,
                    "No rows imported. Fix invalid rows and retry.",
                    0,
                    invalid_rows.size(),
                    invalid_rows};
        }

        const auto previous_questions = questions_;
        for (const auto& question : staged_questions) {
            questions_[question.question_id] = question;
        }

        const OperationResult persisted = persist();
        if (!persisted.success) {
            questions_ = previous_questions;
            return {false, persisted.message, 0, invalid_rows.size(), invalid_rows};
        }

        return {true,
                "CSV import completed.",
                staged_questions.size(),
                invalid_rows.size(),
                invalid_rows};
    }

    OperationResult exportToCsv(const Session& session,
                                const std::string& csv_path,
                                const std::optional<std::string>& subject_filter = std::nullopt) const override {
        if (!hasQuestionManagementAccess(session)) {
            return unauthorizedResult();
        }

        std::filesystem::path output_path(csv_path);
        if (output_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(output_path.parent_path(), ec);
            if (ec) {
                return {false, "Unable to create export directory."};
            }
        }

        std::ofstream out(csv_path);
        if (!out.is_open()) {
            return {false, "Unable to open CSV file for export."};
        }

        out << "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available\n";

        const std::vector<Question> questions = subject_filter.has_value()
                                                    ? listQuestionsBySubject(*subject_filter)
                                                    : listQuestions();

        for (const auto& question : questions) {
            out << csvEscape(question.question_id) << ','
                << csvEscape(question.subject_tag) << ','
                << csvEscape(question.difficulty) << ','
                << csvEscape(question.question_type) << ','
                << csvEscape(question.question_text) << ','
                << csvEscape(joinEscaped(question.options, '|')) << ','
                << question.correct_option << ','
                << (question.is_available ? "true" : "false") << '\n';
        }

        return {true, "CSV export completed."};
    }

private:
    OperationResult persist() const {
        std::filesystem::path storage_file(storage_path_);
        if (storage_file.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(storage_file.parent_path(), ec);
            if (ec) {
                return {false, "Unable to create question storage directory."};
            }
        }

        const std::filesystem::path temp_file = storage_file.string() + ".tmp";
        std::ofstream out(temp_file);
        if (!out.is_open()) {
            return {false, "Unable to write question storage file."};
        }

        out << "question_id\tsubject_tag\tdifficulty\tquestion_type\tquestion_text\toptions\tcorrect_option\tis_available\n";

        const std::vector<Question> questions = listQuestions();
        for (const auto& question : questions) {
            out << escapeDelimited(question.question_id, '\t') << '\t'
                << escapeDelimited(question.subject_tag, '\t') << '\t'
                << escapeDelimited(question.difficulty, '\t') << '\t'
                << escapeDelimited(question.question_type, '\t') << '\t'
                << escapeDelimited(question.question_text, '\t') << '\t'
                << escapeDelimited(joinEscaped(question.options, '|'), '\t') << '\t'
                << escapeDelimited(std::to_string(question.correct_option), '\t') << '\t'
                << (question.is_available ? "true" : "false") << '\n';
        }

        out.close();
        if (out.fail()) {
            std::error_code ec;
            std::filesystem::remove(temp_file, ec);
            return {false, "Failed to finalize question storage write."};
        }

        std::error_code ec;
        if (std::filesystem::exists(storage_file, ec)) {
            std::filesystem::remove(storage_file, ec);
            if (ec) {
                std::filesystem::remove(temp_file, ec);
                return {false, "Unable to replace existing question storage file."};
            }
        }

        std::filesystem::rename(temp_file, storage_file, ec);
        if (ec) {
            std::filesystem::remove(temp_file, ec);
            return {false, "Unable to atomically replace question storage file."};
        }

        return {true, "Question storage persisted."};
    }

    std::string storage_path_;
    std::unique_ptr<QuestionValidator> validator_;
    std::unordered_map<std::string, Question> questions_;
};

class BasicConsoleMenu : public ConsoleMenu {
public:
    BasicConsoleMenu(Session session, QuestionBank& question_bank, std::istream& input, std::ostream& output)
        : session_(std::move(session)), question_bank_(question_bank), input_(input), output_(output) {}

    void run() override {
        bool running = true;
        while (running) {
            output_ << "\n=== QuizSphere Question Bank ===\n";
            output_ << "User: " << session_.display_name << " (" << roleToString(session_.role) << ")\n";
            output_ << "1. List all questions\n";
            output_ << "2. List available questions\n";
            output_ << "3. Add question\n";
            output_ << "4. Edit question\n";
            output_ << "5. Delete question\n";
            output_ << "6. Enable/disable question\n";
            output_ << "7. Export questions to CSV\n";
            output_ << "8. Import questions from CSV\n";
            output_ << "0. Exit\n";
            output_ << "Choose an option: ";

            std::string choice;
            if (!std::getline(input_, choice)) {
                return;
            }

            if (choice == "1") {
                printQuestions(question_bank_.listQuestions(false));
            } else if (choice == "2") {
                printQuestions(question_bank_.listQuestions(true));
            } else if (choice == "3") {
                handleAdd();
            } else if (choice == "4") {
                handleEdit();
            } else if (choice == "5") {
                handleDelete();
            } else if (choice == "6") {
                handleToggleAvailability();
            } else if (choice == "7") {
                handleExport();
            } else if (choice == "8") {
                handleImport();
            } else if (choice == "0") {
                running = false;
            } else {
                output_ << "Invalid option.\n";
            }
        }
    }

private:
    static std::vector<std::string> splitRawOptions(const std::string& input) {
        std::vector<std::string> options;
        std::string current;
        for (char c : input) {
            if (c == '|') {
                options.push_back(trim(current));
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        options.push_back(trim(current));
        return options;
    }

    Question readQuestionFields(const std::string& existing_id = "") {
        Question question;
        if (!existing_id.empty()) {
            question.question_id = existing_id;
            output_ << "Question ID: " << question.question_id << "\n";
        } else {
            output_ << "Question ID: ";
            std::getline(input_, question.question_id);
        }

        output_ << "Subject tag: ";
        std::getline(input_, question.subject_tag);

        output_ << "Difficulty (Easy/Medium/Hard): ";
        std::getline(input_, question.difficulty);

        output_ << "Question type (MCQ/MultipleChoice/TrueFalse/ShortAnswer): ";
        std::getline(input_, question.question_type);

        output_ << "Question text: ";
        std::getline(input_, question.question_text);

        output_ << "Options (pipe-separated, e.g. A|B|C): ";
        std::string options;
        std::getline(input_, options);
        question.options = splitRawOptions(options);

        output_ << "Correct option index (0-based): ";
        std::string correct;
        std::getline(input_, correct);
        std::size_t correct_option = std::numeric_limits<std::size_t>::max();
        parseSizeT(correct, &correct_option);
        question.correct_option = correct_option;
        question.is_available = true;
        return question;
    }

    bool confirm(const std::string& prompt) {
        output_ << prompt << " [yes/no]: ";
        std::string answer;
        std::getline(input_, answer);
        answer = trim(answer);
        std::transform(answer.begin(), answer.end(), answer.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return answer == "yes" || answer == "y";
    }

    void printQuestions(const std::vector<Question>& questions) {
        if (questions.empty()) {
            output_ << "No questions found.\n";
            return;
        }

        for (const auto& question : questions) {
            output_ << "- [" << question.question_id << "] " << question.subject_tag
                    << " | " << question.difficulty << " | " << question.question_type
                    << " | available=" << (question.is_available ? "true" : "false") << "\n";
        }
    }

    void printResult(const OperationResult& result) {
        output_ << (result.success ? "Success: " : "Error: ") << result.message << '\n';
    }

    void handleAdd() {
        Question question = readQuestionFields();
        printResult(question_bank_.addQuestion(session_, question));
    }

    void handleEdit() {
        output_ << "Question ID to edit: ";
        std::string question_id;
        std::getline(input_, question_id);

        if (!question_bank_.findQuestionById(question_id).has_value()) {
            output_ << "Error: Question not found.\n";
            return;
        }

        Question question = readQuestionFields(question_id);
        if (const auto existing = question_bank_.findQuestionById(question_id); existing.has_value()) {
            question.is_available = existing->is_available;
        }
        printResult(question_bank_.updateQuestion(session_, question));
    }

    void handleDelete() {
        output_ << "Question ID to delete: ";
        std::string question_id;
        std::getline(input_, question_id);

        const bool confirmed = confirm("Delete question '" + question_id + "'?");
        printResult(question_bank_.deleteQuestion(session_, question_id, confirmed));
    }

    void handleToggleAvailability() {
        output_ << "Question ID: ";
        std::string question_id;
        std::getline(input_, question_id);

        output_ << "Set availability (enable/disable): ";
        std::string command;
        std::getline(input_, command);
        command = trim(command);

        const bool enable = (command == "enable" || command == "EN" || command == "1" || command == "true");
        if (!enable && command != "disable" && command != "0" && command != "false") {
            output_ << "Error: choose 'enable' or 'disable'.\n";
            return;
        }

        const bool confirmed = confirm(std::string(enable ? "Enable" : "Disable") + " question '" + question_id + "'?");
        if (!confirmed) {
            output_ << "Error: Action cancelled.\n";
            return;
        }

        printResult(question_bank_.setAvailability(session_, question_id, enable));
    }

    void handleExport() {
        output_ << "CSV path: ";
        std::string csv_path;
        std::getline(input_, csv_path);

        output_ << "Subject filter (leave empty for all): ";
        std::string subject;
        std::getline(input_, subject);

        std::optional<std::string> filter;
        if (!trim(subject).empty()) {
            filter = trim(subject);
        }

        printResult(question_bank_.exportToCsv(session_, csv_path, filter));
    }

    void handleImport() {
        output_ << "CSV path: ";
        std::string csv_path;
        std::getline(input_, csv_path);

        ImportReport report = question_bank_.importFromCsv(session_, csv_path);
        output_ << (report.success ? "Success: " : "Error: ") << report.message << '\n';
        output_ << "Imported: " << report.imported_count << ", Skipped: " << report.skipped_count << '\n';
        for (const auto& issue : report.invalid_rows) {
            output_ << "  Row " << issue.row_number << ": " << issue.reason << '\n';
        }
    }

    Session session_;
    QuestionBank& question_bank_;
    std::istream& input_;
    std::ostream& output_;
};

}  // namespace

std::string roleToString(const Role role) {
    switch (role) {
        case Role::Student:
            return "Student";
        case Role::Instructor:
            return "Instructor";
        case Role::Administrator:
            return "Administrator";
    }
    return "Unknown";
}

std::unique_ptr<QuestionValidator> createQuestionValidator() {
    return std::make_unique<DefaultQuestionValidator>();
}

std::unique_ptr<QuestionBank> createQuestionBank(std::string storage_path) {
    return std::make_unique<FileQuestionBank>(std::move(storage_path));
}

std::unique_ptr<ConsoleMenu> createConsoleMenu(Session session,
                                               QuestionBank& question_bank,
                                               std::istream& input,
                                               std::ostream& output) {
    return std::make_unique<BasicConsoleMenu>(std::move(session), question_bank, input, output);
}

}  // namespace quizsphere
