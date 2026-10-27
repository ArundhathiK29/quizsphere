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
    CONFIRMATION_REQUIRED
};

struct Result {
    ErrorCode code{ErrorCode::OK};
    std::string message;

    [[nodiscard]] bool ok() const { return code == ErrorCode::OK; }
};

std::string roleToString(Role role);

}  // namespace quizsphere
