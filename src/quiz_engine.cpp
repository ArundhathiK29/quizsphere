
#include "quiz_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

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
    QuizEngineImpl(QuestionBank& question_bank,
               QuizRepository& quizzes,
               AttemptRepository&,
               GradingModule&)
    : question_bank_(question_bank),
      quizzes_(quizzes) {}

    Result createQuiz(const Session& session,
                      const QuizConfig& config,
                      Quiz* quiz) override {
        if (!canCreateQuiz(session)) {
            return {ErrorCode::NOT_AUTHORIZED,
                    "Only authenticated instructors and admins can create quizzes."};
        }

        if (quiz == nullptr) {
            return validationError("Quiz output pointer must not be null.");
        }

        const std::string title = trim(config.title);

        if (title.empty()) {
            return validationError("Quiz title must not be empty.");
        }

        if (config.question_count <= 0) {
            return validationError("Question count must be greater than zero.");
        }

        if (config.time_limit_minutes <= 0) {
            return validationError("Time limit must be greater than zero.");
        }

        std::vector<PublicQuestion> available_questions;

        const Result question_result = question_bank_.listAvailable(
            &available_questions,
            config.subject_filter,
            config.difficulty_filter);

        if (!question_result.ok()) {
            return question_result;
        }

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
        new_quiz.scoring = config.scoring;

        new_quiz.question_ids.reserve(
            static_cast<std::size_t>(config.question_count));

        for (int i = 0; i < config.question_count; ++i) {
            new_quiz.question_ids.push_back(
                available_questions[static_cast<std::size_t>(i)].question_id);
        }

        const Result save_result = quizzes_.saveQuiz(new_quiz);

        if (!save_result.ok()) {
            return save_result;
        }

        *quiz = new_quiz;
        return {ErrorCode::OK, "Quiz created successfully."};
    }

    Result getQuiz(const std::string& quiz_id, Quiz* quiz) const override {
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

    Result startAttempt(const Session&, const std::string&, QuizMode,
                        Attempt*) override {
        return notImplemented("QuizEngine::startAttempt");
    }

    Result saveAnswer(Attempt*, const Answer&) override {
        return notImplemented("QuizEngine::saveAnswer");
    }

    Result submitAttempt(const Session&, Attempt*, bool,
                         GradeReport*) override {
        return notImplemented("QuizEngine::submitAttempt");
    }

private:
    QuestionBank& question_bank_;
    QuizRepository& quizzes_;
};

}  // namespace

std::unique_ptr<QuizEngine> createQuizEngine(
    QuestionBank& question_bank,
    QuizRepository& quizzes,
    AttemptRepository& attempts,
    GradingModule& grading) {
    return std::make_unique<QuizEngineImpl>(
        question_bank, quizzes, attempts, grading);
}

}  // namespace quizsphere
