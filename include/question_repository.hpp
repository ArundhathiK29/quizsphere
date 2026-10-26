#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "question_types.hpp"

namespace quizsphere {

// The ONLY storage boundary for the Question Bank (SAD 3.4). A SQLite
// implementation can be added later without touching the business code.
class QuestionRepository {
public:
    virtual ~QuestionRepository() = default;

    virtual Result loadAll(std::vector<Question>* questions) = 0;
    virtual Result saveAll(const std::vector<Question>& questions) = 0;
    virtual Result readTextFile(const std::string& path, std::string* content) const = 0;
    virtual Result writeTextFileAtomic(const std::string& path,
                                       const std::string& content,
                                       bool overwrite) const = 0;
    // Quizzes/attempts do not exist yet, so the file repository always answers false.
    // The Quiz Engine owner must implement the real check before attempts are stored.
    [[nodiscard]] virtual bool isReferenced(const std::string& question_id) const = 0;
};

std::unique_ptr<QuestionRepository> createFileQuestionRepository(std::string storage_path = "data/questions.tsv");

}  // namespace quizsphere
