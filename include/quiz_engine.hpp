#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "question_bank.hpp"
#include "quiz_types.hpp"

// Quiz Engine Module (BL-07, BL-08, BL-19..21, BL-38, BL-39; owner: Ashwin).
namespace quizsphere {

class QuizEngine {
public:
    virtual ~QuizEngine() = default;

    // FR-008 / FR-009. Instructor or Admin only (NOT_AUTHORIZED otherwise). Pools only AVAILABLE questions
    // matching the filters; rejects question_count <= 0 or time_limit_minutes <= 0 (BR-1, BR-2) with VALIDATION_FAILED.
    virtual Result createQuiz(const Session& session, const QuizConfig& config, Quiz* quiz) = 0;
    virtual Result getQuiz(const std::string& quiz_id, Quiz* quiz) const = 0;       // QUIZ_NOT_FOUND
    virtual Result listQuizzes(std::vector<Quiz>* quizzes) const = 0;

    // FR-010. Student session; shuffles question order and options for this attempt.
    // Graded mode refuses a second attempt with ATTEMPT_ALREADY_SUBMITTED (FR-015).
    virtual Result startAttempt(const Session& session, const std::string& quiz_id, QuizMode mode, Attempt* attempt) = 0;

    // FR-012 / FR-013: record, change or flag an answer while the attempt is open.
    virtual Result saveAnswer(Attempt* attempt, const Answer& answer) = 0;

    // Submit and grade through the Grading Module. `timed_out` = the timer ended it (FR-011).
    virtual Result submitAttempt(const Session& session, Attempt* attempt, bool timed_out, GradeReport* report) = 0;
};

// Stub for now (BL-01): every call returns NOT_IMPLEMENTED.
std::unique_ptr<QuizEngine> createQuizEngine(QuestionBank& question_bank,
                                             QuizRepository& quizzes,
                                             AttemptRepository& attempts,
                                             GradingModule& grading);

}  // namespace quizsphere
