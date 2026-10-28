#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "data_access_layer.hpp"
#include "question_bank.hpp"
#include "quiz_types.hpp"

// Grading Module (BL-10 / BL-25 / BL-43, owner: Chaturya with Ashwin; SAD 4.3).
namespace quizsphere {

class GradingModule {
public:
    virtual ~GradingModule() = default;

    // SAD 4.3 GradeAttempt(quiz_id, student_id, answers). Looks up the student's open attempt, applies the quiz's
    // ScoringScheme (marks per question, optional negative marking, blanks score 0), stores the graded attempt
    // atomically through the DAL, and fills `report`.
    // Errors: QUIZ_NOT_FOUND, ATTEMPT_ALREADY_SUBMITTED (FR-015),
    //         TIME_EXPIRED - the attempt is still graded and report->auto_submitted is set (FR-011).
    virtual Result gradeAttempt(const std::string& quiz_id,
                                const std::string& student_id,
                                const std::vector<Answer>& answers,
                                GradeReport* report) = 0;

    // The stored itemised result of a graded attempt (result screen, BL-23).
    virtual Result getReport(const std::string& quiz_id, const std::string& student_id, GradeReport* report) const = 0;
};

// Stub for now (BL-01): every call returns NOT_IMPLEMENTED.
std::unique_ptr<GradingModule> createGradingModule(QuestionBank& question_bank,
                                                   QuizRepository& quizzes,
                                                   AttemptRepository& attempts);

}  // namespace quizsphere
