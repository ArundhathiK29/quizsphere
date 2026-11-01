#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "import_logger.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"

namespace quizsphere {

class QuestionBank {
public:
    virtual ~QuestionBank() = default;

    virtual Result load() = 0;
    virtual Result addQuestion(const Session& session, const Question& question) = 0;
    virtual Result updateQuestion(const Session& session, const Question& question) = 0;
    virtual Result deleteQuestion(const Session& session, const std::string& question_id, bool confirmed) = 0;
    virtual Result setAvailability(const Session& session, const std::string& question_id, bool is_available) = 0;

    // Instructor/Admin only: these expose the correct answer.
    virtual Result findQuestionById(const Session& session, const std::string& question_id, Question* question) const = 0;
    virtual Result listQuestions(const Session& session,
                                 std::vector<Question>* questions,
                                 bool available_only,
                                 const std::optional<std::string>& subject_filter,
                                 const std::optional<Difficulty>& difficulty_filter) const = 0;

    // For the Quiz Engine: never exposes the correct answer.
    virtual Result getQuestionForQuiz(const std::string& question_id, PublicQuestion* question) const = 0;
    virtual Result listAvailable(std::vector<PublicQuestion>* questions,
                                 const std::optional<std::string>& subject_filter,
                                 const std::optional<Difficulty>& difficulty_filter) const = 0;

    virtual ImportReport importFromCsv(const Session& session, const std::string& csv_path) = 0;
    virtual Result exportToCsv(const Session& session,
                               const std::string& csv_path,
                               const std::optional<std::string>& subject_filter,
                               const std::optional<Difficulty>& difficulty_filter,
                               bool overwrite) const = 0;
};

// If logger is null, rejected import rows go to logs/question_import.log.
std::unique_ptr<QuestionBank> createQuestionBank(std::unique_ptr<QuestionRepository> repository,
                                                 std::unique_ptr<ImportLogger> logger = nullptr);

std::string difficultyToString(Difficulty difficulty);
std::optional<Difficulty> parseDifficulty(std::string text);
std::string questionTypeToString(QuestionType type);
std::optional<QuestionType> parseQuestionType(std::string text);

}  // namespace quizsphere
