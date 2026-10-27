#include "console_menu.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quizsphere {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string errorForCode(ErrorCode code, const std::string& message) {
    switch (code) {
        case ErrorCode::NOT_AUTHORIZED:
            return "Not authorized: sign in as Instructor/Admin.";
        case ErrorCode::VALIDATION_FAILED:
            return "Validation failed: " + message;
        case ErrorCode::DUPLICATE_ID:
            return "Duplicate ID: choose a unique question_id.";
        case ErrorCode::QUESTION_NOT_FOUND:
            return "Question not found: verify the question_id.";
        case ErrorCode::QUESTION_IN_USE:
            return "Question in use: cannot edit/delete referenced question.";
        case ErrorCode::IO_ERROR:
            return "I/O error: " + message;
        case ErrorCode::CSV_STRUCTURE_INVALID:
            return "CSV structure invalid: fix header or file format.";
        case ErrorCode::CONFIRMATION_REQUIRED:
            return "Confirmation required: answer yes to continue.";
        case ErrorCode::OK:
            return message;
    }
    return message;
}

class BasicConsoleMenu final : public ConsoleMenu {
public:
    BasicConsoleMenu(const Session& session,
                     std::string display_name,
                     QuestionBank& question_bank,
                     std::istream& input,
                     std::ostream& output)
        : session_(session),
          display_name_(std::move(display_name)),
          question_bank_(question_bank),
          input_(input),
          output_(output) {}

    void run() override {
        while (true) {
            showMenu();
            std::string choice;
            if (!readLine(&choice)) {
                return;
            }

            if (choice == "0") {
                return;
            }
            if (choice == "1") {
                handleList(false);
                continue;
            }
            if (choice == "2") {
                handleList(true);
                continue;
            }
            if (choice == "3") {
                handleAdd();
                continue;
            }
            if (choice == "4") {
                handleUpdate();
                continue;
            }
            if (choice == "5") {
                handleDelete();
                continue;
            }
            if (choice == "6") {
                handleAvailability();
                continue;
            }
            if (choice == "7") {
                handleExport();
                continue;
            }
            if (choice == "8") {
                handleImport();
                continue;
            }

            output_ << "Error: invalid option. Choose 0-8.\n";
        }
    }

private:
    void showMenu() {
        output_ << "\n=== QuizSphere Question Bank ===\n";
        output_ << "User: " << display_name_ << " (" << roleToString(session_.role) << ")\n";
        output_ << "1. List all questions\n";
        output_ << "2. List available questions\n";
        if (session_.role == Role::Instructor || session_.role == Role::Admin) {
            output_ << "3. Add question\n";
            output_ << "4. Edit question\n";
            output_ << "5. Delete question\n";
            output_ << "6. Enable/disable question\n";
            output_ << "7. Export questions to CSV\n";
            output_ << "8. Import questions from CSV\n";
        }
        output_ << "0. Exit\n";
        output_ << "Choose an option: ";
    }

    bool readLine(std::string* line) {
        if (!std::getline(input_, *line)) {
            output_ << "\nInput ended. Exiting menu.\n";
            return false;
        }
        *line = trim(*line);
        return true;
    }

    bool readDifficultyFilter(std::optional<Difficulty>* out_filter) {
        output_ << "Difficulty filter (Easy/Medium/Hard, blank for all): ";
        std::string raw;
        if (!readLine(&raw)) {
            return false;
        }
        if (raw.empty()) {
            out_filter->reset();
            return true;
        }
        const auto parsed = parseDifficulty(raw);
        if (!parsed.has_value()) {
            output_ << "Error: difficulty must be Easy, Medium, or Hard.\n";
            return false;
        }
        *out_filter = *parsed;
        return true;
    }

    bool readSubjectFilter(std::optional<std::string>* out_filter) {
        output_ << "Subject filter (blank for all): ";
        std::string subject;
        if (!readLine(&subject)) {
            return false;
        }
        if (subject.empty()) {
            out_filter->reset();
            return true;
        }
        *out_filter = subject;
        return true;
    }

    bool confirm(const std::string& prompt) {
        output_ << prompt << " [yes/no]: ";
        std::string answer;
        if (!readLine(&answer)) {
            return false;
        }
        answer = trim(answer);
        std::transform(answer.begin(), answer.end(), answer.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return answer == "yes";
    }

    Question readQuestion(const std::optional<std::string>& fixed_id) {
        Question q;
        if (fixed_id.has_value()) {
            q.question_id = trim(*fixed_id);
            output_ << "Editing question_id: " << q.question_id << "\n";
        } else {
            output_ << "question_id: ";
            std::string id;
            readLine(&id);
            q.question_id = id;
        }

        output_ << "subject_tag: ";
        readLine(&q.subject_tag);

        output_ << "difficulty (Easy/Medium/Hard): ";
        std::string difficulty;
        readLine(&difficulty);
        q.difficulty = parseDifficulty(difficulty).value_or(Difficulty::Easy);

        q.question_type = QuestionType::MCQ;

        output_ << "question_text: ";
        readLine(&q.question_text);

        output_ << "options (pipe separated): ";
        std::string options_raw;
        readLine(&options_raw);
        q.options.clear();
        std::string current;
        for (char c : options_raw) {
            if (c == '|') {
                q.options.push_back(trim(current));
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        q.options.push_back(trim(current));

        output_ << "correct_option index: ";
        std::string idx_raw;
        readLine(&idx_raw);
        try {
            q.correct_option = static_cast<std::size_t>(std::stoull(idx_raw));
        } catch (...) {
            q.correct_option = std::numeric_limits<std::size_t>::max();
        }

        q.is_available = true;
        return q;
    }

    void printQuestions(const std::vector<Question>& questions) {
        if (questions.empty()) {
            output_ << "No questions found.\n";
            return;
        }
        for (const auto& q : questions) {
            output_ << "- " << q.question_id << " | " << q.subject_tag << " | "
                    << difficultyToString(q.difficulty) << " | "
                    << (q.is_available ? "available" : "disabled") << "\n";
        }
    }

    void handleList(bool available_only) {
        std::vector<Question> questions;
        std::optional<std::string> subject_filter;
        if (!readSubjectFilter(&subject_filter)) {
            return;
        }
        std::optional<Difficulty> difficulty_filter;
        if (!readDifficultyFilter(&difficulty_filter)) {
            return;
        }

        const Result result = question_bank_.listQuestions(
            session_, &questions, available_only, subject_filter, difficulty_filter);
        if (!result.ok()) {
            output_ << "Error: " << errorForCode(result.code, result.message) << '\n';
            return;
        }
        printQuestions(questions);
    }

    void handleAdd() {
        Question q = readQuestion(std::nullopt);
        const Result result = question_bank_.addQuestion(session_, q);
        if (!result.ok()) {
            output_ << "Error: " << errorForCode(result.code, result.message) << '\n';
            return;
        }
        output_ << "Success: " << result.message << '\n';
    }

    void handleUpdate() {
        output_ << "question_id to edit: ";
        std::string id;
        if (!readLine(&id)) {
            return;
        }
        Question existing;
        const Result find_result = question_bank_.findQuestionById(session_, id, &existing);
        if (!find_result.ok()) {
            output_ << "Error: " << errorForCode(find_result.code, find_result.message) << '\n';
            return;
        }

        Question q = readQuestion(id);
        q.is_available = existing.is_available;

        const Result result = question_bank_.updateQuestion(session_, q);
        if (!result.ok()) {
            output_ << "Error: " << errorForCode(result.code, result.message) << '\n';
            return;
        }
        output_ << "Success: " << result.message << '\n';
    }

    void handleDelete() {
        output_ << "question_id to delete: ";
        std::string id;
        if (!readLine(&id)) {
            return;
        }

        const bool confirmed = confirm("Delete question " + id + "?");
        const Result result = question_bank_.deleteQuestion(session_, id, confirmed);
        if (!result.ok()) {
            output_ << "Error: " << errorForCode(result.code, result.message) << '\n';
            return;
        }
        output_ << "Success: " << result.message << '\n';
    }

    void handleAvailability() {
        output_ << "question_id: ";
        std::string id;
        if (!readLine(&id)) {
            return;
        }

        output_ << "Action (enable/disable): ";
        std::string action;
        if (!readLine(&action)) {
            return;
        }

        bool enable = false;
        if (action == "enable") {
            enable = true;
        } else if (action == "disable") {
            enable = false;
        } else {
            output_ << "Error: action must be 'enable' or 'disable'.\n";
            return;
        }

        if (!confirm(std::string(enable ? "Enable" : "Disable") + " question " + id + "?")) {
            output_ << "Error: confirmation required.\n";
            return;
        }

        const Result result = question_bank_.setAvailability(session_, id, enable);
        if (!result.ok()) {
            output_ << "Error: " << errorForCode(result.code, result.message) << '\n';
            return;
        }
        output_ << "Success: " << result.message << '\n';
    }

    void handleExport() {
        output_ << "CSV output path: ";
        std::string path;
        if (!readLine(&path)) {
            return;
        }

        std::optional<std::string> subject_filter;
        if (!readSubjectFilter(&subject_filter)) {
            return;
        }
        std::optional<Difficulty> difficulty_filter;
        if (!readDifficultyFilter(&difficulty_filter)) {
            return;
        }

        bool overwrite = false;
        if (std::filesystem::exists(path)) {
            overwrite = confirm("File exists. Overwrite?");
        }

        const Result result = question_bank_.exportToCsv(
            session_, path, subject_filter, difficulty_filter,
            overwrite);
        if (!result.ok()) {
            output_ << "Error: " << errorForCode(result.code, result.message) << '\n';
            return;
        }
        output_ << "Success: " << result.message << '\n';
    }

    void handleImport() {
        output_ << "CSV input path: ";
        std::string path;
        if (!readLine(&path)) {
            return;
        }

        const ImportReport report = question_bank_.importFromCsv(session_, path);
        if (!report.result.ok()) {
            output_ << "Error: " << errorForCode(report.result.code, report.result.message) << '\n';
            return;
        }

        output_ << "Success: imported=" << report.imported_count
                << ", skipped=" << report.skipped_count << '\n';
        for (const auto& issue : report.invalid_rows) {
            output_ << "  row " << issue.row_number << ": " << issue.reason << '\n';
        }
    }

    Session session_;
    std::string display_name_;
    QuestionBank& question_bank_;
    std::istream& input_;
    std::ostream& output_;
};

}  // namespace

std::unique_ptr<ConsoleMenu> createConsoleMenu(const Session& session,
                                               std::string display_name,
                                               QuestionBank& question_bank,
                                               std::istream& input,
                                               std::ostream& output) {
    return std::make_unique<BasicConsoleMenu>(session, std::move(display_name), question_bank, input, output);
}

}  // namespace quizsphere
