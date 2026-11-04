
#include "quiz_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace quizsphere {
namespace {

Result validationError(const std::string& message) {
    return {ErrorCode::VALIDATION_FAILED, message};
}

bool isValidSession(const Session& session) {
    return !session.user_id.empty() && !session.logged_in_at.empty();
}

bool canCreateQuiz(const Session& session) {
    return isValidSession(session) &&
           (session.role == Role::Instructor || session.role == Role::Admin);
}

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });

    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });

    return std::string(first, last.base());
}

std::string currentUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm utc_time{};

#ifdef _WIN32
    gmtime_s(&utc_time, &time);
#else
    gmtime_r(&time, &utc_time);
#endif

    std::ostringstream timestamp;
    timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return timestamp.str();
}

std::string generateQuizId() {
    static std::random_device random_device;
    static std::mt19937_64 generator(random_device());

    std::ostringstream id;
    id << "quiz-" << std::hex << generator();
    return id.str();
}

class QuizEngineImpl final : public QuizEngine {
public:
    QuizEngineImpl(
        QuestionBank& question_bank,
        QuizRepository& quizzes,
        AttemptRepository& attempts,
        GradingModule& grading,
        std::uint64_t seed)
        : question_bank_(question_bank), quizzes_(quizzes), attempts_(attempts),
          grading_(grading), random_(seed) {}

    Result createQuiz(
        const Session& session,
        const QuizConfig& config,
        Quiz* quiz) override {

        if (!canCreateQuiz(session)) {
            return {
                ErrorCode::NOT_AUTHORIZED,
                "Only authenticated instructors and admins can create quizzes."
            };
        }

        if (quiz == nullptr) {
            return validationError("Quiz output pointer must not be null.");
        }

        const std::string title = trim(config.title);

        if (title.empty()) {
            return validationError("Quiz title must not be empty.");
        }

        if (config.question_count <= 0) {
            return validationError(
                "Question count must be greater than zero.");
        }

        if (config.time_limit_minutes <= 0) {
            return validationError(
                "Time limit must be greater than zero.");
        }

        // BL-08: validate the scoring scheme before saving the quiz.
        if (!std::isfinite(config.scoring.marks_per_question) ||
            config.scoring.marks_per_question <= 0.0) {
            return validationError(
                "Marks per question must be finite and greater than zero.");
        }

        if (!std::isfinite(config.scoring.negative_mark) ||
            config.scoring.negative_mark < 0.0) {
            return validationError(
                "Negative marking must be finite and non-negative.");
        }

        if (config.difficulty_filter && (*config.difficulty_filter != Difficulty::Easy &&
            *config.difficulty_filter != Difficulty::Medium && *config.difficulty_filter != Difficulty::Hard)) {
            return validationError("Difficulty must be Easy, Medium, or Hard.");
        }
        auto subject_filter = config.subject_filter;
        if (subject_filter) {
            *subject_filter = trim(*subject_filter);
            if (subject_filter->empty()) return validationError("Subject filter must not be blank.");
        }
        std::vector<PublicQuestion> available_questions;

        const Result question_result = question_bank_.listAvailable(
            &available_questions,
            subject_filter,
            config.difficulty_filter);

        if (!question_result.ok()) {
            return question_result;
        }

        std::unordered_set<std::string> selected_ids;
        available_questions.erase(std::remove_if(available_questions.begin(), available_questions.end(),
            [&](const PublicQuestion& question) {
                return !question.is_available || question.question_id.empty() ||
                       !selected_ids.insert(question.question_id).second;
            }), available_questions.end());

        if (available_questions.size() <
            static_cast<std::size_t>(config.question_count)) {
            return validationError(
                "Not enough available questions match the selected filters.");
        }

        // Randomly select the requested number of eligible questions.
        std::random_device random_device;
        std::mt19937 generator(random_device());

        std::shuffle(
            available_questions.begin(),
            available_questions.end(),
            generator);

        Quiz new_quiz;
        new_quiz.quiz_id = generateQuizId();
        new_quiz.title = title;
        new_quiz.created_by = session.user_id;
        new_quiz.created_at = currentUtcTimestamp();
        new_quiz.time_limit_minutes = config.time_limit_minutes;

        // BL-08: retain both scoring values on the quiz.
        new_quiz.scoring = config.scoring;

        new_quiz.question_ids.reserve(
            static_cast<std::size_t>(config.question_count));

        for (int i = 0; i < config.question_count; ++i) {
            new_quiz.question_ids.push_back(
                available_questions[static_cast<std::size_t>(i)].question_id);
        }

        // The repository persists the quiz, including its scoring scheme.
        const Result save_result = quizzes_.saveQuiz(new_quiz);

        if (!save_result.ok()) {
            return save_result;
        }

        *quiz = new_quiz;

        return {
            ErrorCode::OK,
            "Quiz created successfully."
        };
    }

    Result getQuiz(
        const std::string& quiz_id,
        Quiz* quiz) const override {

        if (quiz == nullptr || trim(quiz_id).empty()) {
            return validationError(
                "Quiz ID and quiz output pointer must be valid.");
        }

        return quizzes_.findQuiz(trim(quiz_id), quiz);
    }

    Result listQuizzes(std::vector<Quiz>* quizzes) const override {
        if (quizzes == nullptr) {
            return validationError(
                "Quiz list output pointer must not be null.");
        }

        return quizzes_.listQuizzes(quizzes);
    }

    Result startAttempt(const Session& session, const std::string& quiz_id, QuizMode mode, Attempt* out) override {
        if (!isValidSession(session) || session.role != Role::Student)
            return {ErrorCode::NOT_AUTHORIZED, "Only a signed-in student can start an attempt."};
        if (!out || (mode != QuizMode::Practice && mode != QuizMode::Graded))
            return validationError("Invalid mode or attempt output.");
        Quiz quiz;
        auto result = quizzes_.findQuiz(quiz_id, &quiz);
        if (!result.ok()) return result;
        if (quiz.question_ids.empty() || quiz.time_limit_minutes <= 0 ||
            !std::isfinite(quiz.scoring.marks_per_question) || quiz.scoring.marks_per_question <= 0 ||
            !std::isfinite(quiz.scoring.negative_mark) || quiz.scoring.negative_mark < 0)
            return validationError("Quiz configuration is invalid.");
        Attempt attempt;
        bool resumed = false;
        if (mode == QuizMode::Graded) {
            std::vector<Attempt> previous;
            result = attempts_.listAttemptsForStudent(session.user_id, &previous);
            if (!result.ok()) return result;
            if (std::any_of(previous.begin(), previous.end(), [&](const Attempt& a) {
                    return a.quiz_id == quiz_id && a.mode == QuizMode::Graded && a.submitted;
                })) return {ErrorCode::ATTEMPT_ALREADY_SUBMITTED, "This graded quiz has already been submitted."};
            result = attempts_.findOpenAttempt(quiz_id, session.user_id, &attempt);
            if (!result.ok() && result.code != ErrorCode::ATTEMPT_NOT_FOUND) return result;
            resumed = result.ok();
            if (resumed && (attempt.mode != mode || attempt.student_id != session.user_id || attempt.quiz_id != quiz_id))
                return validationError("Stored attempt identity is invalid.");
        }
        if (!resumed) {
            attempt.attempt_id = "attempt-" + generateQuizId();
            attempt.quiz_id = quiz_id;
            attempt.student_id = session.user_id;
            attempt.started_at = currentUtcTimestamp();
            attempt.mode = mode;
            attempt.question_order = quiz.question_ids;
            std::shuffle(attempt.question_order.begin(), attempt.question_order.end(), random_);
        }
        result = validateAttempt(attempt, quiz);
        if (!result.ok()) return result;
        if (mode == QuizMode::Graded && !resumed) {
            result = attempts_.saveInProgressAttempt(attempt);
            if (!result.ok()) return result;
        }
        active_[attempt.attempt_id] = attempt;
        *out = attempt;
        return {ErrorCode::OK, resumed ? "Open attempt resumed." : "Attempt started."};
    }

    Result saveAnswer(Attempt* attempt, const Answer& answer) override {
        Attempt current;
        auto result = currentAttempt(attempt, &current);
        if (!result.ok()) return result;
        if (std::find(current.question_order.begin(), current.question_order.end(), answer.question_id) == current.question_order.end())
            return validationError("Question is not part of this attempt.");
        PublicQuestion question;
        result = question_bank_.getQuestionForQuiz(answer.question_id, &question);
        if (!result.ok()) return result;
        if (answer.selected_option < kBlankAnswer ||
            (answer.selected_option != kBlankAnswer && static_cast<std::size_t>(answer.selected_option) >= question.options.size()))
            return validationError("Answer option is out of range.");
        const auto old = std::find_if(current.answers.begin(), current.answers.end(), [&](const Answer& a) {
            return a.question_id == answer.question_id;
        });
        if (old == current.answers.end()) current.answers.push_back(answer);
        else *old = answer;
        if (current.mode == QuizMode::Graded) {
            result = attempts_.saveInProgressAttempt(current);
            if (!result.ok()) return result;
        }
        active_[current.attempt_id] = current;
        *attempt = current;
        return {ErrorCode::OK, "Answer saved."};
    }

    Result submitAttempt(const Session& session, Attempt* attempt, bool timed_out, GradeReport* report) override {
        if (!isValidSession(session) || session.role != Role::Student || !attempt || attempt->student_id != session.user_id)
            return {ErrorCode::NOT_AUTHORIZED, "Only the attempt's student can submit it."};
        if (!report) return validationError("Report output must not be null.");
        if (timed_out) return notImplemented("Quiz timer/automatic submission");
        Attempt current;
        auto result = currentAttempt(attempt, &current);
        if (!result.ok()) return result;
        if (current.mode == QuizMode::Practice) {
            current.submitted = true;
            current.submitted_at = currentUtcTimestamp();
            active_[current.attempt_id] = current;
            *attempt = current;
            *report = {};
            return {ErrorCode::OK, "Practice completed; no result is stored."};
        }
        GradeReport computed;
        result = grading_.gradeAttempt(current.quiz_id, current.student_id, current.answers, &computed);
        if (!result.ok() && result.code != ErrorCode::TIME_EXPIRED) return result;
        Attempt stored;
        const auto persisted = attempts_.findAttempt(current.attempt_id, &stored);
        if (!persisted.ok()) return persisted;
        if (!stored.submitted) return {ErrorCode::IO_ERROR, "Grading did not finalize the attempt."};
        active_[stored.attempt_id] = stored;
        *attempt = stored;
        *report = computed;
        return result;
    }

private:
    QuestionBank& question_bank_;
    QuizRepository& quizzes_;
    AttemptRepository& attempts_;
    GradingModule& grading_;
    std::mt19937_64 random_;
    std::unordered_map<std::string, Attempt> active_;

    Result currentAttempt(const Attempt* supplied, Attempt* current) {
        if (!supplied) return validationError("Attempt must not be null.");
        const auto found = active_.find(supplied->attempt_id);
        if (found == active_.end()) return validationError("Start or resume the attempt before changing it.");
        *current = found->second;
        if (supplied->student_id != current->student_id || supplied->quiz_id != current->quiz_id ||
            supplied->mode != current->mode || supplied->question_order != current->question_order ||
            supplied->started_at != current->started_at)
            return validationError("Attempt identity cannot change.");
        if (current->mode == QuizMode::Graded) {
            const auto result = attempts_.findAttempt(current->attempt_id, current);
            if (!result.ok()) return result;
        }
        if (supplied->submitted || current->submitted)
            return {ErrorCode::ATTEMPT_ALREADY_SUBMITTED, "Attempt already submitted."};
        if (supplied->answers.size() != current->answers.size() ||
            !std::equal(supplied->answers.begin(), supplied->answers.end(), current->answers.begin(),
                [](const Answer& a, const Answer& b) {
                    return a.question_id == b.question_id && a.selected_option == b.selected_option &&
                           a.flagged_for_review == b.flagged_for_review;
                })) return validationError("Answer state changed; resume the attempt and use saveAnswer.");
        return {};
    }

    Result validateAttempt(const Attempt& attempt, const Quiz& quiz) const {
        const std::unordered_set<std::string> expected(quiz.question_ids.begin(), quiz.question_ids.end());
        const std::unordered_set<std::string> actual(attempt.question_order.begin(), attempt.question_order.end());
        if (expected.size() != quiz.question_ids.size() || actual.size() != attempt.question_order.size() || actual != expected)
            return validationError("Attempt must contain each quiz question exactly once.");
        std::unordered_map<std::string, std::size_t> option_counts;
        for (const auto& id : attempt.question_order) {
            PublicQuestion question;
            const auto result = question_bank_.getQuestionForQuiz(id, &question);
            if (!result.ok()) return result;
            if (question.options.size() < 2) return validationError("Question requires at least two options.");
            option_counts.emplace(id, question.options.size());
        }
        std::unordered_set<std::string> answered;
        for (const auto& answer : attempt.answers) {
            const auto count = option_counts.find(answer.question_id);
            if (count == option_counts.end() || !answered.insert(answer.question_id).second ||
                answer.selected_option < kBlankAnswer || (answer.selected_option != kBlankAnswer &&
                static_cast<std::size_t>(answer.selected_option) >= count->second))
                return validationError("Stored attempt contains an invalid answer.");
        }
        return {};
    }
};

}  // namespace

std::unique_ptr<QuizEngine> createQuizEngine(
    QuestionBank& question_bank,
    QuizRepository& quizzes,
    AttemptRepository& attempts,
    GradingModule& grading) {

    return std::make_unique<QuizEngineImpl>(
        question_bank,
        quizzes,
        attempts,
        grading,
        std::random_device{}());
}

std::unique_ptr<QuizEngine> createQuizEngine(QuestionBank& bank, QuizRepository& quizzes,
                                             AttemptRepository& attempts, GradingModule& grading,
                                             std::uint64_t random_seed) {
    return std::make_unique<QuizEngineImpl>(bank, quizzes, attempts, grading, random_seed);
}

}  // namespace quizsphere
