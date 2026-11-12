#pragma once

#include <string>

#include "question_bank.hpp"
#include "quiz_types.hpp"

// Itemised Result View (BL-23, owner: Chaturya; FR-017).
//
// "As a student, I want to see each question, my answer, the correct answer and marks so that
// I can learn from mistakes." This formats a GradeReport (already produced by the Grading Module,
// see grading_module.hpp) into plain text for the console.
namespace quizsphere {

// One line per question: its id, the student's selected option index (or "blank"), the correct
// option index, whether it was correct, and marks awarded - followed by a total score line.
// Index-only; prefer the QuestionBank overload below wherever readable option text is wanted. Kept
// for callers that only have a GradeReport in hand (and for the existing formatter tests).
std::string formatItemisedResult(const GradeReport& report);

// Same as above, but resolves each selected/correct option index to its actual option text via
// question_bank. GradeReport/ItemResult indexes are always in the question's ORIGINAL (unshuffled)
// option order - the same convention Grading itself scores against (see grading_module.cpp) - so
// this looks them up directly with no re-shuffling or re-ordering involved. Falls back to the
// index-only line for a question that can no longer be found (e.g. deleted since the attempt).
std::string formatItemisedResult(const GradeReport& report, QuestionBank& question_bank);

}  // namespace quizsphere