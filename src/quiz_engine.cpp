#include "quiz_engine.hpp"

namespace quizsphere {
namespace {

// BL-01 stub. Ashwin replaces the bodies (BL-07, BL-08, BL-19..21); keep the interface in quiz_engine.hpp.
class StubQuizEngine final : public QuizEngine {
public:
    Result createQuiz(const Session&, const QuizConfig&, Quiz*) override { return notImplemented("QuizEngine::createQuiz"); }
    Result getQuiz(const std::string&, Quiz*) const override { return notImplemented("QuizEngine::getQuiz"); }
    Result listQuizzes(std::vector<Quiz>*) const override { return notImplemented("QuizEngine::listQuizzes"); }
    Result startAttempt(const Session&, const std::string&, QuizMode, Attempt*) override {
        return notImplemented("QuizEngine::startAttempt");
    }
    Result saveAnswer(Attempt*, const Answer&) override { return notImplemented("QuizEngine::saveAnswer"); }
    Result submitAttempt(const Session&, Attempt*, bool, GradeReport*) override {
        return notImplemented("QuizEngine::submitAttempt");
    }
};

}  // namespace

std::unique_ptr<QuizEngine> createQuizEngine(QuestionBank&, QuizRepository&, AttemptRepository&, GradingModule&) {
    return std::make_unique<StubQuizEngine>();
}

}  // namespace quizsphere
