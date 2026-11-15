#include "analytics_module.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace quizsphere {
namespace {

// Shared by leaderboard(), topicLeaderboard() and allTimeLeaderboard(): standard competition
// ranking (ties share a rank, the next distinct score skips ahead - 1, 1, 3, 4, ...), matching
// LeaderboardEntry's "1 = best; ties share a rank" contract.
void rankByScore(const std::unordered_map<std::string, double>& totals, std::vector<LeaderboardEntry>* entries) {
    entries->clear();
    entries->reserve(totals.size());
    std::transform(totals.begin(), totals.end(), std::back_inserter(*entries),
                   [](const auto& entry) { return LeaderboardEntry{entry.first, entry.second, 0}; });
    std::sort(entries->begin(), entries->end(),
             [](const LeaderboardEntry& a, const LeaderboardEntry& b) { return a.score > b.score; });

    int rank = 0;
    for (std::size_t i = 0; i < entries->size(); ++i) {
        if (i == 0 || (*entries)[i].score != (*entries)[i - 1].score) {
            rank = static_cast<int>(i) + 1;
        }
        (*entries)[i].rank = rank;
    }
}

class AnalyticsModuleImpl final : public AnalyticsModule {
public:
    AnalyticsModuleImpl(QuestionBank& question_bank, QuizRepository& quizzes, AttemptRepository& attempts)
        : question_bank_(question_bank), quizzes_(quizzes), attempts_(attempts) {}

    // BL-24 / FR-017: ranked graded attempts of one quiz; practice attempts never appear (BR-4).
    Result leaderboard(const std::string& quiz_id, std::vector<LeaderboardEntry>* entries) const override {
        if (entries == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "entries must not be null."};
        }

        Quiz quiz;
        const Result quiz_result = quizzes_.findQuiz(quiz_id, &quiz);
        if (!quiz_result.ok()) {
            return quiz_result;
        }

        std::vector<Attempt> attempts;
        const Result list_result = attempts_.listGradedAttempts(quiz_id, &attempts);
        if (!list_result.ok()) {
            return list_result;
        }

        std::unordered_map<std::string, double> totals;
        for (const Attempt& attempt : attempts) {
            // BR-4 defense-in-depth: practice attempts are repeatable and never shown on the
            // leaderboard, even though listGradedAttempts should already only return Graded ones.
            if (attempt.mode != QuizMode::Graded) {
                continue;
            }
            totals[attempt.student_id] = attempt.score;
        }

        rankByScore(totals, entries);
        return {ErrorCode::OK, "Leaderboard computed."};
    }

    // BL-24 (extended scope, FR-017): students ranked by their total marks on questions tagged
    // subject_tag, summed across every graded attempt they've made, across every quiz.
    Result topicLeaderboard(const std::string& subject_tag, std::vector<LeaderboardEntry>* entries) const override {
        if (entries == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "entries must not be null."};
        }

        std::vector<Quiz> quizzes;
        const Result list_quizzes_result = quizzes_.listQuizzes(&quizzes);
        if (!list_quizzes_result.ok()) {
            return list_quizzes_result;
        }

        std::unordered_map<std::string, double> totals;
        for (const Quiz& quiz : quizzes) {
            std::vector<Attempt> attempts;
            const Result list_result = attempts_.listGradedAttempts(quiz.quiz_id, &attempts);
            if (!list_result.ok()) {
                return list_result;
            }
            for (const Attempt& attempt : attempts) {
                if (attempt.mode != QuizMode::Graded) {
                    continue;
                }
                GradeReport report;
                const Result report_result = attempts_.findReportForAttempt(attempt.attempt_id, &report);
                if (!report_result.ok()) {
                    return report_result;
                }
                for (const TopicScore& topic : report.topic_breakdown) {
                    if (topic.subject_tag == subject_tag) {
                        totals[attempt.student_id] += topic.marks;
                    }
                }
            }
        }

        rankByScore(totals, entries);
        return {ErrorCode::OK, "Topic leaderboard computed."};
    }

    // BL-24 (extended scope, FR-017): students ranked by their total score summed across every
    // graded attempt they've made, across every quiz.
    Result allTimeLeaderboard(std::vector<LeaderboardEntry>* entries) const override {
        if (entries == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "entries must not be null."};
        }

        std::vector<Quiz> quizzes;
        const Result list_quizzes_result = quizzes_.listQuizzes(&quizzes);
        if (!list_quizzes_result.ok()) {
            return list_quizzes_result;
        }

        std::unordered_map<std::string, double> totals;
        for (const Quiz& quiz : quizzes) {
            std::vector<Attempt> attempts;
            const Result list_result = attempts_.listGradedAttempts(quiz.quiz_id, &attempts);
            if (!list_result.ok()) {
                return list_result;
            }
            for (const Attempt& attempt : attempts) {
                if (attempt.mode != QuizMode::Graded) {
                    continue;
                }
                totals[attempt.student_id] += attempt.score;
            }
        }

        rankByScore(totals, entries);
        return {ErrorCode::OK, "All-time leaderboard computed."};
    }

    // BL-26 stub for now; implemented in a separate PR/issue.
    Result difficultyIndex(const std::string&, double*) const override {
        return notImplemented("AnalyticsModule::difficultyIndex");
    }

    Result topicBreakdown(const std::string&, const std::string&, std::vector<TopicScore>*) const override {
        return notImplemented("AnalyticsModule::topicBreakdown");
    }
    Result exportTranscriptCsv(const Session&, const std::string&, const std::string&, bool) const override {
        return notImplemented("AnalyticsModule::exportTranscriptCsv");
    }

private:
    // Stored for BL-26's difficultyIndex(), which stacks on top of this branch and is the only
    // caller that needs it; leaderboard() above doesn't touch it.
    [[maybe_unused]] QuestionBank& question_bank_;
    QuizRepository& quizzes_;
    AttemptRepository& attempts_;
};

}  // namespace

std::unique_ptr<AnalyticsModule> createAnalyticsModule(QuestionBank& question_bank,
                                                       QuizRepository& quizzes,
                                                       AttemptRepository& attempts) {
    return std::make_unique<AnalyticsModuleImpl>(question_bank, quizzes, attempts);
}

}  // namespace quizsphere
