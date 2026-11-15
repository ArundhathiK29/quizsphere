#pragma once

#include <istream>
#include <ostream>

#include "analytics_module.hpp"

namespace quizsphere {

// BL-24: Instructor/Admin console entry point for the rankings (FR-017). Students are refused.
// Offers the per-quiz, per-topic and all-time leaderboards; each one is read-only.
void runAnalyticsMenu(const Session& session, const AnalyticsModule& analytics, const QuizRepository& quizzes,
                      std::istream& input, std::ostream& output);

}  // namespace quizsphere
