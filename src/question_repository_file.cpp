#include "question_repository.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace quizsphere {
namespace {

constexpr const char* kStorageHeader =
    "question_id\tsubject_tag\tdifficulty\tquestion_type\tquestion_text\toptions\tcorrect_option\tis_available";

std::string encodeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string decodeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool escape = false;
    for (char c : value) {
        if (!escape) {
            if (c == '\\') {
                escape = true;
            } else {
                out.push_back(c);
            }
            continue;
        }

        switch (c) {
            case '\\':
                out.push_back('\\');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            default:
                out.push_back('\\');
                out.push_back(c);
                break;
        }
        escape = false;
    }
    if (escape) {
        out.push_back('\\');
    }
    return out;
}

std::vector<std::string> splitRawTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
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

std::string difficultyToStorage(Difficulty difficulty) {
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

bool difficultyFromStorage(const std::string& value, Difficulty* difficulty) {
    if (value == "Easy") {
        *difficulty = Difficulty::Easy;
        return true;
    }
    if (value == "Medium") {
        *difficulty = Difficulty::Medium;
        return true;
    }
    if (value == "Hard") {
        *difficulty = Difficulty::Hard;
        return true;
    }
    return false;
}

bool questionTypeFromStorage(const std::string& value, QuestionType* type) {
    if (value == "MCQ") {
        *type = QuestionType::MCQ;
        return true;
    }
    return false;
}

bool parseSize(const std::string& value, std::size_t* out) {
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

bool parseBool(const std::string& value, bool* out) {
    if (value == "true") {
        *out = true;
        return true;
    }
    if (value == "false") {
        *out = false;
        return true;
    }
    return false;
}

class FileQuestionRepository : public QuestionRepository {
public:
    explicit FileQuestionRepository(std::string path) : storage_path_(std::move(path)) {}

    Result loadAll(std::vector<Question>* questions) override {
        questions->clear();

        std::error_code exists_ec;
        const bool exists = std::filesystem::exists(storage_path_, exists_ec);
        if (exists_ec) {
            return {ErrorCode::IO_ERROR, "Unable to inspect storage file existence."};
        }

        if (!exists) {
            return {ErrorCode::OK, "Storage file not present; starting empty."};
        }

        std::ifstream in(storage_path_);
        if (!in.is_open()) {
            return {ErrorCode::IO_ERROR, "Storage file exists but cannot be opened."};
        }

        std::vector<Question> loaded;
        std::unordered_set<std::string> ids;
        std::string line;
        std::size_t line_number = 0;

        while (std::getline(in, line)) {
            ++line_number;
            if (line_number == 1) {
                if (line != kStorageHeader) {
                    questions->clear();
                    return {ErrorCode::IO_ERROR, "Storage header mismatch."};
                }
                continue;
            }

            if (line.empty()) {
                continue;
            }

            const auto raw_fields = splitRawTabs(line);
            if (raw_fields.size() != 8) {
                questions->clear();
                return {ErrorCode::IO_ERROR, "Malformed storage row at line " + std::to_string(line_number) + "."};
            }

            Question q;
            q.question_id = decodeField(raw_fields[0]);
            q.subject_tag = decodeField(raw_fields[1]);

            if (!difficultyFromStorage(decodeField(raw_fields[2]), &q.difficulty)) {
                questions->clear();
                return {ErrorCode::IO_ERROR, "Invalid difficulty at line " + std::to_string(line_number) + "."};
            }

            if (!questionTypeFromStorage(decodeField(raw_fields[3]), &q.question_type)) {
                questions->clear();
                return {ErrorCode::IO_ERROR, "Invalid question type at line " + std::to_string(line_number) + "."};
            }

            q.question_text = decodeField(raw_fields[4]);
            q.options = decodeOptionsCell(decodeField(raw_fields[5]));

            if (!parseSize(decodeField(raw_fields[6]), &q.correct_option)) {
                questions->clear();
                return {ErrorCode::IO_ERROR, "Invalid correct option at line " + std::to_string(line_number) + "."};
            }

            if (!parseBool(decodeField(raw_fields[7]), &q.is_available)) {
                questions->clear();
                return {ErrorCode::IO_ERROR, "Invalid availability at line " + std::to_string(line_number) + "."};
            }

            if (!ids.insert(q.question_id).second) {
                questions->clear();
                return {ErrorCode::IO_ERROR, "Duplicate question_id in storage."};
            }

            loaded.push_back(q);
        }

        if (in.bad()) {
            questions->clear();
            return {ErrorCode::IO_ERROR, "Failed while reading storage."};
        }

        *questions = std::move(loaded);
        return {ErrorCode::OK, "Loaded questions."};
    }

    Result saveAll(const std::vector<Question>& questions) override {
        const std::filesystem::path target(storage_path_);
        if (target.has_parent_path()) {
            std::error_code mkdir_ec;
            std::filesystem::create_directories(target.parent_path(), mkdir_ec);
            if (mkdir_ec) {
                return {ErrorCode::IO_ERROR, "Unable to create storage directory."};
            }
        }

        const std::filesystem::path temp = target.string() + ".tmp";
        std::ofstream out(temp, std::ios::trunc);
        if (!out.is_open()) {
            return {ErrorCode::IO_ERROR, "Unable to open temporary storage file."};
        }

        out << kStorageHeader << '\n';
        for (const auto& q : questions) {
            out << encodeField(q.question_id) << '\t'
                << encodeField(q.subject_tag) << '\t'
                << encodeField(difficultyToStorage(q.difficulty)) << '\t'
                << encodeField("MCQ") << '\t'
                << encodeField(q.question_text) << '\t'
                << encodeField(encodeOptionsCell(q.options)) << '\t'
                << encodeField(std::to_string(q.correct_option)) << '\t'
                << encodeField(q.is_available ? "true" : "false") << '\n';
        }

        out.flush();
        if (!out.good()) {
            out.close();
            std::error_code remove_ec;
            std::filesystem::remove(temp, remove_ec);
            return {ErrorCode::IO_ERROR, "Failed to flush temporary storage file."};
        }

        out.close();
        if (out.fail()) {
            std::error_code remove_ec;
            std::filesystem::remove(temp, remove_ec);
            return {ErrorCode::IO_ERROR, "Failed to close temporary storage file."};
        }

        std::error_code rename_ec;
        std::filesystem::rename(temp, target, rename_ec);
        if (rename_ec) {
            std::error_code remove_ec;
            std::filesystem::remove(temp, remove_ec);
            return {ErrorCode::IO_ERROR, "Failed to replace storage file."};
        }

        return {ErrorCode::OK, "Saved questions."};
    }

    Result readTextFile(const std::string& path, std::string* content) const override {
        std::error_code exists_ec;
        if (!std::filesystem::exists(path, exists_ec)) {
            return {ErrorCode::IO_ERROR, "File does not exist."};
        }
        if (exists_ec) {
            return {ErrorCode::IO_ERROR, "Unable to inspect file."};
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return {ErrorCode::IO_ERROR, "Unable to open file."};
        }

        std::ostringstream buffer;
        buffer << in.rdbuf();
        if (in.bad()) {
            return {ErrorCode::IO_ERROR, "Unable to read file."};
        }

        *content = buffer.str();
        return {ErrorCode::OK, "Read file."};
    }

    Result writeTextFileAtomic(const std::string& path,
                               const std::string& content,
                               bool overwrite) const override {
        std::error_code exists_ec;
        const bool exists = std::filesystem::exists(path, exists_ec);
        if (exists_ec) {
            return {ErrorCode::IO_ERROR, "Unable to inspect export file path."};
        }
        if (exists && !overwrite) {
            return {ErrorCode::CONFIRMATION_REQUIRED, "Export target exists. Confirmation required to overwrite."};
        }

        const std::filesystem::path target(path);
        if (target.has_parent_path()) {
            std::error_code mkdir_ec;
            std::filesystem::create_directories(target.parent_path(), mkdir_ec);
            if (mkdir_ec) {
                return {ErrorCode::IO_ERROR, "Unable to create export directory."};
            }
        }

        const std::filesystem::path temp = target.string() + ".tmp";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return {ErrorCode::IO_ERROR, "Unable to open temporary export file."};
        }

        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::error_code remove_ec;
            std::filesystem::remove(temp, remove_ec);
            return {ErrorCode::IO_ERROR, "Failed to flush export data."};
        }
        out.close();
        if (out.fail()) {
            std::error_code remove_ec;
            std::filesystem::remove(temp, remove_ec);
            return {ErrorCode::IO_ERROR, "Failed to close export file."};
        }

        std::error_code rename_ec;
        std::filesystem::rename(temp, target, rename_ec);
        if (rename_ec) {
            std::error_code remove_ec;
            std::filesystem::remove(temp, remove_ec);
            return {ErrorCode::IO_ERROR, "Failed to finalize export file."};
        }
        return {ErrorCode::OK, "Export written."};
    }

    [[nodiscard]] bool isReferenced(const std::string& /*question_id*/) const override {
        return false;
    }

private:
    std::string storage_path_;
};

}  // namespace

std::unique_ptr<QuestionRepository> createFileQuestionRepository(std::string storage_path) {
    return std::make_unique<FileQuestionRepository>(std::move(storage_path));
}

}  // namespace quizsphere
