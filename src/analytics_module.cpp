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

    // BL-26 / FR-019: percentage (0-100) of GRADED attempts that answered the question correctly,
    // counted across every quiz the question appears in and over every graded attempt that saw the
    // question at all - a skipped (blank) answer counts as incorrect. Practice attempts are
    // repeatable and never persisted as results, so they must not move the index (BR-4).
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
                // BR-4 defense-in-depth, same as the leaderboards: graded attempts only.
                if (attempt.mode != QuizMode::Graded) {
                    continue;
                }
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

        // No graded attempts have seen this question yet: a defined, neutral 0% rather than an
        // error - the question exists (checked above), it's just unattempted so far.
        *percent_correct = seen == 0 ? 0.0 : (100.0 * static_cast<double>(correct) / static_cast<double>(seen));
        return {ErrorCode::OK, "Difficulty index computed."};
    }

    // BL-41 / FR-020: the student's own graded topic rows, exactly as grading stored them.
    Result topicBreakdown(const std::string& quiz_id, const std::string& student_id,
                          std::vector<TopicScore>* topics) const override {
        if (topics == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "topics must not be null."};
        }
        GradeReport report;
        const Result found = findGradedReport(quiz_id, student_id, &report);
        if (!found.ok()) {
            return found;
        }
        *topics = report.topic_breakdown;
        return {ErrorCode::OK, "Topic breakdown retrieved."};
    }

    // BL-41 / FR-020: student vs class per topic, weakest first.
    Result topicAnalytics(const Session& session, const std::string& quiz_id, const std::string& student_id,
                          std::vector<TopicAnalytics>* topics) const override {
        if (topics == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "topics must not be null."};
        }
        if (session.role == Role::Student) {
            if (session.user_id != student_id) {
                return {ErrorCode::NOT_AUTHORIZED, "Students may only view their own topic analytics."};
            }
        } else if (session.role != Role::Instructor && session.role != Role::Admin) {
            return {ErrorCode::NOT_AUTHORIZED, "Not authorized."};
        }

        std::vector<TopicScore> own;
        const Result own_result = topicBreakdown(quiz_id, student_id, &own);
        if (!own_result.ok()) {
            return own_result;
        }

        // Class average: mean percent-correct per topic over every GRADED attempt on this quiz
        // (the student's own attempt included). Practice attempts never count (BR-4).
        std::vector<Attempt> attempts;
        const Result list_result = attempts_.listGradedAttempts(quiz_id, &attempts);
        if (!list_result.ok()) {
            return list_result;
        }
        std::unordered_map<std::string, std::pair<double, int>> class_sum;  // tag -> (sum of %, count)
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
                if (topic.total <= 0) {
                    continue;
                }
                auto& slot = class_sum[topic.subject_tag];
                slot.first += percent(topic.correct, topic.total);
                slot.second += 1;
            }
        }

        topics->clear();
        topics->reserve(own.size());
        for (const TopicScore& topic : own) {
            TopicAnalytics row;
            row.subject_tag = topic.subject_tag;
            row.student_correct = topic.correct;
            row.student_total = topic.total;
            row.student_marks = topic.marks;
            row.student_percent = percent(topic.correct, topic.total);
            const auto it = class_sum.find(topic.subject_tag);
            if (it != class_sum.end() && it->second.second > 0) {
                row.class_average_percent = it->second.first / it->second.second;
                row.class_attempts = it->second.second;
            }
            topics->push_back(row);
        }
        std::stable_sort(topics->begin(), topics->end(), [](const TopicAnalytics& a, const TopicAnalytics& b) {
            return a.student_percent < b.student_percent;
        });
        if (!topics->empty()) {
            const double lowest = topics->front().student_percent;
            for (TopicAnalytics& row : *topics) {
                row.weakest = row.student_percent == lowest;
            }
        }
        return {ErrorCode::OK, "Topic analytics computed."};
    }
    Result exportTranscriptCsv(const Session&, const std::string&, const std::string&, bool) const override {
        return notImplemented("AnalyticsModule::exportTranscriptCsv");
    }

private:
    static double percent(int correct, int total) {
        return total <= 0 ? 0.0 : 100.0 * static_cast<double>(correct) / static_cast<double>(total);
    }

    // The GradeReport of the student's GRADED attempt on quiz_id, looked up by exact attempt id
    // (findReport(quiz, student) returns the latest report, which could be a retained practice one).
    Result findGradedReport(const std::string& quiz_id, const std::string& student_id, GradeReport* report) const {
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
        const auto graded = std::find_if(attempts.begin(), attempts.end(), [&student_id](const Attempt& attempt) {
            return attempt.mode == QuizMode::Graded && attempt.student_id == student_id;
        });
        if (graded == attempts.end()) {
            return {ErrorCode::ATTEMPT_NOT_FOUND, "No graded result for this student on this quiz."};
        }
        return attempts_.findReportForAttempt(graded->attempt_id, report);
    }

    QuestionBank& question_bank_;  // answer key for difficultyIndex()
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
