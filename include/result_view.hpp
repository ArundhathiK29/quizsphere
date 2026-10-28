#pragma once

#include <string>

#include "quiz_types.hpp"

// Itemised Result View (BL-23, owner: Chaturya; FR-017).
//
// "As a student, I want to see each question, my answer, the correct answer and marks so that
// I can learn from mistakes." This formats a GradeReport (already produced by the Grading Module,
// see grading_module.hpp) into plain text for the console.
//
// Deliberately decoupled from ConsoleMenu/the Student menu (BL-04) and the Quiz Engine (BL-07/08),
// neither of which exists yet: this takes only a GradeReport, so it is independently testable now
// and ready to be called the moment the student attempt flow is wired up.
namespace quizsphere {

// One line per question: its id, the student's selected option index (or "blank"), the correct
// option index, whether it was correct, and marks awarded - followed by a total score line.
// Option *text* isn't available here (GradeReport/ItemResult only carry indexes, see quiz_types.hpp);
// once the student menu exists and has a QuestionBank/Question in hand, it may prefer to resolve
// indexes to option text itself before or after calling this.
std::string formatItemisedResult(const GradeReport& report);

}  // namespace quizsphere