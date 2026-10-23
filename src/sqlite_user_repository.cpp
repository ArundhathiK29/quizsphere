
#include "sqlite_user_repository.h"

#include <string>

namespace QuizSphere {

SQLiteUserRepository::SQLiteUserRepository(
    const std::string& databasePath) {

    if (sqlite3_open(databasePath.c_str(), &db_) != SQLITE_OK) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return;
    }

    if (!initializeSchema()) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

SQLiteUserRepository::~SQLiteUserRepository() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SQLiteUserRepository::isOpen() const {
    return db_ != nullptr;
}

bool SQLiteUserRepository::initializeSchema() {
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL UNIQUE,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL "
        "CHECK(role IN ('Student', 'Instructor')),"
        "enabled INTEGER NOT NULL DEFAULT 1 "
        "CHECK(enabled IN (0, 1))"
        ");";

    char* errorMessage = nullptr;

    const int result = sqlite3_exec(
        db_, sql, nullptr, nullptr, &errorMessage
    );

    sqlite3_free(errorMessage);

    return result == SQLITE_OK;
}

bool SQLiteUserRepository::usernameExists(
    const std::string& username,
    bool& exists) {

    exists = false;

    if (db_ == nullptr) {
        return false;
    }

    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "SELECT 1 FROM users WHERE username = ? LIMIT 1;";

    if (sqlite3_prepare_v2(
            db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(
        statement, 1, username.c_str(), -1, SQLITE_TRANSIENT
    );

    const int result = sqlite3_step(statement);

    if (result == SQLITE_ROW) {
        exists = true;
    }

    const bool success =
        result == SQLITE_ROW || result == SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

bool SQLiteUserRepository::createUser(
    const User& user,
    int& newUserId) {

    newUserId = 0;

    if (db_ == nullptr) {
        return false;
    }

    const char* role = nullptr;

    if (user.role == UserRole::Student) {
        role = "Student";
    } else if (user.role == UserRole::Instructor) {
        role = "Instructor";
    } else {
        return false;
    }

    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "INSERT INTO users "
        "(username, password_hash, role, enabled) "
        "VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(
            db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(
        statement, 1, user.username.c_str(), -1, SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement, 2, user.passwordHash.c_str(), -1, SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement, 3, role, -1, SQLITE_STATIC
    );

    sqlite3_bind_int(
        statement, 4, user.enabled ? 1 : 0
    );

    const bool success =
        sqlite3_step(statement) == SQLITE_DONE;

    if (success) {
        newUserId = static_cast<int>(
            sqlite3_last_insert_rowid(db_)
        );
    }

    sqlite3_finalize(statement);

    return success;
}

bool SQLiteUserRepository::findUserByUsername(
    const std::string& username,
    User& user,
    bool& found) {

    found = false;

    if (db_ == nullptr) {
        return false;
    }

    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "SELECT id, username, password_hash, role, enabled "
        "FROM users WHERE username = ? LIMIT 1;";

    if (sqlite3_prepare_v2(
            db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(
        statement, 1, username.c_str(), -1, SQLITE_TRANSIENT
    );

    const int result = sqlite3_step(statement);

    if (result == SQLITE_ROW) {
        user.id = sqlite3_column_int(statement, 0);

        const auto* usernameText =
            sqlite3_column_text(statement, 1);
        const auto* hashText =
            sqlite3_column_text(statement, 2);
        const auto* roleText =
            sqlite3_column_text(statement, 3);

        user.username = usernameText
            ? reinterpret_cast<const char*>(usernameText) : "";

        user.passwordHash = hashText
            ? reinterpret_cast<const char*>(hashText) : "";

        const std::string role = roleText
            ? reinterpret_cast<const char*>(roleText) : "";

        if (role == "Student") {
            user.role = UserRole::Student;
        } else if (role == "Instructor") {
            user.role = UserRole::Instructor;
        } else {
            sqlite3_finalize(statement);
            return false;
        }

        user.enabled = sqlite3_column_int(statement, 4) == 1;
        found = true;
    }

    const bool success =
        result == SQLITE_ROW || result == SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

bool SQLiteUserRepository::updateUserStatus(
    int userId,
    bool enabled) {

    if (db_ == nullptr) {
        return false;
    }

    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "UPDATE users SET enabled = ? WHERE id = ?;";

    if (sqlite3_prepare_v2(
            db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(
        statement, 1, enabled ? 1 : 0
    );

    sqlite3_bind_int(
        statement, 2, userId
    );

    const bool executed =
        sqlite3_step(statement) == SQLITE_DONE;

    const bool updated =
        executed && sqlite3_changes(db_) > 0;

    sqlite3_finalize(statement);

    return updated;
}

} // namespace QuizSphere
