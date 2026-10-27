#include "sqlite_user_repository.hpp"

namespace quizsphere {
namespace {

constexpr int kSchemaVersion = 2;

// Owns a prepared statement so every early return finalises it.
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            stmt_ = nullptr;
        }
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool valid() const { return stmt_ != nullptr; }
    [[nodiscard]] sqlite3_stmt* get() const { return stmt_; }

    void bindText(int index, const std::string& value) const {
        sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }
    void bindInt(int index, int value) const { sqlite3_bind_int(stmt_, index, value); }
    void bindInt64(int index, std::int64_t value) const { sqlite3_bind_int64(stmt_, index, value); }

private:
    sqlite3_stmt* stmt_{nullptr};
};

const char* roleToDb(Role role) {
    switch (role) {
        case Role::Student: return "Student";
        case Role::Instructor: return "Instructor";
        case Role::Admin: return "Admin";
    }
    return nullptr;
}

bool roleFromDb(const std::string& text, Role& role) {
    if (text == "Student") { role = Role::Student; return true; }
    if (text == "Instructor") { role = Role::Instructor; return true; }
    if (text == "Admin") { role = Role::Admin; return true; }
    return false;
}

std::string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

}  // namespace

SQLiteUserRepository::SQLiteUserRepository(const std::string& database_path) {
    if (sqlite3_open(database_path.c_str(), &db_) != SQLITE_OK) {
        error_ = db_ != nullptr ? sqlite3_errmsg(db_) : "unable to allocate database handle";
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    if (!initializeSchema()) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

SQLiteUserRepository::~SQLiteUserRepository() {
    sqlite3_close(db_);
    db_ = nullptr;
}

bool SQLiteUserRepository::isOpen() const { return db_ != nullptr; }

const std::string& SQLiteUserRepository::error() const { return error_; }

bool SQLiteUserRepository::initializeSchema() {
    // user_version 0 with no users table = brand-new file; anything else must already be our schema.
    int version = 0;
    {
        Statement pragma(db_, "PRAGMA user_version;");
        if (!pragma.valid() || sqlite3_step(pragma.get()) != SQLITE_ROW) {
            error_ = "unable to read database schema version";
            return false;
        }
        version = sqlite3_column_int(pragma.get(), 0);
    }
    bool has_users_table = false;
    {
        Statement probe(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='users' LIMIT 1;");
        has_users_table = probe.valid() && sqlite3_step(probe.get()) == SQLITE_ROW;
    }
    if (has_users_table && version != kSchemaVersion) {
        error_ = "this database was created by an older QuizSphere build (schema v" + std::to_string(version) +
                 ", expected v" + std::to_string(kSchemaVersion) + "); delete the old development database file";
        return false;
    }

    const char* sql =
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  password_hash TEXT NOT NULL,"
        "  role TEXT NOT NULL CHECK(role IN ('Student','Instructor','Admin')),"
        "  enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),"
        "  failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK(failed_attempts >= 0),"
        "  locked_until INTEGER NOT NULL DEFAULT 0"
        ");"
        "PRAGMA user_version = 2;";

    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error_ = message != nullptr ? message : "schema initialisation failed";
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool SQLiteUserRepository::usernameExists(const std::string& username, bool& exists) {
    exists = false;
    if (db_ == nullptr) return false;

    Statement stmt(db_, "SELECT 1 FROM users WHERE username = ?1 LIMIT 1;");
    if (!stmt.valid()) return false;
    stmt.bindText(1, username);

    const int rc = sqlite3_step(stmt.get());
    exists = (rc == SQLITE_ROW);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool SQLiteUserRepository::createUser(const User& user, int& new_user_id) {
    new_user_id = 0;
    const char* role = roleToDb(user.role);
    if (db_ == nullptr || role == nullptr) return false;

    Statement stmt(db_,
        "INSERT INTO users (username, password_hash, role, enabled, failed_attempts, locked_until) "
        "VALUES (?1, ?2, ?3, ?4, 0, 0);");
    if (!stmt.valid()) return false;
    stmt.bindText(1, user.username);
    stmt.bindText(2, user.password_hash);
    stmt.bindText(3, role);
    stmt.bindInt(4, user.enabled ? 1 : 0);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return false;
    new_user_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool SQLiteUserRepository::findUser(const char* sql, const std::string* text_key, int int_key,
                                    User& user, bool& found) {
    found = false;
    if (db_ == nullptr) return false;

    Statement stmt(db_, sql);
    if (!stmt.valid()) return false;
    if (text_key != nullptr) {
        stmt.bindText(1, *text_key);
    } else {
        stmt.bindInt(1, int_key);
    }

    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return true;
    if (rc != SQLITE_ROW) return false;

    Role role = Role::Student;
    if (!roleFromDb(columnText(stmt.get(), 3), role)) return false;  // corrupt row = storage error

    user.id = sqlite3_column_int(stmt.get(), 0);
    user.username = columnText(stmt.get(), 1);
    user.password_hash = columnText(stmt.get(), 2);
    user.role = role;
    user.enabled = sqlite3_column_int(stmt.get(), 4) == 1;
    user.failed_attempts = sqlite3_column_int(stmt.get(), 5);
    user.locked_until = sqlite3_column_int64(stmt.get(), 6);
    found = true;
    return true;
}

bool SQLiteUserRepository::findUserByUsername(const std::string& username, User& user, bool& found) {
    return findUser("SELECT id, username, password_hash, role, enabled, failed_attempts, locked_until "
                    "FROM users WHERE username = ?1 LIMIT 1;",
                    &username, 0, user, found);
}

bool SQLiteUserRepository::findUserById(int user_id, User& user, bool& found) {
    return findUser("SELECT id, username, password_hash, role, enabled, failed_attempts, locked_until "
                    "FROM users WHERE id = ?1 LIMIT 1;",
                    nullptr, user_id, user, found);
}

bool SQLiteUserRepository::updateUserStatus(int user_id, bool enabled, bool& updated) {
    updated = false;
    if (db_ == nullptr) return false;
    Statement stmt(db_, "UPDATE users SET enabled = ?1 WHERE id = ?2;");
    if (!stmt.valid()) return false;
    stmt.bindInt(1, enabled ? 1 : 0);
    stmt.bindInt(2, user_id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return false;
    updated = sqlite3_changes(db_) > 0;
    return true;
}

bool SQLiteUserRepository::updateUserRole(int user_id, Role role, bool& updated) {
    updated = false;
    const char* role_text = roleToDb(role);
    if (db_ == nullptr || role_text == nullptr) return false;
    Statement stmt(db_, "UPDATE users SET role = ?1 WHERE id = ?2;");
    if (!stmt.valid()) return false;
    stmt.bindText(1, role_text);
    stmt.bindInt(2, user_id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return false;
    updated = sqlite3_changes(db_) > 0;
    return true;
}

bool SQLiteUserRepository::deleteUser(int user_id, bool& deleted) {
    deleted = false;
    if (db_ == nullptr) return false;
    Statement stmt(db_, "DELETE FROM users WHERE id = ?1;");
    if (!stmt.valid()) return false;
    stmt.bindInt(1, user_id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return false;
    deleted = sqlite3_changes(db_) > 0;
    return true;
}

bool SQLiteUserRepository::setLoginFailureState(int user_id, int failed_attempts, std::int64_t locked_until) {
    if (db_ == nullptr) return false;
    Statement stmt(db_, "UPDATE users SET failed_attempts = ?1, locked_until = ?2 WHERE id = ?3;");
    if (!stmt.valid()) return false;
    stmt.bindInt(1, failed_attempts);
    stmt.bindInt64(2, locked_until);
    stmt.bindInt(3, user_id);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool SQLiteUserRepository::countAdmins(int& count) {
    count = 0;
    if (db_ == nullptr) return false;
    Statement stmt(db_, "SELECT COUNT(*) FROM users WHERE role = 'Admin';");
    if (!stmt.valid() || sqlite3_step(stmt.get()) != SQLITE_ROW) return false;
    count = sqlite3_column_int(stmt.get(), 0);
    return true;
}

}  // namespace quizsphere
