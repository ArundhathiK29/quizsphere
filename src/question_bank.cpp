#include "question_bank.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "import_logger.hpp"

namespace quizsphere {
namespace {

constexpr const char* kCsvHeader =
    "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available";

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool hasDisallowedControlChars(const std::string& value, bool allow_text_controls) {
    return std::any_of(value.begin(), value.end(), [allow_text_controls](char raw) {
        const auto c = static_cast<unsigned char>(raw);
        if (c >= 32U && c != 127U) {
            return false;
        }
        return !(allow_text_controls && (c == '\n' || c == '\r' || c == '\t'));
    });
}

bool isValidSession(const Session& session) {
    return !trim(session.user_id).empty() && !trim(session.logged_in_at).empty();
}

bool canManage(const Session& session) {
    return isValidSession(session) && (session.role == Role::Instructor || session.role == Role::Admin);
}

bool canReadAnswers(const Session& session) {
    return isValidSession(session) && (session.role == Role::Instructor || session.role == Role::Admin);
}

Result notAuthorized() {
    return {ErrorCode::NOT_AUTHORIZED, "A valid Instructor/Admin session is required."};
}

std::string encodeOptionsCell(const std::vector<std::string>& options) {
    std::string out;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i > 0) {
            out.push_back('|');
        }
        for (char c : options[i]) {
            if (c == '\\' || c == '|') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
    }
    return out;
}

std::vector<std::string> decodeOptionsCell(const std::string& cell) {
    std::vector<std::string> options;
    std::string current;
    bool escape = false;
    for (char c : cell) {
        if (!escape) {
            if (c == '\\') {
                escape = true;
            } else if (c == '|') {
                options.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
            continue;
        }

        if (c == '\\' || c == '|') {
            current.push_back(c);
        } else {
            current.push_back('\\');
            current.push_back(c);
        }
        escape = false;
    }
    if (escape) {
        current.push_back('\\');
    }
    options.push_back(current);
    return options;
}

std::string csvEscape(const std::string& value) {
    const bool needs_quotes = std::any_of(value.begin(), value.end(), [](char c) {
        return c == ',' || c == '"' || c == '\n' || c == '\r';
    });
    if (!needs_quotes) {
        return value;
    }

    std::string out = "\"";
    for (char c : value) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

bool startsFormula(const std::string& value) {
    return !value.empty() && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@');
}

std::string neutralizeFormula(const std::string& value) {
    if (startsFormula(value)) {
        return "'" + value;
    }
    return value;
}

std::string deneutralizeFormula(const std::string& value) {
    if (value.size() >= 2 && value[0] == '\'' && startsFormula(value.substr(1))) {
        return value.substr(1);
    }
    return value;
}

bool parseBool(const std::string& raw, bool* out) {
    const std::string value = toLower(trim(raw));
    if (value == "true" || value == "1" || value == "yes") {
        *out = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        *out = false;
        return true;
    }
    return false;
}

bool parseSize(const std::string& raw, std::size_t* out) {
    const std::string value = trim(raw);
    if (value.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        *out = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

struct CsvRecord {
    std::size_t row_number{0};
    std::vector<std::string> fields;
    bool malformed_quoting{false};
};

std::string trimFieldForHeader(std::string field) {
    field = trim(field);
    if (!field.empty() && field.back() == '\r') {
        field.pop_back();
    }
    return trim(field);
}

std::vector<CsvRecord> parseCsvRecords(const std::string& content) {
    std::string data = content;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.erase(0, 3);
    }

    std::vector<CsvRecord> records;
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    bool malformed = false;
    std::size_t row = 1;

    auto commit_record = [&](bool force) {
        if (!force && fields.empty() && field.empty() && !malformed) {
            return;
        }
        fields.push_back(field);
        records.push_back({row, fields, malformed});
        ++row;
        fields.clear();
        field.clear();
        in_quotes = false;
        malformed = false;
    };

    for (std::size_t i = 0; i < data.size(); ++i) {
        const char c = data[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < data.size() && data[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }

        if (c == ',') {
            fields.push_back(field);
            field.clear();
            continue;
        }

        if (c == '"') {
            if (!field.empty()) {
                malformed = true;
                field.push_back(c);
            } else {
                in_quotes = true;
            }
            continue;
        }

        if (c == '\n') {
            commit_record(false);
            continue;
        }

        if (c == '\r') {
            if (i + 1 < data.size() && data[i + 1] == '\n') {
                ++i;
            }
            commit_record(false);
            continue;
        }

        field.push_back(c);
    }

    if (in_quotes) {
        malformed = true;
    }

    if (!fields.empty() || !field.empty() || malformed) {
        commit_record(true);
    }

    return records;
}

bool subjectMatches(const std::string& subject, const std::optional<std::string>& filter) {
    if (!filter.has_value()) {
        return true;
    }
    return toLower(trim(subject)) == toLower(trim(*filter));
}

bool difficultyMatches(Difficulty difficulty, const std::optional<Difficulty>& filter) {
    if (!filter.has_value()) {
        return true;
    }
    return difficulty == *filter;
}

PublicQuestion makePublic(const Question& q) {
    return PublicQuestion{q.question_id,
                          q.subject_tag,
                          q.difficulty,
                          q.question_type,
                          q.question_text,
                          q.options,
                          q.is_available};
}

class CoreQuestionBank : public QuestionBank {
public:
    explicit CoreQuestionBank(std::unique_ptr<QuestionRepository> repository)
        : repository_(std::move(repository)) {}

    Result load() override {
        std::vector<Question> loaded;
        const Result result = repository_->loadAll(&loaded);
        if (!result.ok()) {
            questions_.clear();
            return result;
        }

        std::unordered_set<std::string> ids;
        for (const auto& question : loaded) {
            if (!validateQuestion(question).ok()) {
                questions_.clear();
                return {ErrorCode::IO_ERROR, "Invalid question found in storage."};
            }
            if (!ids.insert(question.question_id).second) {
                questions_.clear();
                return {ErrorCode::IO_ERROR, "Duplicate question_id found in storage."};
            }
        }

        questions_ = std::move(loaded);
        return {ErrorCode::OK, "Question bank loaded."};
    }

    Result addQuestion(const Session& session, const Question& question) override {
        if (!canManage(session)) {
            return notAuthorized();
        }

        Question normalized = normalize(question);
        const Result validation = validateQuestion(normalized);
        if (!validation.ok()) {
            return validation;
        }

        if (findIndexById(normalized.question_id).has_value()) {
            return {ErrorCode::DUPLICATE_ID, "Question ID already exists."};
        }

        questions_.push_back(normalized);
        const Result persist = repository_->saveAll(questions_);
        if (!persist.ok()) {
            questions_.pop_back();
            return persist;
        }
        return {ErrorCode::OK, "Question added."};
    }

    Result updateQuestion(const Session& session, const Question& question) override {
        if (!canManage(session)) {
            return notAuthorized();
        }

        Question normalized = normalize(question);
        const Result validation = validateQuestion(normalized);
        if (!validation.ok()) {
            return validation;
        }

        const auto index = findIndexById(normalized.question_id);
        if (!index.has_value()) {
            return {ErrorCode::QUESTION_NOT_FOUND, "Question not found."};
        }

        if (repository_->isReferenced(normalized.question_id)) {
            return {ErrorCode::QUESTION_IN_USE, "Question is referenced by quiz/attempt and cannot be edited."};
        }

        const Question old = questions_[*index];
        questions_[*index] = normalized;
        const Result persist = repository_->saveAll(questions_);
        if (!persist.ok()) {
            questions_[*index] = old;
            return persist;
        }

        return {ErrorCode::OK, "Question updated."};
    }

    Result deleteQuestion(const Session& session, const std::string& question_id, bool confirmed) override {
        if (!canManage(session)) {
            return notAuthorized();
        }
        if (!confirmed) {
            return {ErrorCode::CONFIRMATION_REQUIRED, "Confirm deletion with yes/no and retry."};
        }

        const std::string id = trim(question_id);
        const auto index = findIndexById(id);
        if (!index.has_value()) {
            return {ErrorCode::QUESTION_NOT_FOUND, "Question not found."};
        }

        if (repository_->isReferenced(id)) {
            return {ErrorCode::QUESTION_IN_USE, "Question is referenced by quiz/attempt and cannot be deleted."};
        }

        const Question removed = questions_[*index];
        questions_.erase(questions_.begin() + static_cast<std::ptrdiff_t>(*index));
        const Result persist = repository_->saveAll(questions_);
        if (!persist.ok()) {
            questions_.insert(questions_.begin() + static_cast<std::ptrdiff_t>(*index), removed);
            return persist;
        }

        return {ErrorCode::OK, "Question deleted."};
    }

    Result setAvailability(const Session& session, const std::string& question_id, bool is_available) override {
        if (!canManage(session)) {
            return notAuthorized();
        }

        const auto index = findIndexById(trim(question_id));
        if (!index.has_value()) {
            return {ErrorCode::QUESTION_NOT_FOUND, "Question not found."};
        }

        const bool previous = questions_[*index].is_available;
        questions_[*index].is_available = is_available;
        const Result persist = repository_->saveAll(questions_);
        if (!persist.ok()) {
            questions_[*index].is_available = previous;
            return persist;
        }
        return {ErrorCode::OK, is_available ? "Question enabled." : "Question disabled."};
    }

    Result findQuestionById(const Session& session, const std::string& question_id, Question* question) const override {
        if (!canReadAnswers(session)) {
            return notAuthorized();
        }

        const auto index = findIndexById(trim(question_id));
        if (!index.has_value()) {
            return {ErrorCode::QUESTION_NOT_FOUND, "Question not found."};
        }

        *question = questions_[*index];
        return {ErrorCode::OK, "Question found."};
    }

    Result listQuestions(const Session& session,
                         std::vector<Question>* questions,
                         bool available_only,
                         const std::optional<std::string>& subject_filter,
                         const std::optional<Difficulty>& difficulty_filter) const override {
        if (!canReadAnswers(session)) {
            return notAuthorized();
        }

        questions->clear();
        for (const auto& q : questions_) {
            if (available_only && !q.is_available) {
                continue;
            }
            if (!subjectMatches(q.subject_tag, subject_filter)) {
                continue;
            }
            if (!difficultyMatches(q.difficulty, difficulty_filter)) {
                continue;
            }
            questions->push_back(q);
        }
        return {ErrorCode::OK, "Questions listed."};
    }

    Result getQuestionForQuiz(const std::string& question_id, PublicQuestion* question) const override {
        const auto index = findIndexById(trim(question_id));
        if (!index.has_value()) {
            return {ErrorCode::QUESTION_NOT_FOUND, "Question not found."};
        }
        *question = makePublic(questions_[*index]);
        return {ErrorCode::OK, "Question found."};
    }

    Result listAvailable(std::vector<PublicQuestion>* questions,
                         const std::optional<std::string>& subject_filter,
                         const std::optional<Difficulty>& difficulty_filter) const override {
        questions->clear();
        for (const auto& q : questions_) {
            if (!q.is_available) {
                continue;
            }
            if (!subjectMatches(q.subject_tag, subject_filter)) {
                continue;
            }
            if (!difficultyMatches(q.difficulty, difficulty_filter)) {
                continue;
            }
            questions->push_back(makePublic(q));
        }
        return {ErrorCode::OK, "Available questions listed."};
    }

    ImportReport importFromCsv(const Session& session, const std::string& csv_path) override {
        if (!canManage(session)) {
            return {{ErrorCode::NOT_AUTHORIZED, "A valid Instructor/Admin session is required."}, 0, 0, {}};
        }

        std::string content;
        const Result read = repository_->readTextFile(csv_path, &content);
        if (!read.ok()) {
            return {{ErrorCode::CSV_STRUCTURE_INVALID, "CSV file cannot be opened."}, 0, 0, {}};
        }

        const auto records = parseCsvRecords(content);
        if (records.empty()) {
            return {{ErrorCode::CSV_STRUCTURE_INVALID, "CSV file is empty or has no header."}, 0, 0, {}};
        }

        const auto& header = records.front();
        if (header.malformed_quoting || header.fields.size() != 8 ||
            trimFieldForHeader(header.fields[0]) != "question_id" ||
            trimFieldForHeader(header.fields[1]) != "subject_tag" ||
            trimFieldForHeader(header.fields[2]) != "difficulty" ||
            trimFieldForHeader(header.fields[3]) != "question_type" ||
            trimFieldForHeader(header.fields[4]) != "question_text" ||
            trimFieldForHeader(header.fields[5]) != "options" ||
            trimFieldForHeader(header.fields[6]) != "correct_option" ||
            trimFieldForHeader(header.fields[7]) != "is_available") {
            return {{ErrorCode::CSV_STRUCTURE_INVALID, "CSV header is invalid."}, 0, 0, {}};
        }

        if (records.size() == 1) {
            return {{ErrorCode::OK, "Import complete."}, 0, 0, {}};
        }

        std::vector<Question> staged;
        std::vector<ImportIssue> invalid_rows;
        std::unordered_set<std::string> seen_ids;
        for (const auto& existing : questions_) {
            seen_ids.insert(existing.question_id);
        }

        for (std::size_t i = 1; i < records.size(); ++i) {
            const auto& row = records[i];

            auto skip = [&](const std::string& reason) {
                invalid_rows.push_back({row.row_number, reason});
                logRejectedImportRow(reason, row.row_number);
            };

            if (row.malformed_quoting) {
                skip("Malformed CSV quoting.");
                continue;
            }
            if (row.fields.size() != 8) {
                skip("Row must contain exactly 8 columns.");
                continue;
            }

            Question q{};
            q.question_id = deneutralizeFormula(row.fields[0]);
            q.subject_tag = deneutralizeFormula(row.fields[1]);
            const auto difficulty = parseDifficulty(deneutralizeFormula(row.fields[2]));
            const auto type = parseQuestionType(deneutralizeFormula(row.fields[3]));
            q.question_text = deneutralizeFormula(row.fields[4]);
            q.options = decodeOptionsCell(deneutralizeFormula(row.fields[5]));

            if (!difficulty.has_value()) {
                skip("Difficulty must be Easy, Medium, or Hard.");
                continue;
            }
            q.difficulty = *difficulty;

            if (!type.has_value()) {
                skip("Question type must be MCQ.");
                continue;
            }
            q.question_type = *type;

            if (!parseSize(deneutralizeFormula(row.fields[6]), &q.correct_option)) {
                skip("correct_option must be a numeric index.");
                continue;
            }

            if (!parseBool(deneutralizeFormula(row.fields[7]), &q.is_available)) {
                skip("is_available must be true/false, yes/no, or 1/0.");
                continue;
            }

            q = normalize(q);
            const Result validation = validateQuestion(q);
            if (!validation.ok()) {
                skip(validation.message);
                continue;
            }

            if (!seen_ids.insert(q.question_id).second) {
                skip("Duplicate question_id; existing IDs are not overwritten.");
                continue;
            }

            staged.push_back(q);
        }

        if (staged.empty()) {
            return {{ErrorCode::OK, "Import complete."}, 0, invalid_rows.size(), invalid_rows};
        }

        const auto snapshot = questions_;
        questions_.insert(questions_.end(), staged.begin(), staged.end());
        const Result persist = repository_->saveAll(questions_);
        if (!persist.ok()) {
            questions_ = snapshot;
            return {{ErrorCode::IO_ERROR, "Failed to persist imported rows."}, 0, invalid_rows.size(), invalid_rows};
        }

        return {{ErrorCode::OK, "Import complete."}, staged.size(), invalid_rows.size(), invalid_rows};
    }

    Result exportToCsv(const Session& session,
                       const std::string& csv_path,
                       const std::optional<std::string>& subject_filter,
                       const std::optional<Difficulty>& difficulty_filter,
                       bool overwrite) const override {
        if (!canManage(session)) {
            return notAuthorized();
        }

        std::ostringstream out;
        out << kCsvHeader << '\n';

        for (const auto& q : questions_) {
            if (!subjectMatches(q.subject_tag, subject_filter)) {
                continue;
            }
            if (!difficultyMatches(q.difficulty, difficulty_filter)) {
                continue;
            }

            out << csvEscape(neutralizeFormula(q.question_id)) << ','
                << csvEscape(neutralizeFormula(q.subject_tag)) << ','
                << csvEscape(neutralizeFormula(difficultyToString(q.difficulty))) << ','
                << csvEscape(neutralizeFormula(questionTypeToString(q.question_type))) << ','
                << csvEscape(neutralizeFormula(q.question_text)) << ','
                << csvEscape(neutralizeFormula(encodeOptionsCell(q.options))) << ','
                << q.correct_option << ','
                << (q.is_available ? "true" : "false") << '\n';
        }

        if (!out.good()) {
            return {ErrorCode::IO_ERROR, "Failed to prepare CSV content."};
        }

        return repository_->writeTextFileAtomic(csv_path, out.str(), overwrite);
    }

private:
    static Question normalize(const Question& original) {
        Question q = original;
        q.question_id = trim(q.question_id);
        q.subject_tag = trim(q.subject_tag);
        return q;
    }

    Result validateQuestion(const Question& q) const {
        if (q.question_id.empty()) {
            return {ErrorCode::VALIDATION_FAILED, "question_id is required."};
        }
        if (q.question_id.size() > 36) {
            return {ErrorCode::VALIDATION_FAILED, "question_id must be 36 characters or fewer."};
        }
        if (hasDisallowedControlChars(q.question_id, false)) {
            return {ErrorCode::VALIDATION_FAILED, "question_id contains disallowed control characters."};
        }

        if (q.subject_tag.empty()) {
            return {ErrorCode::VALIDATION_FAILED, "subject_tag is required."};
        }
        if (q.subject_tag.size() > 40) {
            return {ErrorCode::VALIDATION_FAILED, "subject_tag must be 40 characters or fewer."};
        }
        if (hasDisallowedControlChars(q.subject_tag, false)) {
            return {ErrorCode::VALIDATION_FAILED, "subject_tag contains disallowed control characters."};
        }

        if (q.question_type != QuestionType::MCQ) {
            return {ErrorCode::VALIDATION_FAILED, "Only MCQ question type is supported."};
        }

        if (trim(q.question_text).empty()) {
            return {ErrorCode::VALIDATION_FAILED, "question_text is required."};
        }
        if (q.question_text.size() > 500) {
            return {ErrorCode::VALIDATION_FAILED, "question_text must be 500 characters or fewer."};
        }
        if (hasDisallowedControlChars(q.question_text, true)) {
            return {ErrorCode::VALIDATION_FAILED, "question_text contains unsupported control characters."};
        }

        if (q.options.size() < 2) {
            return {ErrorCode::VALIDATION_FAILED, "At least two options are required."};
        }

        std::unordered_set<std::string> seen_options;
        for (const auto& option : q.options) {
            const std::string trimmed = trim(option);
            if (trimmed.empty()) {
                return {ErrorCode::VALIDATION_FAILED, "Options must not be empty or whitespace-only."};
            }
            if (trimmed.size() > 200) {
                return {ErrorCode::VALIDATION_FAILED, "Each option must be 200 characters or fewer."};
            }
            if (hasDisallowedControlChars(option, false)) {
                return {ErrorCode::VALIDATION_FAILED, "Options contain disallowed control characters."};
            }
            const std::string normalized = toLower(trimmed);
            if (!seen_options.insert(normalized).second) {
                return {ErrorCode::VALIDATION_FAILED, "Duplicate options are not allowed."};
            }
        }

        if (q.correct_option >= q.options.size()) {
            return {ErrorCode::VALIDATION_FAILED, "correct_option must reference an existing option."};
        }
        if (std::to_string(q.correct_option).size() > 5) {
            return {ErrorCode::VALIDATION_FAILED, "correct_option is too large."};
        }

        return {ErrorCode::OK, "Valid question."};
    }

    std::optional<std::size_t> findIndexById(const std::string& id) const {
        for (std::size_t i = 0; i < questions_.size(); ++i) {
            if (questions_[i].question_id == id) {
                return i;
            }
        }
        return std::nullopt;
    }

    std::unique_ptr<QuestionRepository> repository_;
    std::vector<Question> questions_;
};

}  // namespace

std::unique_ptr<QuestionBank> createQuestionBank(std::unique_ptr<QuestionRepository> repository) {
    return std::make_unique<CoreQuestionBank>(std::move(repository));
}

std::optional<Session> createDemoSession() {
    return Session{"demo-instructor", Role::Instructor, "1970-01-01T00:00:00Z"};
}

std::string difficultyToString(Difficulty difficulty) {
    switch (difficulty) {
        case Difficulty::Easy:
            return "Easy";
        case Difficulty::Medium:
            return "Medium";
        case Difficulty::Hard:
            return "Hard";
    }
    return "Easy";
}

std::optional<Difficulty> parseDifficulty(std::string text) {
    text = toLower(trim(text));
    if (text == "easy") {
        return Difficulty::Easy;
    }
    if (text == "medium") {
        return Difficulty::Medium;
    }
    if (text == "hard") {
        return Difficulty::Hard;
    }
    return std::nullopt;
}

std::string questionTypeToString(QuestionType type) {
    switch (type) {
        case QuestionType::MCQ:
            return "MCQ";
    }
    return "MCQ";
}

std::optional<QuestionType> parseQuestionType(std::string text) {
    text = toLower(trim(text));
    if (text == "mcq") {
        return QuestionType::MCQ;
    }
    return std::nullopt;
}

}  // namespace quizsphere
