#pragma once

#include <optional>
#include <string>
#include <vector>

#include "question_types.hpp"

// Shared data structures for the Quiz Engine, Grading, Analytics and Data Access modules (BL-01).
// Agreed conventions - change them only through a PR the whole team reviews:
//   * Option indexes are ALWAYS the question's ORIGINAL index (Question::options). The Quiz Engine
//     shuffles for display and maps the student's choice back before storing an Answer.
//   * A blank (skipped) answer is kBlankAnswer.
//   * Timestamps are UTC ISO-8601 strings, e.g. "2026-10-25T10:30:00Z" (same as Session::logged_in_at).
//   * negative_mark is a non-negative number SUBTRACTED per wrong answer; blank answers score 0.
namespace quizsphere {

constexpr int kBlankAnswer = -1;

enum class QuizMode {
    Practice,   // not stored in the leaderboard (BR-4), may be repeated
    Graded      // one attempt per student per quiz (FR-015)
};

struct ScoringScheme {
    double marks_per_question{1.0};
    double negative_mark{0.0};
};

// What an instructor supplies when creating a quiz (FR-008, FR-009).
struct QuizConfig {
    std::string title;
    int time_limit_minutes{0};    // must be > 0 (BR-2)
    int question_count{0};        // must be > 0 (BR-1)
    std::optional<std::string> subject_filter;
    std::optional<Difficulty> difficulty_filter;
    ScoringScheme scoring;
};

struct Quiz {
    std::string quiz_id;
    std::string title;
    std::string created_by;                   // Session::user_id of the instructor
    std::string created_at;
    int time_limit_minutes{0};
    ScoringScheme scoring;
    std::vector<std::string> question_ids;    // the pooled questions (only available ones)
};

struct Answer {
    std::string question_id;
    int selected_option{kBlankAnswer};        // original option index, or kBlankAnswer
    bool flagged_for_review{false};           // FR-012
};

struct Attempt {
    std::string attempt_id;
    std::string quiz_id;
    std::string student_id;
    QuizMode mode{QuizMode::Graded};
    std::string started_at;
    std::string submitted_at;                 // empty until submitted
    std::vector<std::string> question_order;  // shuffled order shown to this student (FR-010)
    std::vector<Answer> answers;
    bool submitted{false};
    bool auto_submitted{false};               // true when the timer ended the attempt (FR-011)
    double score{0.0};                        // filled in by the Grading Module
};

struct ItemResult {
    std::string question_id;
    int selected_option{kBlankAnswer};
    std::size_t correct_option{0};
    bool correct{false};
    bool blank{true};
    double marks_awarded{0.0};                // negative for a wrong answer with negative marking
};

struct TopicScore {
    std::string subject_tag;
    int correct{0};
    int total{0};
    double marks{0.0};
};

// SAD 4.3: Result{score, itemised_breakdown, topic_breakdown}. Named GradeReport to avoid
// clashing with quizsphere::Result (the error-or-ok return type).
struct GradeReport {
    double score{0.0};
    double max_score{0.0};
    std::vector<ItemResult> itemised_breakdown;
    std::vector<TopicScore> topic_breakdown;
    bool auto_submitted{false};
};

struct LeaderboardEntry {
    std::string student_id;
    double score{0.0};
    int rank{0};                              // 1 = best; ties share a rank
};

}  // namespace quizsphere
