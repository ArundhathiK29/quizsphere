
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
    AuthStatus status = AuthStatus::InvalidInput;
    int userId = 0;
    UserRole role = UserRole::Student;
};

class IUserRepository;

bool isValidUsername(const std::string& username);
bool isValidRole(UserRole role);

AuthStatus validateRegistration(
    const std::string& username,
    UserRole role
);

AuthResult registerUser(
    IUserRepository& repository,
    const std::string& username,
    const std::string& password,
    UserRole role
);

AuthResult loginUser(
    IUserRepository& repository,
    const std::string& username,
    const std::string& password
);

AuthStatus setAccountEnabled(
    IUserRepository& repository,
    int userId,
    bool enabled
);

} // namespace QuizSphere

#endif
