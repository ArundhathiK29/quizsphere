#pragma once

#include <string>

namespace quizsphere {

enum class Role {
    Student,
    Instructor,
    Admin
};

struct Session {
    std::string user_id;
    Role role{Role::Student};
    std::string logged_in_at;
};

enum class ErrorCode {
    OK,
    NOT_AUTHORIZED,
    VALIDATION_FAILED,
    DUPLICATE_ID,
    QUESTION_NOT_FOUND,
    QUESTION_IN_USE,
    IO_ERROR,
    CSV_STRUCTURE_INVALID,
    CONFIRMATION_REQUIRED,
    // Quiz engine / grading errors (SAD 4.3)
    QUIZ_NOT_FOUND,
    ATTEMPT_NOT_FOUND,
    ATTEMPT_ALREADY_SUBMITTED,
    TIME_EXPIRED,
    // Returned by module stubs until the owner implements them
    NOT_IMPLEMENTED
};

struct Result {
    ErrorCode code{ErrorCode::OK};
    std::string message;

    [[nodiscard]] bool ok() const { return code == ErrorCode::OK; }
};

std::string roleToString(Role role);

// Used by interface stubs so unfinished modules fail loudly instead of silently succeeding.
inline Result notImplemented(const std::string& feature) {
    return {ErrorCode::NOT_IMPLEMENTED, feature + " is not implemented yet."};
}

}  // namespace quizsphere
