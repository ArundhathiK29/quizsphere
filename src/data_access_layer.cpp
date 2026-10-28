#include "data_access_layer.hpp"

#include "sqlite_user_repository.hpp"

namespace quizsphere {
namespace {

// BL-01 stubs. BL-06 replaces these with SQLite-backed implementations.
class StubQuizRepository final : public QuizRepository {
public:
    Result saveQuiz(const Quiz&) override { return notImplemented("QuizRepository::saveQuiz"); }
    Result findQuiz(const std::string&, Quiz*) const override { return notImplemented("QuizRepository::findQuiz"); }
    Result listQuizzes(std::vector<Quiz>*) const override { return notImplemented("QuizRepository::listQuizzes"); }
};

class StubAttemptRepository final : public AttemptRepository {
public:
    Result saveInProgressAttempt(const Attempt&) override { return notImplemented("AttemptRepository::saveInProgressAttempt"); }
    Result findAttempt(const std::string&, Attempt*) const override { return notImplemented("AttemptRepository::findAttempt"); }
    Result findOpenAttempt(const std::string&, const std::string&, Attempt*) const override {
        return notImplemented("AttemptRepository::findOpenAttempt");
    }
    Result saveGradedAttempt(const Attempt&, const GradeReport&) override {
        return notImplemented("AttemptRepository::saveGradedAttempt");
    }
    Result findReport(const std::string&, const std::string&, GradeReport*) const override {
        return notImplemented("AttemptRepository::findReport");
    }
    Result listGradedAttempts(const std::string&, std::vector<Attempt>*) const override {
        return notImplemented("AttemptRepository::listGradedAttempts");
    }
    Result listAttemptsForStudent(const std::string&, std::vector<Attempt>*) const override {
        return notImplemented("AttemptRepository::listAttemptsForStudent");
    }
};

}  // namespace

std::unique_ptr<DataAccessLayer> createSqliteDataAccessLayer(const std::string& database_path, std::string* error) {
    auto users = std::make_unique<SQLiteUserRepository>(database_path);
    if (!users->isOpen()) {
        if (error != nullptr) {
            *error = users->error();
        }
        return nullptr;
    }
    auto layer = std::make_unique<DataAccessLayer>();
    layer->users = std::move(users);
    layer->quizzes = std::make_unique<StubQuizRepository>();
    layer->attempts = std::make_unique<StubAttemptRepository>();
    return layer;
}

}  // namespace quizsphere
