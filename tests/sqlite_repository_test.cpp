#include "auth.h"
#include "sqlite_user_repository.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace QuizSphere;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("TEST FAILED: " + message);
    }
}

} // namespace

int main() {
    const std::string databasePath =
        "quizsphere_integration_test.db";

    std::remove(databasePath.c_str());

    try {
        int registeredUserId = 0;

        {
            SQLiteUserRepository repository(databasePath);

            require(repository.isOpen(),
                    "Database should open and initialize");

            // Test the repository's basic storage operations.
            bool exists = false;
            require(repository.usernameExists("aruu_test", exists),
                    "Username lookup should succeed");
            require(!exists, "New username should not exist");

            User directUser;
            directUser.username = "aruu_test";
            directUser.passwordHash = "test_hash_not_a_real_password";
            directUser.role = UserRole::Student;
            directUser.enabled = true;

            int directUserId = 0;
            require(repository.createUser(directUser, directUserId),
                    "Repository should insert a user");
            require(directUserId > 0,
                    "Inserted user should have an ID");

            int duplicateId = 0;
            require(!repository.createUser(directUser, duplicateId),
                    "Duplicate username must be rejected");

            User retrieved;
            bool found = false;

            require(repository.findUserByUsername(
                        "aruu_test", retrieved, found),
                    "User lookup should execute successfully");
            require(found, "Inserted user should be found");
            require(retrieved.id == directUserId,
                    "Retrieved ID should match");
            require(retrieved.passwordHash == directUser.passwordHash,
                    "Stored hash field should match");
            require(retrieved.role == UserRole::Student,
                    "Stored role should match");

            require(repository.updateUserStatus(directUserId, false),
                    "Account should be disabled");
            require(repository.updateUserStatus(directUserId, true),
                    "Account should be re-enabled");

            // Test registration through the real SQLite repository.
            AuthResult registration = registerUser(
                repository,
                "aruu_integration",
                "CorrectHorse9!",
                UserRole::Student
            );

            require(registration.status == AuthStatus::Success,
                    "Valid registration should succeed");
            require(registration.userId > 0,
                    "Registration should return a valid user ID");

            registeredUserId = registration.userId;

            User registeredUser;
            found = false;

            require(repository.findUserByUsername(
                        "aruu_integration", registeredUser, found),
                    "Registered user lookup should succeed");
            require(found, "Registered user should be saved in SQLite");
            require(registeredUser.id == registeredUserId,
                    "Saved user ID should match registration result");
            require(registeredUser.passwordHash != "CorrectHorse9!",
                    "Plaintext password must not be stored");
            require(
                registeredUser.passwordHash.find("pbkdf2-sha256$") == 0,
                "Stored password should use the expected hash format"
            );
            require(registeredUser.role == UserRole::Student,
                    "Registered role should be preserved");

            // Duplicate registration must be rejected.
            AuthResult duplicateRegistration = registerUser(
                repository,
                "aruu_integration",
                "AnotherPass9!",
                UserRole::Student
            );

            require(
                duplicateRegistration.status ==
                    AuthStatus::UsernameAlreadyExists,
                "Duplicate registration should be rejected"
            );

            // Invalid usernames and unsupported roles must be rejected.
            require(
                registerUser(
                    repository, "ab", "CorrectHorse9!",
                    UserRole::Student
                ).status == AuthStatus::InvalidInput,
                "Too-short username should be rejected"
            );

            require(
                registerUser(
                    repository, "invalid_role", "CorrectHorse9!",
                    static_cast<UserRole>(99)
                ).status == AuthStatus::InvalidInput,
                "Unsupported role should be rejected"
            );

            // The authentication service must verify the actual stored hash.
            AuthResult successfulLogin = loginUser(
                repository, "aruu_integration", "CorrectHorse9!"
            );

            require(successfulLogin.status == AuthStatus::Success,
                    "Correct password should authenticate");
            require(successfulLogin.userId == registeredUserId,
                    "Login should return the registered user ID");

            require(
                loginUser(
                    repository, "aruu_integration", "WrongPassword9!"
                ).status == AuthStatus::InvalidCredentials,
                "Wrong password should be rejected"
            );

            require(
                loginUser(
                    repository, "missing_user", "CorrectHorse9!"
                ).status == AuthStatus::InvalidCredentials,
                "Unknown username should be rejected"
            );
        }

        // Reopen the database and verify that the registration persisted.
        {
            SQLiteUserRepository reopenedRepository(databasePath);

            require(reopenedRepository.isOpen(),
                    "Existing database should reopen");

            User persistedUser;
            bool found = false;

            require(reopenedRepository.findUserByUsername(
                        "aruu_integration", persistedUser, found),
                    "Persisted user lookup should succeed");
            require(found,
                    "Registered user should survive database reopening");
            require(persistedUser.id == registeredUserId,
                    "Persisted user ID should remain unchanged");
            require(persistedUser.passwordHash != "CorrectHorse9!",
                    "Persisted password must not be plaintext");
        }

        std::remove(databasePath.c_str());

        std::cout
            << "All SQLite repository and registration integration tests passed.\n";

        return 0;
    } catch (const std::exception& error) {
        std::remove(databasePath.c_str());
        std::cerr << error.what() << '\n';
        return 1;
    }
}
