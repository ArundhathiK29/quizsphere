
#ifndef QUIZSPHERE_AUTH_H
#define QUIZSPHERE_AUTH_H

#include <string>

namespace QuizSphere {

enum class UserRole {
    Student,
    Instructor
};

struct User {
    int id = 0;
    std::string username;
    std::string passwordHash;
    UserRole role = UserRole::Student;
    bool enabled = true;
};

enum class AuthStatus {
    Success,
    InvalidInput,
    UsernameAlreadyExists,
    InvalidCredentials,
    AccountDisabled,
    StorageError
};

struct AuthResult {
    AuthStatus status;
    int userId = 0;
    UserRole role = UserRole::Student;
};
bool isValidUsername(const std::string& username);
bool isValidRole(UserRole role);
} // namespace QuizSphere

#endif
