#include "grading_module.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <unordered_map>

namespace quizsphere {
namespace {

// Internal session used only to read full Question records (including the answer key) from the
// Question Bank, which requires an Instructor/Admin session (QuestionBank::findQuestionById).
// Grading is a trusted system module, not acting on behalf of any particular user, so it carries
// its own session rather than reusing question_bank.hpp's demo/CLI session.
const Session& systemSession() {
    static const Session session{"system:grading", Role::Admin, "1970-01-01T00:00:00Z"};
    return session;
}

std::string currentUtcTimestamp() {
    const std::time_t raw = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &raw);
#else
    gmtime_r(&raw, &parts);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts) == 0) {
        return "1970-01-01T00:00:00Z";
    }
    return buffer;
}

const Answer* findAnswer(const std::vector<Answer>& answers, const std::string& question_id) {
    const auto it = std::find_if(answers.begin(), answers.end(), [&question_id](const Answer& answer) {
        return answer.question_id == question_id;
    });
    return it == answers.end() ? nullptr : &(*it);
}

// Grading Module (BL-10, BL-25, BL-43; owner: Chaturya with Ashwin; SAD 4.3).
class GradingModuleImpl final : public GradingModule {
public:
    GradingModuleImpl(QuestionBank& question_bank, QuizRepository& quizzes, AttemptRepository& attempts)
        : question_bank_(question_bank), quizzes_(quizzes), attempts_(attempts) {}

    Result gradeAttempt(const std::string& quiz_id,
                        const std::string& student_id,
                        const std::vector<Answer>& answers,
                        GradeReport* report) override {
        if (report == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "report must not be null."};
        }

        Quiz quiz;
        const Result quiz_result = quizzes_.findQuiz(quiz_id, &quiz);
        if (!quiz_result.ok()) {
            return quiz_result;
        }

        // The open (in-progress) attempt carries the shuffled question_order the student saw and,
        // if the Quiz Engine already persisted a timeout, auto_submitted = true (FR-011).
        Attempt attempt;
        const Result attempt_result = attempts_.findOpenAttempt(quiz_id, student_id, &attempt);
        if (!attempt_result.ok()) {
            return attempt_result;
        }

        // Fetch every question in this attempt once; also used below to validate answers.
        std::unordered_map<std::string, Question> questions_by_id;
        for (const std::string& question_id : attempt.question_order) {
            Question question;
            const Result question_result = question_bank_.findQuestionById(systemSession(), question_id, &question);
            if (!question_result.ok()) {
                return question_result;
            }
            questions_by_id.emplace(question_id, question);
        }

        // Validate the submitted answers before grading anything (NFR-006: tampered/malformed
        // input is rejected by the module itself, not just hidden in a UI): no duplicate answers
        // for the same question, no answers for a question outside this attempt, no option index
        // that isn't kBlankAnswer or a valid index into that question's options.
        std::unordered_map<std::string, int> answer_counts;
        for (const Answer& answer : answers) {
            if (++answer_counts[answer.question_id] > 1) {
                return {ErrorCode::VALIDATION_FAILED, "Duplicate answer for question " + answer.question_id + "."};
            }
            const auto question_it = questions_by_id.find(answer.question_id);
            if (question_it == questions_by_id.end()) {
                return {ErrorCode::VALIDATION_FAILED,
                       "Answer for question " + answer.question_id + " is not part of this attempt."};
            }
            if (answer.selected_option != kBlankAnswer &&
                (answer.selected_option < 0 ||
                 static_cast<std::size_t>(answer.selected_option) >= question_it->second.options.size())) {
                return {ErrorCode::VALIDATION_FAILED, "Invalid option index for question " + answer.question_id + "."};
            }
        }

        GradeReport computed;
        computed.auto_submitted = attempt.auto_submitted;

        std::unordered_map<std::string, std::size_t> topic_index;  // subject_tag -> topic_breakdown slot

        for (const std::string& question_id : attempt.question_order) {
            const Question& question = questions_by_id.at(question_id);

            const Answer* answer = findAnswer(answers, question_id);
            const int selected_option = answer != nullptr ? answer->selected_option : kBlankAnswer;
            const bool blank = selected_option == kBlankAnswer;
            // Convention (MODULE_INTERFACES.md): Grading compares against Question::correct_option
            // directly, using the question's ORIGINAL option order - never the shuffled display order.
            const bool correct = !blank && selected_option >= 0 &&
                                 static_cast<std::size_t>(selected_option) == question.correct_option;

            double marks_awarded = 0.0;  // blank answers always score 0, even with negative marking
            if (correct) {
                marks_awarded = quiz.scoring.marks_per_question;
            } else if (!blank) {
                marks_awarded = -quiz.scoring.negative_mark;  // negative_mark is non-negative; subtract it
            }

            ItemResult item;
            item.question_id = question_id;
            item.selected_option = selected_option;
            item.correct_option = question.correct_option;
            item.correct = correct;
            item.blank = blank;
            item.marks_awarded = marks_awarded;
            computed.itemised_breakdown.push_back(item);

            computed.score += marks_awarded;
            computed.max_score += quiz.scoring.marks_per_question;

            auto index_it = topic_index.find(question.subject_tag);
            if (index_it == topic_index.end()) {
                index_it = topic_index.emplace(question.subject_tag, computed.topic_breakdown.size()).first;
                computed.topic_breakdown.push_back(TopicScore{question.subject_tag, 0, 0, 0.0});
            }
            TopicScore& topic = computed.topic_breakdown[index_it->second];
            topic.total += 1;
            topic.marks += marks_awarded;
            if (correct) {
                topic.correct += 1;
            }
        }

        // BR-5 (BL-25, UT-GRADE-02): "a negative marking scheme cannot bring a single question's
        // score below zero." Per-item marks_awarded above keeps the raw (possibly negative) deduction
        // for the itemised breakdown (FR-017 transparency), but the attempt's own total never goes
        // negative overall.
        if (computed.score < 0.0) {
            computed.score = 0.0;
        }

        attempt.answers = answers;
        attempt.submitted = true;
        attempt.score = computed.score;  // quiz_types.hpp: "filled in by the Grading Module"
        if (attempt.submitted_at.empty()) {
            attempt.submitted_at = currentUtcTimestamp();
        }

        // Atomic, all-or-nothing persistence; the DAL itself reports ATTEMPT_ALREADY_SUBMITTED
        // (FR-015, NFR-006) if a graded attempt already exists.
        const Result save_result = attempts_.saveGradedAttempt(attempt, computed);
        if (!save_result.ok()) {
            return save_result;
        }

        *report = computed;

        if (attempt.auto_submitted) {
            return {ErrorCode::TIME_EXPIRED, "The attempt was auto-submitted when the timer ended."};
        }
        return {ErrorCode::OK, "Attempt graded."};
    }

    Result getReport(const std::string& quiz_id, const std::string& student_id, GradeReport* report) const override {
        if (report == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "report must not be null."};
        }
        return attempts_.findReport(quiz_id, student_id, report);
    }

private:
    QuestionBank& question_bank_;
    QuizRepository& quizzes_;
    AttemptRepository& attempts_;
};

}  // namespace

std::unique_ptr<GradingModule> createGradingModule(QuestionBank& question_bank,
                                                   QuizRepository& quizzes,
                                                   AttemptRepository& attempts) {
    return std::make_unique<GradingModuleImpl>(question_bank, quizzes, attempts);
}

}  // namespace quizsphere
