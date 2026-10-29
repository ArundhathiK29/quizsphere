#include "analytics_module.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace quizsphere {
namespace {

// Same rationale as grading_module.cpp: a trusted system session to read the answer key
// (QuestionBank::findQuestionById requires Instructor/Admin), not acting on behalf of any user.
const Session& systemSession() {
    static const Session session{"system:analytics", Role::Admin, "1970-01-01T00:00:00Z"};
    return session;
}

bool answeredCorrectly(const Attempt& attempt, const std::string& question_id, const Question& question) {
    const auto it = std::find_if(attempt.answers.begin(), attempt.answers.end(),
                                 [&question_id](const Answer& answer) { return answer.question_id == question_id; });
    if (it == attempt.answers.end()) {
        return false;  // blank, or not actually part of this attempt
    }
    return it->selected_option >= 0 && static_cast<std::size_t>(it->selected_option) == question.correct_option;
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
        // BL-26 / FR-019: percentage (0-100) of graded attempts that answered the question correctly.
    // Counted across every quiz the question appears in, both practice and graded attempts (unlike
    // the leaderboard, item-difficulty analysis isn't restricted by BR-4), and over every attempt
    // that saw the question at all - a skipped (blank) answer counts as not-correct.
    Result difficultyIndex(const std::string& question_id, double* percent_correct) const override {
        if (percent_correct == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "percent_correct must not be null."};
        }

        Question question;
        const Result question_result = question_bank_.findQuestionById(systemSession(), question_id, &question);
        if (!question_result.ok()) {
            return question_result;
        }

        std::vector<Quiz> quizzes;
        const Result list_quizzes_result = quizzes_.listQuizzes(&quizzes);
        if (!list_quizzes_result.ok()) {
            return list_quizzes_result;
        }

        int seen = 0;
        int correct = 0;
        for (const Quiz& quiz : quizzes) {
            std::vector<Attempt> attempts;
            const Result list_result = attempts_.listGradedAttempts(quiz.quiz_id, &attempts);
            if (!list_result.ok()) {
                return list_result;
            }
            for (const Attempt& attempt : attempts) {
                const bool saw_question =
                    std::find(attempt.question_order.begin(), attempt.question_order.end(), question_id) !=
                    attempt.question_order.end();
                if (!saw_question) {
                    continue;
                }
                ++seen;
                if (answeredCorrectly(attempt, question_id, question)) {
                    ++correct;
                }
            }
        }

        // No attempts have seen this question yet: a defined, neutral 0% rather than an error -
        // the question exists (checked above), it's just unattempted so far.
        *percent_correct = seen == 0 ? 0.0 : (100.0 * static_cast<double>(correct) / static_cast<double>(seen));
        return {ErrorCode::OK, "Difficulty index computed."};
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