#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common_types.hpp"
#include "quiz_types.hpp"
#include "user_repository.hpp"

// Data Access Layer (BL-06, owner: Ashwin). The ONLY code allowed to talk to SQLite or the filesystem
// (SAD 3.4); every other module goes through these interfaces. Writes that span several tables must be atomic.
// Question storage already lives behind QuestionRepository (question_repository.hpp); user storage behind
// IUserRepository (user_repository.hpp).
namespace quizsphere {

class QuizRepository {
public:
    virtual ~QuizRepository() = default;
    virtual Result saveQuiz(const Quiz& quiz) = 0;
    virtual Result findQuiz(const std::string& quiz_id, Quiz* quiz) const = 0;      // QUIZ_NOT_FOUND
    virtual Result listQuizzes(std::vector<Quiz>* quizzes) const = 0;
};

class AttemptRepository {
public:
    virtual ~AttemptRepository() = default;
    // Persist an in-progress attempt (so the timer deadline survives a restart).
    virtual Result saveInProgressAttempt(const Attempt& attempt) = 0;
    virtual Result findAttempt(const std::string& attempt_id, Attempt* attempt) const = 0;   // ATTEMPT_NOT_FOUND
    // The student's open (not yet submitted) attempt for a quiz, if any.
    virtual Result findOpenAttempt(const std::string& quiz_id, const std::string& student_id, Attempt* attempt) const = 0;
    // Atomically store the submitted attempt, its answers and the grade (all or nothing).
    // ATTEMPT_ALREADY_SUBMITTED if a graded attempt already exists (FR-015, NFR-006).
    virtual Result saveGradedAttempt(const Attempt& attempt, const GradeReport& report) = 0;
    virtual Result findReport(const std::string& quiz_id, const std::string& student_id, GradeReport* report) const = 0;
    virtual Result listGradedAttempts(const std::string& quiz_id, std::vector<Attempt>* attempts) const = 0;
    virtual Result listAttemptsForStudent(const std::string& student_id, std::vector<Attempt>* attempts) const = 0;
};

struct DataAccessLayer {
    std::unique_ptr<IUserRepository> users;
    std::unique_ptr<QuizRepository> quizzes;
    std::unique_ptr<AttemptRepository> attempts;
};

// Opens (creating if needed) the SQLite database with tables for users, questions, quizzes, attempts and answers.
// Returns nullptr and fills `error` if the database cannot be opened.
// NOTE: BL-01 stub - users are real, quizzes/attempts return NOT_IMPLEMENTED until BL-06 is done.
std::unique_ptr<DataAccessLayer> createSqliteDataAccessLayer(const std::string& database_path, std::string* error);

}  // namespace quizsphere
