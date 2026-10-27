#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "question_types.hpp"

namespace quizsphere {

class QuestionRepository {
public:
    virtual ~QuestionRepository() = default;

    virtual Result loadAll(std::vector<Question>* questions) = 0;
    virtual Result saveAll(const std::vector<Question>& questions) = 0;
    virtual Result readTextFile(const std::string& path, std::string* content) const = 0;
    virtual Result writeTextFileAtomic(const std::string& path,
                                       const std::string& content,
                                       bool overwrite) const = 0;
    [[nodiscard]] virtual bool isReferenced(const std::string& question_id) const = 0;
};

std::unique_ptr<QuestionRepository> createFileQuestionRepository(std::string storage_path = "data/questions.tsv");

}  // namespace quizsphere
