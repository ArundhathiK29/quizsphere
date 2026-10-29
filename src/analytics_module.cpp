#include "analytics_module.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace quizsphere {
namespace {

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

        // BR-4: practice attempts are repeatable and never shown on the leaderboard.
        std::vector<Attempt> graded;
        std::copy_if(attempts.begin(), attempts.end(), std::back_inserter(graded),
                    [](const Attempt& attempt) { return attempt.mode == QuizMode::Graded; });

        std::sort(graded.begin(), graded.end(),
                 [](const Attempt& a, const Attempt& b) { return a.score > b.score; });

        entries->clear();
        entries->reserve(graded.size());
        int rank = 0;
        for (std::size_t i = 0; i < graded.size(); ++i) {
            // Standard competition ranking: ties share a rank, the next distinct score skips ahead
            // (1, 1, 3, 4, ...), matching LeaderboardEntry's "1 = best; ties share a rank" contract.
            if (i == 0 || graded[i].score != graded[i - 1].score) {
                rank = static_cast<int>(i) + 1;
            }
            entries->push_back(LeaderboardEntry{graded[i].student_id, graded[i].score, rank});
        }

        return {ErrorCode::OK, "Leaderboard computed."};
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
    QuestionBank& question_bank_;
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