#include "console_menu.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
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

// Whole string must be digits (no sign, no suffix).
bool parseWholeNumber(const std::string& text, std::size_t* out) {
    if (text.empty() || text.size() > 18) {
        return false;
    }
    if (!std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    *out = static_cast<std::size_t>(std::stoull(text));
    return true;
}

// One-line messages that name the problem and the fix (SAD 4.5).
std::string describe(const Result& result) {
    switch (result.code) {
        case ErrorCode::NOT_AUTHORIZED:
            return "Not authorized: sign in as Instructor or Admin.";
        case ErrorCode::VALIDATION_FAILED:
            return "Validation failed: " + result.message;
        case ErrorCode::DUPLICATE_ID:
            return "Duplicate ID: choose a unique question_id.";
        case ErrorCode::QUESTION_NOT_FOUND:
            return "Question not found: check the question_id.";
        case ErrorCode::QUESTION_IN_USE:
            return "Question in use: it is referenced by a quiz or attempt.";
        case ErrorCode::IO_ERROR:
            return "I/O error: " + result.message;
        case ErrorCode::CSV_STRUCTURE_INVALID:
            return "CSV rejected: " + result.message;
        case ErrorCode::CONFIRMATION_REQUIRED:
            return "Confirmation required: answer yes to continue.";
        case ErrorCode::OK:
            return result.message;
    }
    return result.message;
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

            const bool is_action = choice.size() == 1 && choice[0] >= '1' && choice[0] <= '8';
            if (!is_action) {
                output_ << "Error: invalid option. Choose 0-8.\n";
                continue;
            }
            if (!isManager()) {
                output_ << "Error: " << describe({ErrorCode::NOT_AUTHORIZED, ""}) << '\n';
                continue;
            }

            switch (choice[0]) {
                case '1':
                    handleList(false);
                    break;
                case '2':
                    handleList(true);
                    break;
                case '3':
                    handleAdd();
                    break;
                case '4':
                    handleUpdate();
                    break;
                case '5':
                    handleDelete();
                    break;
                case '6':
                    handleAvailability();
                    break;
                case '7':
                    handleExport();
                    break;
                default:
                    handleImport();
                    break;
            }
        }
    }

private:
    [[nodiscard]] bool isManager() const {
        return session_.role == Role::Instructor || session_.role == Role::Admin;
    }

    void showMenu() {
        output_ << "\n=== QuizSphere Question Bank ===\n";
        output_ << "User: " << display_name_ << " (" << roleToString(session_.role) << ")\n";
        if (isManager()) {
            output_ << "1. List all questions\n";
            output_ << "2. List available questions\n";
            output_ << "3. Add question\n";
            output_ << "4. Edit question\n";
            output_ << "5. Delete question\n";
            output_ << "6. Enable/disable question\n";
            output_ << "7. Export questions to CSV\n";
            output_ << "8. Import questions from CSV\n";
        } else {
            output_ << "No question bank actions are available for your role.\n";
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

    void printError(const Result& result) { output_ << "Error: " << describe(result) << '\n'; }

    void printOutcome(const Result& result) {
        if (result.ok()) {
            output_ << "Success: " << result.message << '\n';
        } else {
            printError(result);
        }
    }

    // Returns false if the user cancelled (bad input or end of input).
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
        } else {
            *out_filter = subject;
        }
        return true;
    }

    bool confirm(const std::string& prompt) {
        output_ << prompt << " [yes/no]: ";
        std::string answer;
        if (!readLine(&answer)) {
            return false;
        }
        std::transform(answer.begin(), answer.end(), answer.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return answer == "yes" || answer == "y";
    }

    // Returns false (and prints why) if the entry was cancelled. Nothing is stored in that case.
    bool readQuestion(const std::optional<std::string>& fixed_id, Question* q) {
        if (fixed_id.has_value()) {
            q->question_id = *fixed_id;
            output_ << "Editing question_id: " << q->question_id << '\n';
        } else {
            output_ << "question_id: ";
            if (!readLine(&q->question_id)) {
                return false;
            }
        }

        output_ << "subject_tag: ";
        if (!readLine(&q->subject_tag)) {
            return false;
        }

        output_ << "difficulty (Easy/Medium/Hard): ";
        std::string difficulty_raw;
        if (!readLine(&difficulty_raw)) {
            return false;
        }
        const auto difficulty = parseDifficulty(difficulty_raw);
        if (!difficulty.has_value()) {
            output_ << "Error: difficulty must be Easy, Medium, or Hard. Nothing was saved.\n";
            return false;
        }
        q->difficulty = *difficulty;
        q->question_type = QuestionType::MCQ;

        output_ << "question_text: ";
        if (!readLine(&q->question_text)) {
            return false;
        }

        output_ << "options (separate with |, e.g. A|B|C; an option cannot contain |): ";
        std::string options_raw;
        if (!readLine(&options_raw)) {
            return false;
        }
        q->options.clear();
        std::string current;
        for (const char c : options_raw) {
            if (c == '|') {
                q->options.push_back(trim(current));
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        q->options.push_back(trim(current));

        output_ << "correct_option index (0 = first option): ";
        std::string index_raw;
        if (!readLine(&index_raw)) {
            return false;
        }
        if (!parseWholeNumber(index_raw, &q->correct_option)) {
            output_ << "Error: correct option must be a whole number such as 0 or 1. Nothing was saved.\n";
            return false;
        }

        q->is_available = true;
        return true;
    }

    void printQuestions(const std::vector<Question>& questions) {
        if (questions.empty()) {
            output_ << "No questions found.\n";
            return;
        }
        for (const auto& q : questions) {
            output_ << "- " << q.question_id << " | " << q.subject_tag << " | "
                    << difficultyToString(q.difficulty) << " | "
                    << (q.is_available ? "available" : "disabled") << '\n';
        }
    }

    void handleList(bool available_only) {
        std::optional<std::string> subject_filter;
        if (!readSubjectFilter(&subject_filter)) {
            return;
        }
        std::optional<Difficulty> difficulty_filter;
        if (!readDifficultyFilter(&difficulty_filter)) {
            return;
        }

        std::vector<Question> questions;
        const Result result =
            question_bank_.listQuestions(session_, &questions, available_only, subject_filter, difficulty_filter);
        if (!result.ok()) {
            printError(result);
            return;
        }
        printQuestions(questions);
    }

    void handleAdd() {
        Question q;
        if (!readQuestion(std::nullopt, &q)) {
            return;
        }
        const Result result = question_bank_.addQuestion(session_, q);
        printOutcome(result);
    }

    void handleUpdate() {
        output_ << "question_id to edit: ";
        std::string id;
        if (!readLine(&id)) {
            return;
        }
        Question existing;
        const Result found = question_bank_.findQuestionById(session_, id, &existing);
        if (!found.ok()) {
            printError(found);
            return;
        }

        Question q;
        if (!readQuestion(existing.question_id, &q)) {
            return;
        }
        q.is_available = existing.is_available;
        const Result result = question_bank_.updateQuestion(session_, q);
        printOutcome(result);
    }

    void handleDelete() {
        output_ << "question_id to delete: ";
        std::string id;
        if (!readLine(&id)) {
            return;
        }
        if (!confirm("Delete question " + id + "?")) {
            output_ << "Delete cancelled. Nothing was changed.\n";
            return;
        }
        const Result result = question_bank_.deleteQuestion(session_, id, true);
        printOutcome(result);
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
        if (action != "enable" && action != "disable") {
            output_ << "Error: action must be 'enable' or 'disable'.\n";
            return;
        }
        const bool enable = action == "enable";
        if (!confirm(std::string(enable ? "Enable" : "Disable") + " question " + id + "?")) {
            output_ << "Action cancelled. Nothing was changed.\n";
            return;
        }
        const Result result = question_bank_.setAvailability(session_, id, enable);
        printOutcome(result);
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

        Result result = question_bank_.exportToCsv(session_, path, subject_filter, difficulty_filter, false);
        if (result.code == ErrorCode::CONFIRMATION_REQUIRED) {
            if (!confirm("File exists. Overwrite?")) {
                output_ << "Export cancelled. The existing file was not changed.\n";
                return;
            }
            result = question_bank_.exportToCsv(session_, path, subject_filter, difficulty_filter, true);
        }
        printOutcome(result);
    }

    void handleImport() {
        output_ << "CSV input path: ";
        std::string path;
        if (!readLine(&path)) {
            return;
        }
        const ImportReport report = question_bank_.importFromCsv(session_, path);
        if (!report.result.ok()) {
            printError(report.result);
            return;
        }
        output_ << "Success: imported=" << report.imported_count << ", skipped=" << report.skipped_count << '\n';
        for (const auto& issue : report.invalid_rows) {
            output_ << "  line " << issue.row_number << ": " << issue.reason << '\n';
        }
        if (report.log_failed) {
            output_ << "Warning: some rejected rows could not be written to the import log.\n";
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
