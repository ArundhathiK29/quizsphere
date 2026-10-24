#pragma once

#include "attempt.h"

#include <string>
#include <vector>

namespace QuizSphere {

// SAD §4.3 grading response. Breakdown item schemas require team agreement.
struct GradeResult {
    double score = 0.0;
    std::vector<std::string> itemisedBreakdown;
    std::vector<std::string> topicBreakdown;
};

enum class GradeError {
    None,
    AttemptAlreadySubmitted,
    QuizNotFound,
    TimeExpired,
    StorageError
};

struct GradeAttemptResult {
    bool success = false;
    GradeResult result{};
    GradeError error = GradeError::None;
    bool autoSubmitted = false;
};

// Interface declaration only. No grading logic is implemented here.
class GradingModule {
public:
    GradingModule() = default;
    virtual ~GradingModule() = default;

    virtual GradeAttemptResult GradeAttempt(
        const std::string& quizId,
        const std::string& studentId,
        const std::vector<Answer>& answers
    ) = 0;
};

} // namespace QuizSphere
