#include "result_view.hpp"

#include <string>

#include <gtest/gtest.h>

using namespace quizsphere;

namespace {

GradeReport sampleReport() {
    GradeReport report;
    report.score = 0.75;
    report.max_score = 3.0;
    report.auto_submitted = false;

    ItemResult correct;
    correct.question_id = "Q1";
    correct.selected_option = 1;
    correct.correct_option = 1;
    correct.correct = true;
    correct.blank = false;
    correct.marks_awarded = 1.0;
    report.itemised_breakdown.push_back(correct);

    ItemResult wrong;
    wrong.question_id = "Q2";
    wrong.selected_option = 2;
    wrong.correct_option = 0;
    wrong.correct = false;
    wrong.blank = false;
    wrong.marks_awarded = -0.25;
    report.itemised_breakdown.push_back(wrong);

    ItemResult blank;
    blank.question_id = "Q3";
    blank.selected_option = kBlankAnswer;
    blank.correct_option = 2;
    blank.correct = false;
    blank.blank = true;
    blank.marks_awarded = 0.0;
    report.itemised_breakdown.push_back(blank);

    return report;
}

// BL-23 / FR-017: "Result lists every question with answer, correct answer and marks awarded."
TEST(ResultView, ListsEveryQuestionWithAnswerCorrectAnswerAndMarks) {
    const std::string text = formatItemisedResult(sampleReport());

    EXPECT_NE(text.find("Q1: your answer = 1, correct answer = 1 -> CORRECT (+1.00)"), std::string::npos);
    EXPECT_NE(text.find("Q2: your answer = 2, correct answer = 0 -> WRONG (-0.25)"), std::string::npos);
    EXPECT_NE(text.find("Q3: your answer = blank, correct answer = 2 -> BLANK (+0.00)"), std::string::npos);
}

TEST(ResultView, ShowsTheTotalScoreAgainstMaxScore) {
    const std::string text = formatItemisedResult(sampleReport());
    EXPECT_NE(text.find("Score: 0.75 / 3.00"), std::string::npos);
}

TEST(ResultView, FlagsAutoSubmittedAttempts) {
    GradeReport report = sampleReport();
    report.auto_submitted = true;

    const std::string text = formatItemisedResult(report);
    EXPECT_NE(text.find("auto-submitted"), std::string::npos);
}

TEST(ResultView, HandlesAnEmptyBreakdownWithoutCrashing) {
    GradeReport report;
    report.score = 0.0;
    report.max_score = 0.0;

    const std::string text = formatItemisedResult(report);
    EXPECT_NE(text.find("Score: 0.00 / 0.00"), std::string::npos);
}

}  // namespace