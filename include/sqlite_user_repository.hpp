#pragma once

#include <sqlite3.h>

#include <string>

#include "user_repository.hpp"

namespace quizsphere {

// SQLite-backed accounts. All SQL uses prepared statements with bound parameters.
class SQLiteUserRepository final : public IUserRepository {
public:
    // Pass ":memory:" for a throw-away in-memory database (tests).
    explicit SQLiteUserRepository(const std::string& database_path);
    ~SQLiteUserRepository() override;

    SQLiteUserRepository(const SQLiteUserRepository&) = delete;
    SQLiteUserRepository& operator=(const SQLiteUserRepository&) = delete;

    [[nodiscard]] bool isOpen() const;
    // Why the database could not be opened (empty when open).
    [[nodiscard]] const std::string& error() const;

    bool usernameExists(const std::string& username, bool& exists) override;
    bool createUser(const User& user, int& new_user_id) override;
    bool findUserByUsername(const std::string& username, User& user, bool& found) override;
    bool findUserById(int user_id, User& user, bool& found) override;
    bool updateUserStatus(int user_id, bool enabled, bool& updated) override;
    bool updateUserRole(int user_id, Role role, bool& updated) override;
    bool deleteUser(int user_id, bool& deleted) override;
    bool setLoginFailureState(int user_id, int failed_attempts, std::int64_t locked_until) override;
    bool countAdmins(int& count) override;

private:
    bool initializeSchema();
    bool findUser(const char* sql, const std::string* text_key, int int_key, User& user, bool& found);

    sqlite3* db_{nullptr};
    std::string error_;
};

}  // namespace quizsphere
