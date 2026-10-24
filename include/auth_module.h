#pragma once

#include <string>

namespace QuizSphere {

enum class UserRole {
    Student,
    Instructor
};

struct Session {
    std::string user_id;
    UserRole role = UserRole::Student;
    std::string logged_in_at;
};

enum class LoginError {
    None,
    InvalidCredentials,
    AccountDisabled,
    StorageError
};

struct LoginResult {
    bool success = false;
    Session session{};
    LoginError error = LoginError::None;
};

class AuthModule {
public:
    virtual ~AuthModule() = default;

    virtual LoginResult Login(
        const std::string& username,
        const std::string& password
    ) = 0;
};

} // namespace QuizSphere
