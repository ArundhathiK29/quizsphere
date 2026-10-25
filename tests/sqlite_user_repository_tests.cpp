#include <gtest/gtest.h>
#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "auth.hpp"
#include "sqlite_user_repository.hpp"

using namespace quizsphere;

namespace {

std::string uniqueDbPath() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() / ("quizsphere_test_" + std::to_string(ticks) + ".db")).string();
}

struct TempDb {
    std::string path = uniqueDbPath();
    ~TempDb() { std::remove(path.c_str()); }
};

User makeUser(const std::string& name, Role role = Role::Student) {
    User user;
    user.username = name;
    user.password_hash = "pbkdf2-sha256$10000$00$00";  // opaque to the repository
    user.role = role;
    return user;
}

}  // namespace

TEST(SqliteUserRepository, OpensAndCreatesSchema) {
    SQLiteUserRepository repo(":memory:");
    ASSERT_TRUE(repo.isOpen()) << repo.error();
    bool exists = true;
    EXPECT_TRUE(repo.usernameExists("nobody", exists));
    EXPECT_FALSE(exists);
}

TEST(SqliteUserRepository, StoresAndReturnsEveryField) {
    SQLiteUserRepository repo(":memory:");
    ASSERT_TRUE(repo.isOpen());

    User user = makeUser("aruu_123", Role::Instructor);
    user.enabled = false;
    int id = 0;
    ASSERT_TRUE(repo.createUser(user, id));
    EXPECT_GT(id, 0);

    User by_name;
    bool found = false;
    ASSERT_TRUE(repo.findUserByUsername("aruu_123", by_name, found));
    ASSERT_TRUE(found);
    EXPECT_EQ(by_name.id, id);
    EXPECT_EQ(by_name.role, Role::Instructor);
    EXPECT_FALSE(by_name.enabled);
    EXPECT_EQ(by_name.password_hash, user.password_hash);
    EXPECT_EQ(by_name.failed_attempts, 0);
    EXPECT_EQ(by_name.locked_until, 0);

    User by_id;
    ASSERT_TRUE(repo.findUserById(id, by_id, found));
    EXPECT_TRUE(found);
    EXPECT_EQ(by_id.username, "aruu_123");

    ASSERT_TRUE(repo.findUserById(id + 100, by_id, found));
    EXPECT_FALSE(found);
}

TEST(SqliteUserRepository, UT_AUTH_03_UsernamesAreUniqueIgnoringCase) {
    SQLiteUserRepository repo(":memory:");
    ASSERT_TRUE(repo.isOpen());
    int id = 0;
    ASSERT_TRUE(repo.createUser(makeUser("Aruu_123"), id));
    EXPECT_FALSE(repo.createUser(makeUser("aruu_123"), id));
    EXPECT_FALSE(repo.createUser(makeUser("ARUU_123"), id));

    bool exists = false;
    ASSERT_TRUE(repo.usernameExists("aRuU_123", exists));
    EXPECT_TRUE(exists);
}

TEST(SqliteUserRepository, SqlInjectionStringsAreTreatedAsPlainData) {
    SQLiteUserRepository repo(":memory:");
    ASSERT_TRUE(repo.isOpen());
    int id = 0;
    ASSERT_TRUE(repo.createUser(makeUser("real_user"), id));

    bool exists = true;
    ASSERT_TRUE(repo.usernameExists("x' OR '1'='1", exists));
    EXPECT_FALSE(exists);

    User user;
    bool found = true;
    ASSERT_TRUE(repo.findUserByUsername("'; DROP TABLE users; --", user, found));
    EXPECT_FALSE(found);

    // Table still intact and the real user still there.
    ASSERT_TRUE(repo.findUserByUsername("real_user", user, found));
    EXPECT_TRUE(found);
}

TEST(SqliteUserRepository, UpdateRoleStatusAndDeleteReportWhetherARowChanged) {
    SQLiteUserRepository repo(":memory:");
    ASSERT_TRUE(repo.isOpen());
    int id = 0;
    ASSERT_TRUE(repo.createUser(makeUser("aruu_123"), id));
    bool changed = false;

    ASSERT_TRUE(repo.updateUserStatus(id, false, changed));
    EXPECT_TRUE(changed);
    ASSERT_TRUE(repo.updateUserStatus(id + 50, false, changed));
    EXPECT_FALSE(changed);

    ASSERT_TRUE(repo.updateUserRole(id, Role::Admin, changed));
    EXPECT_TRUE(changed);
    int admins = 0;
    ASSERT_TRUE(repo.countAdmins(admins));
    EXPECT_EQ(admins, 1);

    ASSERT_TRUE(repo.setLoginFailureState(id, 4, 123456789012LL));
    User user;
    bool found = false;
    ASSERT_TRUE(repo.findUserById(id, user, found));
    EXPECT_FALSE(user.enabled);
    EXPECT_EQ(user.role, Role::Admin);
    EXPECT_EQ(user.failed_attempts, 4);
    EXPECT_EQ(user.locked_until, 123456789012LL);  // 64-bit timestamps survive

    ASSERT_TRUE(repo.deleteUser(id, changed));
    EXPECT_TRUE(changed);
    ASSERT_TRUE(repo.deleteUser(id, changed));
    EXPECT_FALSE(changed);
}

TEST(SqliteUserRepository, DataSurvivesReopeningTheDatabaseFile) {
    TempDb db;
    int id = 0;
    {
        SQLiteUserRepository repo(db.path);
        ASSERT_TRUE(repo.isOpen()) << repo.error();
        ASSERT_TRUE(repo.createUser(makeUser("persisted_user"), id));
    }
    SQLiteUserRepository reopened(db.path);
    ASSERT_TRUE(reopened.isOpen()) << reopened.error();
    User user;
    bool found = false;
    ASSERT_TRUE(reopened.findUserByUsername("persisted_user", user, found));
    EXPECT_TRUE(found);
    EXPECT_EQ(user.id, id);
}

TEST(SqliteUserRepository, RefusesADatabaseFromAnOlderSchemaWithAClearMessage) {
    TempDb db;
    {
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open(db.path.c_str(), &raw), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(raw,
                               "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE,"
                               " password_hash TEXT NOT NULL, role TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 1);",
                               nullptr, nullptr, nullptr),
                  SQLITE_OK);
        sqlite3_close(raw);
    }
    SQLiteUserRepository repo(db.path);
    EXPECT_FALSE(repo.isOpen());
    EXPECT_NE(repo.error().find("older QuizSphere build"), std::string::npos);
}

TEST(SqliteUserRepository, FailsCleanlyWhenThePathCannotBeOpened) {
    SQLiteUserRepository repo("/nonexistent-directory-for-quizsphere/users.db");
    EXPECT_FALSE(repo.isOpen());
    EXPECT_FALSE(repo.error().empty());
    User user;
    bool found = false;
    EXPECT_FALSE(repo.findUserByUsername("anyone", user, found));
}

// Registration -> login through the real SQLite repository (BL-03 / BL-11 integration).
TEST(SqliteUserRepository, RegistrationAndLoginWorkEndToEnd) {
    TempDb db;
    SQLiteUserRepository repo(db.path);
    ASSERT_TRUE(repo.isOpen()) << repo.error();

    const AuthResult registered = registerUser(repo, "aruu_123", "CorrectHorse9!", Role::Student);
    ASSERT_TRUE(registered.ok());

    User stored;
    bool found = false;
    ASSERT_TRUE(repo.findUserByUsername("aruu_123", stored, found));
    ASSERT_TRUE(found);
    EXPECT_EQ(stored.password_hash.find("CorrectHorse9!"), std::string::npos);
    EXPECT_EQ(stored.password_hash.rfind("pbkdf2-sha256$600000$", 0), 0u);

    EXPECT_EQ(registerUser(repo, "ARUU_123", "AnotherPass9!", Role::Student).status, AuthStatus::UsernameAlreadyExists);
    EXPECT_TRUE(loginUser(repo, "aruu_123", "CorrectHorse9!").ok());
    EXPECT_EQ(loginUser(repo, "aruu_123", "wrong-password").status, AuthStatus::InvalidCredentials);

    // Lockout state is persisted by the repository, not just in memory.
    LoginOptions options;
    options.max_failures = 2;
    options.now = [] { return std::int64_t{10000}; };
    loginUser(repo, "aruu_123", "bad-pass-1", options);
    loginUser(repo, "aruu_123", "bad-pass-2", options);
    EXPECT_EQ(loginUser(repo, "aruu_123", "CorrectHorse9!", options).status, AuthStatus::AccountLocked);
}

TEST(SqliteUserRepository, AdminLifecycleThroughTheRealRepository) {
    SQLiteUserRepository repo(":memory:");
    ASSERT_TRUE(repo.isOpen());
    const AuthResult admin = bootstrapAdmin(repo, "root_admin", "CorrectHorse9!");
    ASSERT_TRUE(admin.ok());
    EXPECT_EQ(bootstrapAdmin(repo, "second_admin", "CorrectHorse9!").status, AuthStatus::AdminAlreadyExists);

    const AuthResult student = registerUser(repo, "aruu_123", "CorrectHorse9!", Role::Student);
    ASSERT_TRUE(student.ok());
    const Session actor = makeSession(admin, 1700000000);
    const int student_id = std::stoi(student.user_id);

    EXPECT_EQ(setAccountEnabled(repo, actor, student_id, false), AuthStatus::Success);
    EXPECT_EQ(loginUser(repo, "aruu_123", "CorrectHorse9!").status, AuthStatus::AccountDisabled);
    EXPECT_EQ(setUserRole(repo, actor, student_id, Role::Instructor), AuthStatus::Success);
    EXPECT_EQ(removeUser(repo, actor, student_id), AuthStatus::Success);
    EXPECT_EQ(removeUser(repo, actor, student_id), AuthStatus::NotFound);
}
