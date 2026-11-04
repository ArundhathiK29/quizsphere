#pragma once

#include <istream>
#include <ostream>

#include "quiz_attempt.hpp"

namespace quizsphere {

// Narrow entry points used by the existing role menus; question-bank menus remain independent.
void runQuizCreation(const Session& session, QuizEngine& engine, std::istream& input, std::ostream& output);
void runQuizAttempts(const Session& session, QuizEngine& engine, QuestionBank& bank,
                     std::istream& input, std::ostream& output);
void runQuizResult(const Session& session, const QuizEngine& engine, const GradingModule& grading,
                   std::istream& input, std::ostream& output);

}  // namespace quizsphere
