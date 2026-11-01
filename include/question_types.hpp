#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common_types.hpp"

namespace quizsphere {

enum class Difficulty {
    Easy,
    Medium,
    Hard
};

enum class QuestionType {
    MCQ
};

struct Question {
    std::string question_id;
    std::string subject_tag;
    Difficulty difficulty{Difficulty::Easy};
    QuestionType question_type{QuestionType::MCQ};
    std::string question_text;
    std::vector<std::string> options;
    std::size_t correct_option{0};
    bool is_available{true};
};

// A question without its correct answer: what Quiz Engine / students may see.
struct PublicQuestion {
    std::string question_id;
    std::string subject_tag;
    Difficulty difficulty{Difficulty::Easy};
    QuestionType question_type{QuestionType::MCQ};
    std::string question_text;
    std::vector<std::string> options;
    bool is_available{true};
};

struct ImportIssue {
    // Physical line in the CSV file where the rejected record starts (header = line 1).
    std::size_t row_number{0};
    std::string reason;
};

struct ImportReport {
    Result result;
    std::size_t imported_count{0};
    std::size_t skipped_count{0};
    std::vector<ImportIssue> invalid_rows;
    // True if at least one rejected row could not be written to the import log.
    bool log_failed{false};
};

}  // namespace quizsphere
