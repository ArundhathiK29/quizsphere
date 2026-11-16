#pragma once

#include <istream>
#include <ostream>
#include <functional>

#include "analytics_module.hpp"
#include "quiz_attempt.hpp"
#include "timed_console_input.hpp"

namespace quizsphere {

// Narrow entry points used by the existing role menus; question-bank menus remain independent.
void runQuizCreation(const Session& session, QuizEngine& engine, std::istream& input, std::ostream& output);
using TimedLineReader = std::function<TimedRead(ConsoleDeadline, std::string*)>;
// `timed_read` (BL-38) reads input against the attempt deadline; `analytics` (optional, BL-41)
// adds the per-topic summary under the itemised result.
void runQuizAttempts(const Session& session, QuizEngine& engine, QuestionBank& bank,
                     std::istream& input, std::ostream& output, TimedLineReader timed_read = {},
                     const AnalyticsModule* analytics = nullptr);
void runQuizResult(const Session& session, const QuizEngine& engine, const GradingModule& grading,
                   const QuestionBank& bank, std::istream& input, std::ostream& output,
                   const AnalyticsModule* analytics = nullptr);

}  // namespace quizsphere
