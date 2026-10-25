
#include "auth.h"
#include "user_repository.h"

#include <cassert>
#include <iostream>
#include <map>

using namespace QuizSphere;

class FakeRepository final : public IUserRepository {
public:
    bool usernameExists(
        const std::string& username,
        bool& exists
    ) override {
        if (fail) return false;

        exists = users.count(username) > 0;
        return true;
    }

    bool createUser(
        const User& user,
        int& newUserId
    ) override {
        if (fail || users.count(user.username)) {
            return false;
        }

        User copy = user;
        copy.id = nextId++;

        newUserId = copy.id;
        users[copy.username] = copy;

        return true;
    }

    bool findUserByUsername(
        const std::string& username,
        User& user,
        bool& found
    ) override {
        if (fail) return false;

        auto it = users.find(username);
        found = it != users.end();

        if (found) {
            user = it->second;
        }

        return true;
    }

    bool updateUserStatus(
        int userId,
        bool enabled
    ) override {
        if (fail) return false;

        for (auto& entry : users) {
            if (entry.second.id == userId) {
                entry.second.enabled = enabled;
                return true;
            }
        }

        return false;
    }

    std::map<std::string, User> users;
    int nextId = 1;
    bool fail = false;
};

int main() {
    FakeRepository repository;

    // Username and role validation
    assert(isValidUsername("aruu_123"));
    assert(!isValidUsername("ab"));
    assert(!isValidUsername("bad name"));

    assert(isValidRole(UserRole::Student));
    assert(isValidRole(UserRole::Instructor));

    assert(
        validateRegistration("", UserRole::Student)
        == AuthStatus::InvalidInput
    );

    // Successful registration
    AuthResult registration = registerUser(
        repository,
        "aruu_123",
        "CorrectHorse9!",
        UserRole::Student
    );

    assert(registration.status == AuthStatus::Success);
    assert(registration.userId > 0);

    const User& stored = repository.users.at("aruu_123");

    // Never store the raw password
    assert(stored.passwordHash != "CorrectHorse9!");
    assert(stored.passwordHash.find("pbkdf2-sha256$") == 0);

    // Duplicate username
    assert(
        registerUser(
            repository, "aruu_123",
            "AnotherPass9!", UserRole::Student
        ).status == AuthStatus::UsernameAlreadyExists
    );

    // Password must be at least 8 characters
    assert(
        registerUser(
            repository, "valid_user",
            "short", UserRole::Student
        ).status == AuthStatus::InvalidInput
    );

    // Successful and unsuccessful login
    AuthResult login = loginUser(
        repository, "aruu_123", "CorrectHorse9!"
    );

    assert(login.status == AuthStatus::Success);
    assert(login.role == UserRole::Student);

    assert(
        loginUser(
            repository, "aruu_123", "wrongpassword"
        ).status == AuthStatus::InvalidCredentials
    );

    assert(
        loginUser(
            repository, "missing_user", "CorrectHorse9!"
        ).status == AuthStatus::InvalidCredentials
    );

    // Disabled accounts cannot log in
    assert(
        setAccountEnabled(
            repository, registration.userId, false
        ) == AuthStatus::Success
    );

    assert(
        loginUser(
            repository, "aruu_123", "CorrectHorse9!"
        ).status == AuthStatus::AccountDisabled
    );

    assert(
        setAccountEnabled(
            repository, registration.userId, true
        ) == AuthStatus::Success
    );

    // Storage failures are handled
    repository.fail = true;

    assert(
        registerUser(
            repository, "new_user",
            "StrongPass9!", UserRole::Student
        ).status == AuthStatus::StorageError
    );

    std::cout << "All authentication tests passed.\n";
    return 0;
}
