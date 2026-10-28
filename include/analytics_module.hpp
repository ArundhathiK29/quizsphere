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

class AnalyticsModule {
public:
    virtual ~AnalyticsModule() = default;

    // FR-017: ranked graded attempts of one quiz; practice attempts never appear (BR-4).
    virtual Result leaderboard(const std::string& quiz_id, std::vector<LeaderboardEntry>* entries) const = 0;

    // FR-019: percentage (0-100) of graded attempts that answered the question correctly.
    virtual Result difficultyIndex(const std::string& question_id, double* percent_correct) const = 0;

    // FR-020: per-topic breakdown and weak topics for one student's graded attempt.
    virtual Result topicBreakdown(const std::string& quiz_id,
                                  const std::string& student_id,
                                  std::vector<TopicScore>* topics) const = 0;

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
