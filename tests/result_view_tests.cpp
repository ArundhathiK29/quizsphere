#include "result_view.hpp"

#include <string>

#include <gtest/gtest.h>

using namespace quizsphere;

namespace {

GradeReport sampleReport() {
    GradeReport report;
    report.score = 1.0;  // BR-5: the wrong item is floored at 0, so 1 + 0 + 0
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
    wrong.marks_awarded = 0.0;  // BR-5 per-question floor (BL-25): never below zero
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
    EXPECT_NE(text.find("Q2: your answer = 2, correct answer = 0 -> WRONG (+0.00)"), std::string::npos);
    EXPECT_NE(text.find("Q3: your answer = blank, correct answer = 2 -> BLANK (+0.00)"), std::string::npos);
}

TEST(ResultView, ShowsTheTotalScoreAgainstMaxScore) {
    const std::string text = formatItemisedResult(sampleReport());
    EXPECT_NE(text.find("Score: 1.00 / 3.00"), std::string::npos);
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

// BL-41 / FR-020: the per-topic section under the itemised result.
TEST(ResultView, TopicSummaryListsEachTopicVsClassAverageAndNamesTheWeakest) {
    std::vector<TopicAnalytics> topics;
    TopicAnalytics science;
    science.subject_tag = "Science";
    science.student_correct = 0;
    science.student_total = 1;
    science.student_percent = 0.0;
    science.class_average_percent = 50.0;
    science.weakest = true;
    TopicAnalytics math;
    math.subject_tag = "Math";
    math.student_correct = 2;
    math.student_total = 2;
    math.student_percent = 100.0;
    math.class_average_percent = 75.0;
    topics = {science, math};

    const std::string text = formatTopicSummary(topics);
    EXPECT_NE(text.find("Topics (you vs class average):"), std::string::npos);
    EXPECT_NE(text.find("  Science: 0/1 correct (0.0%), class average 50.0%  <- weakest"), std::string::npos);
    EXPECT_NE(text.find("  Math: 2/2 correct (100.0%), class average 75.0%\n"), std::string::npos);
    EXPECT_NE(text.find("Weakest topic: Science\n"), std::string::npos);
}

TEST(ResultView, TopicSummaryPluralisesTiedWeakestTopicsAndIsEmptyForNoTopics) {
    TopicAnalytics a;
    a.subject_tag = "A";
    a.student_total = 1;
    a.weakest = true;
    TopicAnalytics b = a;
    b.subject_tag = "B";
    const std::string text = formatTopicSummary({a, b});
    EXPECT_NE(text.find("Weakest topics: A, B"), std::string::npos);
    EXPECT_EQ(formatTopicSummary({}), "");
}

}  // namespace
