#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "auth.hpp"
#include "password_hasher.hpp"
#include "user_repository.hpp"

using namespace quizsphere;

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// In-memory stand-in for the SQLite repository. Usernames are case-insensitive like the real schema.
class FakeRepository final : public IUserRepository {
public:
    bool usernameExists(const std::string& username, bool& exists) override {
        if (fail) return false;
        exists = by_name.count(lower(username)) > 0;
        return true;
    }
    bool createUser(const User& user, int& new_user_id) override {
        if (fail || by_name.count(lower(user.username)) > 0) return false;
        User copy = user;
        copy.id = next_id++;
        new_user_id = copy.id;
        by_name[lower(copy.username)] = copy;
        return true;
    }
    bool findUserByUsername(const std::string& username, User& user, bool& found) override {
        found = false;
        if (fail) return false;
        const auto it = by_name.find(lower(username));
        if (it != by_name.end()) {
            user = it->second;
            found = true;
        }
        return true;
    }
    bool findUserById(int user_id, User& user, bool& found) override {
        found = false;
        if (fail) return false;
        for (const auto& entry : by_name) {
            if (entry.second.id == user_id) {
                user = entry.second;
                found = true;
            }
        }
        return true;
    }
    bool updateUserStatus(int user_id, bool enabled, bool& updated) override {
        updated = false;
        if (fail) return false;
        User* user = find(user_id);
        if (user != nullptr) {
            user->enabled = enabled;
            updated = true;
        }
        return true;
    }
    bool updateUserRole(int user_id, Role role, bool& updated) override {
        updated = false;
        if (fail) return false;
        User* user = find(user_id);
        if (user != nullptr) {
            user->role = role;
            updated = true;
        }
        return true;
    }
    bool deleteUser(int user_id, bool& deleted) override {
        deleted = false;
        if (fail) return false;
        for (auto it = by_name.begin(); it != by_name.end(); ++it) {
            if (it->second.id == user_id) {
                by_name.erase(it);
                deleted = true;
                break;
            }
        }
        return true;
    }
    bool setLoginFailureState(int user_id, int failed_attempts, std::int64_t locked_until) override {
        if (fail) return false;
        User* user = find(user_id);
        if (user == nullptr) return false;
        user->failed_attempts = failed_attempts;
        user->locked_until = locked_until;
        return true;
    }
    bool countAdmins(int& count) override {
        count = 0;
        if (fail) return false;
        for (const auto& entry : by_name) {
            if (entry.second.role == Role::Admin) ++count;
        }
        return true;
    }

    // Test helper: add an account with a cheap (low-iteration) hash so tests stay fast.
    int addUser(const std::string& username, const std::string& password, Role role, bool enabled = true) {
        User user;
        user.username = username;
        user.role = role;
        user.enabled = enabled;
        EXPECT_TRUE(hashPassword(password, user.password_hash, kMinPbkdf2Iterations));
        int id = 0;
        EXPECT_TRUE(createUser(user, id));
        return id;
    }

    User* find(int user_id) {
        for (auto& entry : by_name) {
            if (entry.second.id == user_id) return &entry.second;
        }
        return nullptr;
    }

    std::map<std::string, User> by_name;
    int next_id{1};
    bool fail{false};
};

struct LogEntry {
    std::int64_t time;
    std::string username;
    std::string reason;
};

class FakeLog final : public IAuthEventLog {
public:
    void logFailedLogin(std::int64_t unix_time, const std::string& username, const std::string& reason) override {
        entries.push_back({unix_time, username, reason});
    }
    std::vector<LogEntry> entries;
};

Session adminSession(int id) {
    Session session;
    session.user_id = std::to_string(id);
    session.role = Role::Admin;
    return session;
}

constexpr char kPassword[] = "CorrectHorse9!";

}  // namespace

// ---------- Registration (FR-001) ----------

TEST(Auth, UT_AUTH_02_RegistersStudentAndInstructor) {
    FakeRepository repo;
    const AuthResult student = registerUser(repo, "aruu_123", kPassword, Role::Student);
    ASSERT_TRUE(student.ok());
    EXPECT_EQ(student.role, Role::Student);
    EXPECT_EQ(student.user_id, "1");

    const AuthResult instructor = registerUser(repo, "prof.sapna", kPassword, Role::Instructor);
    ASSERT_TRUE(instructor.ok());
    EXPECT_EQ(instructor.role, Role::Instructor);
}

TEST(Auth, UT_AUTH_03_DuplicateUsernameIsRejectedCaseInsensitively) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student);
    EXPECT_EQ(registerUser(repo, "aruu_123", kPassword, Role::Student).status, AuthStatus::UsernameAlreadyExists);
    EXPECT_EQ(registerUser(repo, "ARUU_123", kPassword, Role::Student).status, AuthStatus::UsernameAlreadyExists);
}

TEST(Auth, UT_AUTH_09_SelfRegistrationAsAdminIsRejected) {
    FakeRepository repo;
    const AuthResult result = registerUser(repo, "sneaky_admin", kPassword, Role::Admin);
    EXPECT_EQ(result.status, AuthStatus::InvalidInput);
    EXPECT_TRUE(repo.by_name.empty());
}

TEST(Auth, UT_AUTH_11_PasswordPolicy) {
    EXPECT_FALSE(isValidPassword("short"));
    EXPECT_FALSE(isValidPassword("1234567"));
    EXPECT_TRUE(isValidPassword("12345678"));
    EXPECT_TRUE(isValidPassword(std::string(kMaxPasswordLength, 'a')));
    EXPECT_FALSE(isValidPassword(std::string(kMaxPasswordLength + 1, 'a')));

    FakeRepository repo;
    EXPECT_EQ(registerUser(repo, "valid_user", "short", Role::Student).status, AuthStatus::InvalidInput);
    EXPECT_TRUE(repo.by_name.empty());
}

TEST(Auth, UT_AUTH_12_UsernameValidation) {
    for (const char* ok : {"abc", "aruu_123", "A.b-c_9", "Prof.Sapna"}) {
        EXPECT_TRUE(isValidUsername(ok)) << ok;
    }
    EXPECT_TRUE(isValidUsername(std::string(32, 'a')));
    for (const char* bad : {"", "ab", "bad name", "semi;colon", "quo'te", "x' OR '1'='1", "a\nb", "naïve", "tab\tname"}) {
        EXPECT_FALSE(isValidUsername(bad)) << bad;
    }
    EXPECT_FALSE(isValidUsername(std::string(33, 'a')));
}

TEST(Auth, RegistrationNeverStoresThePlaintextPassword) {
    FakeRepository repo;
    ASSERT_TRUE(registerUser(repo, "aruu_123", kPassword, Role::Student).ok());
    const std::string& stored = repo.by_name.at("aruu_123").password_hash;
    EXPECT_NE(stored, kPassword);
    EXPECT_EQ(stored.find(kPassword), std::string::npos);
    EXPECT_EQ(stored.rfind("pbkdf2-sha256$", 0), 0u);
}

// ---------- Login (FR-002, FR-022) ----------

TEST(Auth, UT_AUTH_01_ValidLoginReturnsIdAndRole) {
    FakeRepository repo;
    const int id = repo.addUser("aruu_123", kPassword, Role::Instructor);
    const AuthResult login = loginUser(repo, "aruu_123", kPassword);
    ASSERT_TRUE(login.ok());
    EXPECT_EQ(login.user_id, std::to_string(id));
    EXPECT_EQ(login.role, Role::Instructor);
    EXPECT_TRUE(loginUser(repo, "ARUU_123", kPassword).ok());  // case-insensitive username
}

TEST(Auth, UT_AUTH_04_WrongPasswordAndUnknownUserBothReturnInvalidCredentials) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student);
    EXPECT_EQ(loginUser(repo, "aruu_123", "wrong-password").status, AuthStatus::InvalidCredentials);
    EXPECT_EQ(loginUser(repo, "nobody_here", kPassword).status, AuthStatus::InvalidCredentials);
    EXPECT_EQ(loginUser(repo, "bad name!", kPassword).status, AuthStatus::InvalidCredentials);
    EXPECT_EQ(loginUser(repo, "aruu_123", "").status, AuthStatus::InvalidCredentials);
}

TEST(Auth, UT_AUTH_06_DisabledAccountIsRefusedOnlyAfterTheCorrectPassword) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student, /*enabled=*/false);
    EXPECT_EQ(loginUser(repo, "aruu_123", kPassword).status, AuthStatus::AccountDisabled);
    // A wrong password must not reveal that the account exists but is disabled.
    EXPECT_EQ(loginUser(repo, "aruu_123", "wrong-password").status, AuthStatus::InvalidCredentials);
}

TEST(Auth, UT_AUTH_15_StorageFailuresAreReportedNotHidden) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student);
    repo.fail = true;
    EXPECT_EQ(registerUser(repo, "new_user", kPassword, Role::Student).status, AuthStatus::StorageError);
    EXPECT_EQ(loginUser(repo, "aruu_123", kPassword).status, AuthStatus::StorageError);
    EXPECT_EQ(bootstrapAdmin(repo, "root_admin", kPassword).status, AuthStatus::StorageError);
}

// ---------- Lockout and logging (SEC-006) ----------

TEST(Auth, UT_AUTH_13_AccountLocksAfterRepeatedFailuresAndUnlocksLater) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student);

    std::int64_t clock = 1000;
    LoginOptions options;
    options.now = [&clock] { return clock; };
    options.max_failures = 3;
    options.lock_seconds = 60;

    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(loginUser(repo, "aruu_123", "wrong-password", options).status, AuthStatus::InvalidCredentials);
    }
    // Locked: even the right password is refused.
    EXPECT_EQ(loginUser(repo, "aruu_123", kPassword, options).status, AuthStatus::AccountLocked);

    clock += 59;
    EXPECT_EQ(loginUser(repo, "aruu_123", kPassword, options).status, AuthStatus::AccountLocked);

    clock += 2;  // lock expired
    const AuthResult after = loginUser(repo, "aruu_123", kPassword, options);
    EXPECT_TRUE(after.ok());
    EXPECT_EQ(repo.by_name.at("aruu_123").failed_attempts, 0);
    EXPECT_EQ(repo.by_name.at("aruu_123").locked_until, 0);
}

TEST(Auth, UT_AUTH_14_SuccessfulLoginResetsTheFailureCounter) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student);
    LoginOptions options;
    options.now = [] { return std::int64_t{5000}; };
    options.max_failures = 3;

    EXPECT_EQ(loginUser(repo, "aruu_123", "bad-pass-1", options).status, AuthStatus::InvalidCredentials);
    EXPECT_EQ(loginUser(repo, "aruu_123", "bad-pass-2", options).status, AuthStatus::InvalidCredentials);
    ASSERT_TRUE(loginUser(repo, "aruu_123", kPassword, options).ok());
    EXPECT_EQ(loginUser(repo, "aruu_123", "bad-pass-3", options).status, AuthStatus::InvalidCredentials);
    EXPECT_EQ(loginUser(repo, "aruu_123", "bad-pass-4", options).status, AuthStatus::InvalidCredentials);
    EXPECT_TRUE(loginUser(repo, "aruu_123", kPassword, options).ok());  // never reached 3 in a row
}

TEST(Auth, UT_AUTH_16_FailedLoginsAreLoggedWithTimestampButNeverThePassword) {
    FakeRepository repo;
    repo.addUser("aruu_123", kPassword, Role::Student);
    repo.addUser("old_user", kPassword, Role::Student, /*enabled=*/false);
    FakeLog log;
    LoginOptions options;
    options.log = &log;
    options.now = [] { return std::int64_t{1700000000}; };

    loginUser(repo, "aruu_123", "my-wrong-password", options);
    loginUser(repo, "ghost_user", "another-secret-pw", options);
    loginUser(repo, "old_user", kPassword, options);
    // Someone types their password into the username prompt:
    loginUser(repo, "hunter2 hunter2", "whatever", options);
    loginUser(repo, "aruu_123", kPassword, options);  // success: nothing logged

    ASSERT_EQ(log.entries.size(), 4u);
    EXPECT_EQ(log.entries[0].reason, "wrong_password");
    EXPECT_EQ(log.entries[0].username, "aruu_123");
    EXPECT_EQ(log.entries[0].time, 1700000000);
    EXPECT_EQ(log.entries[1].reason, "unknown_user");
    EXPECT_EQ(log.entries[2].reason, "disabled_account");
    EXPECT_EQ(log.entries[3].username, "<invalid>");
    for (const LogEntry& entry : log.entries) {
        EXPECT_EQ(entry.reason.find("secret"), std::string::npos);
        EXPECT_EQ(entry.username.find("hunter2"), std::string::npos);
    }
}

// ---------- Admin bootstrap and admin-only operations (SEC-003) ----------

TEST(Auth, UT_AUTH_10_FirstAdminCanBeBootstrappedExactlyOnce) {
    FakeRepository repo;
    const AuthResult first = bootstrapAdmin(repo, "root_admin", kPassword);
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(first.role, Role::Admin);
    EXPECT_EQ(bootstrapAdmin(repo, "second_admin", kPassword).status, AuthStatus::AdminAlreadyExists);
    EXPECT_EQ(bootstrapAdmin(repo, "x", kPassword).status, AuthStatus::InvalidInput);

    const AuthResult login = loginUser(repo, "root_admin", kPassword);
    ASSERT_TRUE(login.ok());
    EXPECT_EQ(login.role, Role::Admin);
}

TEST(Auth, UT_AUTH_07_AdminFunctionsDenyNonAdminsEvenWhenCalledDirectly) {
    FakeRepository repo;
    const int admin = repo.addUser("root_admin", kPassword, Role::Admin);
    const int student = repo.addUser("aruu_123", kPassword, Role::Student);
    const int victim = repo.addUser("victim_1", kPassword, Role::Student);

    Session student_session;
    student_session.user_id = std::to_string(student);
    student_session.role = Role::Student;
    EXPECT_EQ(setAccountEnabled(repo, student_session, victim, false), AuthStatus::PermissionDenied);
    EXPECT_EQ(setUserRole(repo, student_session, victim, Role::Admin), AuthStatus::PermissionDenied);
    EXPECT_EQ(removeUser(repo, student_session, victim), AuthStatus::PermissionDenied);

    // A forged session that claims Admin for a user who is only a Student in storage.
    Session forged = adminSession(student);
    EXPECT_EQ(setAccountEnabled(repo, forged, victim, false), AuthStatus::PermissionDenied);
    // Malformed / unknown actor ids.
    Session malformed;
    malformed.user_id = "1; DROP TABLE users";
    malformed.role = Role::Admin;
    EXPECT_EQ(removeUser(repo, malformed, victim), AuthStatus::PermissionDenied);
    EXPECT_EQ(removeUser(repo, adminSession(9999), victim), AuthStatus::PermissionDenied);

    // A disabled admin loses admin powers immediately, even with an old session.
    repo.find(admin)->enabled = false;
    EXPECT_EQ(setAccountEnabled(repo, adminSession(admin), victim, false), AuthStatus::PermissionDenied);

    EXPECT_TRUE(repo.find(victim)->enabled);
    EXPECT_EQ(repo.find(victim)->role, Role::Student);
}

TEST(Auth, UT_AUTH_08_AdminManagesUsers) {
    FakeRepository repo;
    const int admin = repo.addUser("root_admin", kPassword, Role::Admin);
    const int target = repo.addUser("aruu_123", kPassword, Role::Student);
    const Session actor = adminSession(admin);

    EXPECT_EQ(setAccountEnabled(repo, actor, target, false), AuthStatus::Success);
    EXPECT_EQ(loginUser(repo, "aruu_123", kPassword).status, AuthStatus::AccountDisabled);
    EXPECT_EQ(setAccountEnabled(repo, actor, target, true), AuthStatus::Success);
    EXPECT_TRUE(loginUser(repo, "aruu_123", kPassword).ok());

    EXPECT_EQ(setUserRole(repo, actor, target, Role::Instructor), AuthStatus::Success);
    EXPECT_EQ(repo.find(target)->role, Role::Instructor);

    EXPECT_EQ(setAccountEnabled(repo, actor, 4242, false), AuthStatus::NotFound);
    EXPECT_EQ(setUserRole(repo, actor, 4242, Role::Student), AuthStatus::NotFound);
    EXPECT_EQ(removeUser(repo, actor, 4242), AuthStatus::NotFound);
    EXPECT_EQ(setAccountEnabled(repo, actor, 0, false), AuthStatus::InvalidInput);
    EXPECT_EQ(removeUser(repo, actor, -3), AuthStatus::InvalidInput);

    EXPECT_EQ(removeUser(repo, actor, target), AuthStatus::Success);
    EXPECT_EQ(loginUser(repo, "aruu_123", kPassword).status, AuthStatus::InvalidCredentials);
}

TEST(Auth, UT_AUTH_08c_AdminCreatesUsersOfAnyRoleAndOthersCannot) {
    FakeRepository repo;
    const int admin = repo.addUser("root_admin", kPassword, Role::Admin);
    const int student = repo.addUser("aruu_123", kPassword, Role::Student);

    const AuthResult instructor = createUserAsAdmin(repo, adminSession(admin), "prof.sapna", kPassword, Role::Instructor);
    ASSERT_TRUE(instructor.ok());
    EXPECT_EQ(instructor.role, Role::Instructor);
    EXPECT_TRUE(createUserAsAdmin(repo, adminSession(admin), "second_admin", kPassword, Role::Admin).ok());

    EXPECT_EQ(createUserAsAdmin(repo, adminSession(admin), "PROF.SAPNA", kPassword, Role::Student).status,
              AuthStatus::UsernameAlreadyExists);
    EXPECT_EQ(createUserAsAdmin(repo, adminSession(admin), "x", kPassword, Role::Student).status, AuthStatus::InvalidInput);
    EXPECT_EQ(createUserAsAdmin(repo, adminSession(admin), "new_user", "short", Role::Student).status,
              AuthStatus::InvalidInput);

    // A Student session (even one that claims Admin) cannot create accounts.
    EXPECT_EQ(createUserAsAdmin(repo, adminSession(student), "evil_admin", kPassword, Role::Admin).status,
              AuthStatus::PermissionDenied);
    EXPECT_EQ(repo.by_name.count("evil_admin"), 0u);
}

TEST(Auth, UT_AUTH_08b_AdminCannotLockThemselvesOut) {
    FakeRepository repo;
    const int admin = repo.addUser("root_admin", kPassword, Role::Admin);
    const Session actor = adminSession(admin);
    EXPECT_EQ(setAccountEnabled(repo, actor, admin, false), AuthStatus::PermissionDenied);
    EXPECT_EQ(setUserRole(repo, actor, admin, Role::Student), AuthStatus::PermissionDenied);
    EXPECT_EQ(removeUser(repo, actor, admin), AuthStatus::PermissionDenied);
    EXPECT_TRUE(loginUser(repo, "root_admin", kPassword).ok());
}

TEST(Auth, UT_AUTH_17_SessionCarriesIdentityRoleAndUtcTimestamp) {
    AuthResult login;
    login.status = AuthStatus::Success;
    login.user_id = "7";
    login.role = Role::Instructor;
    const Session session = makeSession(login, 1700000000);
    EXPECT_EQ(session.user_id, "7");
    EXPECT_EQ(session.role, Role::Instructor);
    EXPECT_EQ(session.logged_in_at, "2023-11-14T22:13:20Z");
}
