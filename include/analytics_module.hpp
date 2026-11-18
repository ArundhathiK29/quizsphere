#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "data_access_layer.hpp"
#include "question_bank.hpp"
#include "quiz_types.hpp"

// Analytics Module (BL-24, BL-26, BL-41, BL-42; owner: see backlog).
namespace quizsphere {

// BL-41 / FR-020: one topic of a student's graded attempt on a quiz, next to the class average.
struct TopicAnalytics {
    std::string subject_tag;
    int student_correct{0};
    int student_total{0};
    double student_marks{0.0};
    double student_percent{0.0};        // 100 * student_correct / student_total
    double class_average_percent{0.0};  // mean of that percentage over every GRADED attempt on the quiz
    int class_attempts{0};              // how many graded attempts the class average is over
    bool weakest{false};                // true for the topic(s) with the lowest student_percent
};

class AnalyticsModule {
public:
    virtual ~AnalyticsModule() = default;

    // FR-017: ranked graded attempts of one quiz; practice attempts never appear (BR-4).
    virtual Result leaderboard(const std::string& quiz_id, std::vector<LeaderboardEntry>* entries) const = 0;

    // FR-017 (BL-24, extended scope): students ranked by their TOTAL marks on questions tagged
    // subject_tag, summed across every graded attempt they've made, across every quiz. Practice
    // attempts never count (BR-4); same tie-ranking as leaderboard() above.
    virtual Result topicLeaderboard(const std::string& subject_tag, std::vector<LeaderboardEntry>* entries) const = 0;

    // FR-017 (BL-24, extended scope): students ranked by their TOTAL score summed across every
    // graded attempt they've made, across every quiz. Practice attempts never count (BR-4); same
    // tie-ranking as leaderboard() above.
    virtual Result allTimeLeaderboard(std::vector<LeaderboardEntry>* entries) const = 0;

    // FR-019: percentage (0-100) of graded attempts that answered the question correctly.
    virtual Result difficultyIndex(const std::string& question_id, double* percent_correct) const = 0;

    // FR-020: per-topic breakdown of one student's graded attempt on a quiz (the TopicScore rows
    // the Grading Module stored in that attempt's GradeReport). ATTEMPT_NOT_FOUND if the student
    // has no graded result for the quiz. Practice attempts never have one (BR-4).
    virtual Result topicBreakdown(const std::string& quiz_id,
                                  const std::string& student_id,
                                  std::vector<TopicScore>* topics) const = 0;

    // BL-41 / FR-020: topicBreakdown() plus the class average per topic (over every graded
    // attempt on the quiz) with the weakest topic(s) flagged, sorted weakest first. A Student may
    // only request their own (NOT_AUTHORIZED otherwise); Instructor/Admin may request anyone's.
    virtual Result topicAnalytics(const Session& session,
                                  const std::string& quiz_id,
                                  const std::string& student_id,
                                  std::vector<TopicAnalytics>* topics) const = 0;

    // FR-021: per-student result transcript as CSV. Student may export their own, Instructor/Admin anyone's.
    virtual Result exportTranscriptCsv(const Session& session,
                                       const std::string& student_id,
                                       const std::string& csv_path,
                                       bool overwrite) const = 0;
};

// Stub for now (BL-01): every call returns NOT_IMPLEMENTED.
std::unique_ptr<AnalyticsModule> createAnalyticsModule(QuestionBank& question_bank,
                                                       QuizRepository& quizzes,
                                                       AttemptRepository& attempts);

}  // namespace quizsphere
