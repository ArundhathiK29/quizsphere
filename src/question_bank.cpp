#include "question_bank.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "options_codec.hpp"

namespace quizsphere {
namespace {

constexpr const char* kCsvHeader =
    "question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available";

// A CSV larger than this is rejected before parsing (denial-of-service guard, SEC-005).
constexpr std::size_t kMaxCsvBytes = 10U * 1024U * 1024U;

// ---------------------------------------------------------------- text helpers

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

// ASCII-only lower-casing: "Math" and "math" match; accented letters are compared exactly.
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

// Counts UTF-8 code points; returns false for invalid UTF-8.
bool utf8Length(const std::string& s, std::size_t* count) {
    std::size_t i = 0;
    std::size_t n = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c < 0x80U) {
            len = 1;
            cp = c;
        } else if ((c & 0xE0U) == 0xC0U) {
            len = 2;
            cp = c & 0x1FU;
        } else if ((c & 0xF0U) == 0xE0U) {
            len = 3;
            cp = c & 0x0FU;
        } else if ((c & 0xF8U) == 0xF0U) {
            len = 4;
            cp = c & 0x07U;
        } else {
            return false;
        }
        if (i + len > s.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0U) != 0x80U) {
                return false;
            }
            cp = (cp << 6U) | (cc & 0x3FU);
        }
        if ((len == 2 && cp < 0x80U) || (len == 3 && cp < 0x800U) || (len == 4 && cp < 0x10000U)) {
            return false;  // overlong encoding
        }
        if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
            return false;
        }
        i += len;
        ++n;
    }
    *count = n;
    return true;
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

// Digits only (after trimming); no sign, no suffix.
bool parseSize(const std::string& raw, std::size_t* out) {
    const std::string value = trim(raw);
    if (value.empty() || value.size() > 18) {
        return false;
    }
    if (!std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    *out = static_cast<std::size_t>(std::stoull(value));
    return true;
}

// ---------------------------------------------------------------- authorisation
// NOTE: a Session is only checked for shape here. Real authentication is the Auth
// module's job; until it issues Sessions this check cannot detect a forged one.

bool isValidSession(const Session& session) {
    return !trim(session.user_id).empty() && !trim(session.logged_in_at).empty();
}

bool canManage(const Session& session) {
    return isValidSession(session) && (session.role == Role::Instructor || session.role == Role::Admin);
}

Result notAuthorized() {
    return {ErrorCode::NOT_AUTHORIZED, "A valid Instructor/Admin session is required."};
}

// ---------------------------------------------------------------- CSV writing

std::string csvEscape(const std::string& value) {
    const bool needs_quotes = std::any_of(value.begin(), value.end(), [](char c) {
        return c == ',' || c == '"' || c == '\n' || c == '\r';
    });
    if (!needs_quotes) {
        return value;
    }
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

bool isFormulaChar(char c) {
    return c == '=' || c == '+' || c == '-' || c == '@';
}

// True if the value is (apostrophes)* followed by a formula character. Such values get one
// extra leading apostrophe on export and lose exactly one on import, so the round trip is
// lossless even for values that already start with an apostrophe.
bool needsApostrophe(const std::string& value) {
    std::size_t i = 0;
    while (i < value.size() && value[i] == '\'') {
        ++i;
    }
    return i < value.size() && isFormulaChar(value[i]);
}

std::string neutralizeFormula(const std::string& value) {
    return needsApostrophe(value) ? "'" + value : value;
}

std::string deneutralizeFormula(const std::string& value) {
    if (!value.empty() && value[0] == '\'' && needsApostrophe(value.substr(1))) {
        return value.substr(1);
    }
    return value;
}

// ---------------------------------------------------------------- CSV reading

struct CsvRecord {
    std::size_t line{0};  // physical line where the record starts (1-based)
    std::vector<std::string> fields;
    bool malformed{false};
};

// Parses RFC-4180 style records from the whole content: quoted fields may contain commas,
// doubled quotes, CR and LF. Accepts CRLF/LF/CR line ends and a UTF-8 BOM. Blank lines are
// skipped. A record with bad quoting is flagged malformed and parsing continues with the next
// physical line, so one bad row never hides the rows after it. If one quoted field never closes,
// every later quote in the file is part of a doubled pair, so no second unterminated field can
// exist: the re-scan therefore costs at most one extra pass (no quadratic behaviour).
void parseCsvRecords(std::string data, std::vector<CsvRecord>* records) {
    records->clear();
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEFU &&
        static_cast<unsigned char>(data[1]) == 0xBBU && static_cast<unsigned char>(data[2]) == 0xBFU) {
        data.erase(0, 3);
    }

    std::size_t pos = 0;
    std::size_t line = 1;

    while (pos < data.size()) {
        std::vector<std::string> fields;
        std::string field;
        bool in_quotes = false;
        bool at_field_start = true;
        bool after_close = false;
        bool malformed = false;
        bool any_quoted = false;
        bool terminated = false;
        std::size_t extra_lines = 0;
        std::size_t i = pos;

        for (; i < data.size(); ++i) {
            const char c = data[i];
            if (in_quotes) {
                if (c == '"') {
                    if (i + 1 < data.size() && data[i + 1] == '"') {
                        field.push_back('"');
                        ++i;
                    } else {
                        in_quotes = false;
                        after_close = true;
                    }
                } else {
                    if (c == '\n' || (c == '\r' && !(i + 1 < data.size() && data[i + 1] == '\n'))) {
                        ++extra_lines;
                    }
                    field.push_back(c);
                }
                continue;
            }

            if (c == ',') {
                fields.push_back(field);
                field.clear();
                at_field_start = true;
                after_close = false;
                continue;
            }
            if (c == '\r' || c == '\n') {
                if (c == '\r' && i + 1 < data.size() && data[i + 1] == '\n') {
                    ++i;
                }
                ++i;
                terminated = true;
                break;
            }
            if (c == '"') {
                if (at_field_start && !after_close) {
                    in_quotes = true;
                    any_quoted = true;
                } else {
                    malformed = true;
                    field.push_back(c);
                }
                at_field_start = false;
                continue;
            }
            if (after_close) {
                malformed = true;  // text after a closing quote
            }
            field.push_back(c);
            at_field_start = false;
        }

        if (in_quotes) {
            // Unterminated quoted field: flag this record, resume at the next physical line.
            records->push_back({line, {}, true});
            const std::size_t newline = data.find_first_of("\r\n", pos);
            if (newline == std::string::npos) {
                pos = data.size();
            } else {
                pos = newline + 1;
                if (data[newline] == '\r' && pos < data.size() && data[pos] == '\n') {
                    ++pos;
                }
            }
            ++line;
            continue;
        }

        fields.push_back(field);
        const bool blank = fields.size() == 1 && !any_quoted && !malformed && trim(fields[0]).empty();
        if (!blank) {
            records->push_back({line, std::move(fields), malformed});
        }
        line += extra_lines + (terminated ? 1U : 0U);
        pos = i;
    }
}

// ---------------------------------------------------------------- filters

bool subjectMatches(const std::string& subject, const std::optional<std::string>& filter) {
    if (!filter.has_value()) {
        return true;
    }
    return toLower(trim(subject)) == toLower(trim(*filter));
}

bool difficultyMatches(Difficulty difficulty, const std::optional<Difficulty>& filter) {
    return !filter.has_value() || difficulty == *filter;
}

PublicQuestion makePublic(const Question& q) {
    return PublicQuestion{q.question_id, q.subject_tag, q.difficulty, q.question_type,
                          q.question_text, q.options, q.is_available};
}

// ---------------------------------------------------------------- the bank

class CoreQuestionBank final : public QuestionBank {
public:
    CoreQuestionBank(std::unique_ptr<QuestionRepository> repository, std::unique_ptr<ImportLogger> logger)
        : repository_(std::move(repository)),
          logger_(logger ? std::move(logger) : createFileImportLogger()) {}

    Result load() override {
        std::vector<Question> loaded;
        const Result result = repository_->loadAll(&loaded);
        if (!result.ok()) {
            questions_.clear();
            return result;
        }

        std::unordered_set<std::string> ids;
        for (const auto& question : loaded) {
            const Result validation = validateQuestion(question);
            if (!validation.ok()) {
                questions_.clear();
                return {ErrorCode::IO_ERROR, "Invalid question found in storage: " + validation.message};
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
        const Question normalized = normalize(question);
        const Result validation = validateQuestion(normalized);
        if (!validation.ok()) {
            return validation;
        }
        if (findIndexById(normalized.question_id).has_value()) {
            return {ErrorCode::DUPLICATE_ID, "Question ID already exists."};
        }

        questions_.push_back(normalized);
        const Result persisted = repository_->saveAll(questions_);
        if (!persisted.ok()) {
            questions_.pop_back();
            return persisted;
        }
        return {ErrorCode::OK, "Question added."};
    }

    Result updateQuestion(const Session& session, const Question& question) override {
        if (!canManage(session)) {
            return notAuthorized();
        }
        const Question normalized = normalize(question);
        const Result validation = validateQuestion(normalized);
        if (!validation.ok()) {
            return validation;
        }
        const auto index = findIndexById(normalized.question_id);
        if (!index.has_value()) {
            return {ErrorCode::QUESTION_NOT_FOUND, "Question not found."};
        }
        if (repository_->isReferenced(normalized.question_id)) {
            return {ErrorCode::QUESTION_IN_USE, "Question is referenced by a quiz/attempt and cannot be edited."};
        }

        const Question previous = questions_[*index];
        questions_[*index] = normalized;
        const Result persisted = repository_->saveAll(questions_);
        if (!persisted.ok()) {
            questions_[*index] = previous;
            return persisted;
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
            return {ErrorCode::QUESTION_IN_USE, "Question is referenced by a quiz/attempt and cannot be deleted."};
        }

        const Question removed = questions_[*index];
        questions_.erase(questions_.begin() + static_cast<std::ptrdiff_t>(*index));
        const Result persisted = repository_->saveAll(questions_);
        if (!persisted.ok()) {
            questions_.insert(questions_.begin() + static_cast<std::ptrdiff_t>(*index), removed);
            return persisted;
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
        const Result persisted = repository_->saveAll(questions_);
        if (!persisted.ok()) {
            questions_[*index].is_available = previous;
            return persisted;
        }
        return {ErrorCode::OK, is_available ? "Question enabled." : "Question disabled."};
    }

    Result findQuestionById(const Session& session, const std::string& question_id, Question* question) const override {
        if (!canManage(session)) {
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
        if (!canManage(session)) {
            return notAuthorized();
        }
        questions->clear();
        for (const auto& q : questions_) {
            if (available_only && !q.is_available) {
                continue;
            }
            if (!subjectMatches(q.subject_tag, subject_filter) || !difficultyMatches(q.difficulty, difficulty_filter)) {
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
            if (!subjectMatches(q.subject_tag, subject_filter) || !difficultyMatches(q.difficulty, difficulty_filter)) {
                continue;
            }
            questions->push_back(makePublic(q));
        }
        return {ErrorCode::OK, "Available questions listed."};
    }

    ImportReport importFromCsv(const Session& session, const std::string& csv_path) override {
        if (!canManage(session)) {
            return {notAuthorized(), 0, 0, {}, false};
        }

        // Structural failures reject the whole file and leave the bank untouched.
        std::string content;
        if (!repository_->readTextFile(csv_path, &content).ok()) {
            return structureInvalid("CSV file cannot be opened.");
        }
        if (content.size() > kMaxCsvBytes) {
            return structureInvalid("CSV file is larger than 10 MB.");
        }
        std::vector<CsvRecord> records;
        parseCsvRecords(content, &records);
        if (records.empty()) {
            return structureInvalid("CSV file is empty or has no header.");
        }
        if (!headerMatches(records.front())) {
            return structureInvalid("CSV header is invalid. Expected: " + std::string(kCsvHeader));
        }

        std::vector<Question> staged;
        ImportReport report;
                std::unordered_set<std::string> seen_ids;
        std::transform(questions_.begin(), questions_.end(), std::inserter(seen_ids, seen_ids.end()),
                       [](const Question& existing) { return existing.question_id; });

        for (std::size_t i = 1; i < records.size(); ++i) {
            const CsvRecord& row = records[i];
            const auto skip = [&](const std::string& reason) {
                report.invalid_rows.push_back({row.line, reason});
                if (!logger_->logRejectedRow(row.line, reason)) {
                    report.log_failed = true;
                }
            };

            if (row.malformed) {
                skip("Malformed CSV quoting.");
                continue;
            }
            if (row.fields.size() != 8) {
                skip("Row must contain exactly 8 columns.");
                continue;
            }

            Question q;
            q.question_id = deneutralizeFormula(row.fields[0]);
            q.subject_tag = deneutralizeFormula(row.fields[1]);
            q.question_text = deneutralizeFormula(row.fields[4]);
            q.options = decodeOptionsCell(deneutralizeFormula(row.fields[5]));

            const auto difficulty = parseDifficulty(row.fields[2]);
            if (!difficulty.has_value()) {
                skip("Difficulty must be Easy, Medium, or Hard.");
                continue;
            }
            q.difficulty = *difficulty;

            const auto type = parseQuestionType(row.fields[3]);
            if (!type.has_value()) {
                skip("Question type must be MCQ.");
                continue;
            }
            q.question_type = *type;

            if (!parseSize(row.fields[6], &q.correct_option)) {
                skip("correct_option must be a non-negative whole number.");
                continue;
            }
            if (!parseBool(row.fields[7], &q.is_available)) {
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
            staged.push_back(std::move(q));
        }

        report.skipped_count = report.invalid_rows.size();
        report.result = {ErrorCode::OK, "Import complete."};
        if (staged.empty()) {
            return report;
        }

        // All valid rows are stored with ONE save; on failure nothing changes.
        const auto snapshot = questions_;
        questions_.insert(questions_.end(), staged.begin(), staged.end());
        const Result persisted = repository_->saveAll(questions_);
        if (!persisted.ok()) {
            questions_ = snapshot;
            report.result = {ErrorCode::IO_ERROR, "Failed to store imported rows: " + persisted.message};
            return report;
        }
        report.imported_count = staged.size();
        return report;
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
            if (!subjectMatches(q.subject_tag, subject_filter) || !difficultyMatches(q.difficulty, difficulty_filter)) {
                continue;
            }
            out << csvEscape(neutralizeFormula(q.question_id)) << ','
                << csvEscape(neutralizeFormula(q.subject_tag)) << ','
                << difficultyToString(q.difficulty) << ','
                << questionTypeToString(q.question_type) << ','
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
    static ImportReport structureInvalid(const std::string& message) {
        ImportReport report;
        report.result = {ErrorCode::CSV_STRUCTURE_INVALID, message};
        return report;
    }

    static bool headerMatches(const CsvRecord& header) {
        static const char* const kNames[8] = {"question_id", "subject_tag", "difficulty", "question_type",
                                              "question_text", "options", "correct_option", "is_available"};
        if (header.malformed || header.fields.size() != 8) {
            return false;
        }
        for (std::size_t i = 0; i < 8; ++i) {
            if (trim(header.fields[i]) != kNames[i]) {
                return false;
            }
        }
        return true;
    }

    static Question normalize(const Question& original) {
        Question q = original;
        q.question_id = trim(q.question_id);
        q.subject_tag = trim(q.subject_tag);
        return q;
    }

    // Checks one short text field: not empty, code-point length, valid UTF-8, no control characters.
    static Result checkShortField(const std::string& value, const char* name, std::size_t max_chars) {
        if (trim(value).empty()) {
            return {ErrorCode::VALIDATION_FAILED, std::string(name) + " is required."};
        }
        std::size_t length = 0;
        if (!utf8Length(value, &length)) {
            return {ErrorCode::VALIDATION_FAILED, std::string(name) + " is not valid UTF-8 text."};
        }
        if (length > max_chars) {
            return {ErrorCode::VALIDATION_FAILED,
                    std::string(name) + " must be " + std::to_string(max_chars) + " characters or fewer."};
        }
        if (hasDisallowedControlChars(value, false)) {
            return {ErrorCode::VALIDATION_FAILED, std::string(name) + " contains control characters."};
        }
        return {ErrorCode::OK, ""};
    }

    static Result validateQuestion(const Question& q) {
        if (trim(q.question_id) != q.question_id || trim(q.subject_tag) != q.subject_tag) {
            return {ErrorCode::VALIDATION_FAILED, "question_id and subject_tag must not have surrounding spaces."};
        }
        Result r = checkShortField(q.question_id, "question_id", 36);
        if (!r.ok()) {
            return r;
        }
        r = checkShortField(q.subject_tag, "subject_tag", 40);
        if (!r.ok()) {
            return r;
        }
        if (q.question_type != QuestionType::MCQ) {
            return {ErrorCode::VALIDATION_FAILED, "Only MCQ question type is supported."};
        }

        if (trim(q.question_text).empty()) {
            return {ErrorCode::VALIDATION_FAILED, "question_text is required."};
        }
        std::size_t text_length = 0;
        if (!utf8Length(q.question_text, &text_length)) {
            return {ErrorCode::VALIDATION_FAILED, "question_text is not valid UTF-8 text."};
        }
        if (text_length > 500) {
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
            r = checkShortField(option, "Each option", 200);
            if (!r.ok()) {
                return {ErrorCode::VALIDATION_FAILED, "Options must be non-empty, valid text of 200 characters or fewer, without control characters."};
            }
            if (!seen_options.insert(toLower(trim(option))).second) {
                return {ErrorCode::VALIDATION_FAILED, "Duplicate options are not allowed."};
            }
        }

        if (q.correct_option >= q.options.size()) {
            return {ErrorCode::VALIDATION_FAILED, "correct_option must refer to an existing option."};
        }
        if (std::to_string(q.correct_option).size() > 5) {
            return {ErrorCode::VALIDATION_FAILED, "correct_option is too large."};
        }
        return {ErrorCode::OK, "Valid question."};
    }
    [[nodiscard]] std::optional<std::size_t> findIndexById(const std::string& id) const {
        const auto it = std::find_if(questions_.begin(), questions_.end(),
                                     [&id](const Question& q) { return q.question_id == id; });
        if (it == questions_.end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(it - questions_.begin());
    }
    std::unique_ptr<QuestionRepository> repository_;
    std::unique_ptr<ImportLogger> logger_;
    std::vector<Question> questions_;
};

}  // namespace

std::unique_ptr<QuestionBank> createQuestionBank(std::unique_ptr<QuestionRepository> repository,
                                                 std::unique_ptr<ImportLogger> logger) {
    return std::make_unique<CoreQuestionBank>(std::move(repository), std::move(logger));
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
