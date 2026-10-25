#include "analytics_module.hpp"

namespace quizsphere {
namespace {

// BL-01 stub. The Analytics owner replaces the bodies (BL-24, BL-26, BL-41, BL-42); keep the interface.
class StubAnalyticsModule final : public AnalyticsModule {
public:
    Result leaderboard(const std::string&, std::vector<LeaderboardEntry>*) const override {
        return notImplemented("AnalyticsModule::leaderboard");
    }
    Result difficultyIndex(const std::string&, double*) const override {
        return notImplemented("AnalyticsModule::difficultyIndex");
    }
    Result topicBreakdown(const std::string&, const std::string&, std::vector<TopicScore>*) const override {
        return notImplemented("AnalyticsModule::topicBreakdown");
    }
    Result exportTranscriptCsv(const Session&, const std::string&, const std::string&, bool) const override {
        return notImplemented("AnalyticsModule::exportTranscriptCsv");
    }
};

}  // namespace

std::unique_ptr<AnalyticsModule> createAnalyticsModule(QuestionBank&, QuizRepository&, AttemptRepository&) {
    return std::make_unique<StubAnalyticsModule>();
}

}  // namespace quizsphere
