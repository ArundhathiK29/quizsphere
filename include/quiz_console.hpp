#pragma once

#include <istream>
#include <ostream>
#include <functional>

#include "quiz_attempt.hpp"
#include "timed_console_input.hpp"

namespace quizsphere {

// Narrow entry points used by the existing role menus; question-bank menus remain independent.
void runQuizCreation(const Session& session, QuizEngine& engine, std::istream& input, std::ostream& output);
using TimedLineReader = std::function<TimedRead(ConsoleDeadline, std::string*)>;
void runQuizAttempts(const Session& session, QuizEngine& engine, QuestionBank& bank,
                     std::istream& input, std::ostream& output, TimedLineReader timed_read = {});
void runQuizResult(const Session& session, const QuizEngine& engine, const GradingModule& grading,
                   const QuestionBank& bank, std::istream& input, std::ostream& output);

}  // namespace quizsphere
