#include "admin_user_menu.hpp"

#include <filesystem>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "sqlite_user_repository.hpp"

namespace quizsphere {
namespace {

constexpr char kPassword[] = "AdminMenuTest9!";

class AdminUserMenuTest : public ::testing::Test {
protected:
    SQLiteUserRepository users_{":memory:"};
    Session admin_;

    void SetUp() override {
        ASSERT_TRUE(users_.isOpen());
        const AuthResult created = bootstrapAdmin(users_, "root_admin", kPassword);
        ASSERT_TRUE(created.ok());
        admin_ = makeSession(created, 1700000000);
        ASSERT_TRUE(registerUser(users_, "learner", kPassword, Role::Student).ok());
        ASSERT_TRUE(registerUser(users_, "teacher", kPassword, Role::Instructor).ok());
    }

    std::string run(const std::string& commands, const Session& actor) {
        std::istringstream input(commands);
        std::ostringstream output;
        runAdminUserMenu(actor, users_, input, output);
        return output.str();
    }

    std::string run(const std::string& commands) { return run(commands, admin_); }

    User user(const std::string& username) {
        User result;
        bool found = false;
        EXPECT_TRUE(users_.findUserByUsername(username, result, found));
        EXPECT_TRUE(found);
        return result;
    }
};

TEST_F(AdminUserMenuTest, FindsAccountsWithoutShowingPasswordHashes) {
    const std::string text = run("1\nLEARNER\n0\n");
    EXPECT_NE(text.find("User: learner | Role: Student | Status: Enabled"), std::string::npos);
    EXPECT_EQ(text.find(kPassword), std::string::npos);
    EXPECT_EQ(text.find(user("learner").password_hash), std::string::npos);
}

TEST_F(AdminUserMenuTest, StudentsInstructorsAndForgedAdminSessionsCannotEnter) {
    for (const std::string username : {"learner", "teacher"}) {
        const User account = user(username);
        for (const Role claimed_role : {account.role, Role::Admin}) {
            const Session actor{std::to_string(account.id), claimed_role, ""};
            const std::string text = run("1\nroot_admin\n0\n", actor);
            EXPECT_NE(text.find("Permission denied"), std::string::npos);
            EXPECT_EQ(text.find("=== Manage users ==="), std::string::npos);
            EXPECT_EQ(text.find("User:"), std::string::npos);
        }
    }
}

TEST_F(AdminUserMenuTest, DisabledDemotedDeletedAndMalformedAdminSessionsCannotEnter) {
    EXPECT_NE(run("0\n", {"bad-id", Role::Admin, ""}).find("Permission denied"), std::string::npos);
    bool updated = false;
    const int id = user("root_admin").id;
    ASSERT_TRUE(users_.updateUserStatus(id, false, updated));
    EXPECT_NE(run("0\n").find("Permission denied"), std::string::npos);
    ASSERT_TRUE(users_.updateUserStatus(id, true, updated));
    ASSERT_TRUE(users_.updateUserRole(id, Role::Student, updated));
    EXPECT_NE(run("0\n").find("Permission denied"), std::string::npos);
    ASSERT_TRUE(users_.deleteUser(id, updated));
    EXPECT_NE(run("0\n").find("Permission denied"), std::string::npos);
}

TEST_F(AdminUserMenuTest, InvalidMissingAndCancelledLookupReturnToMenuAndEofExits) {
    const std::string text = run("invalid\n1\nx\n1\nmissing_user\n1\n\n0\n");
    EXPECT_NE(text.find("Invalid option."), std::string::npos);
    EXPECT_NE(text.find("Invalid username."), std::string::npos);
    EXPECT_NE(text.find("No such user."), std::string::npos);
    EXPECT_NE(run("").find("=== Manage users ==="), std::string::npos);
}

TEST(AdminUserMenuStorage, FailedActorLookupExitsWithoutOpeningTheMenu) {
    // A directory cannot be opened as an SQLite database file.
    SQLiteUserRepository closed(std::filesystem::temp_directory_path().string());
    EXPECT_FALSE(closed.isOpen());
    std::istringstream input("0\n");
    std::ostringstream output;
    runAdminUserMenu({"1", Role::Admin, ""}, closed, input, output);
    EXPECT_NE(output.str().find("Storage error"), std::string::npos);
    EXPECT_EQ(output.str().find("=== Manage users ==="), std::string::npos);
}


TEST_F(AdminUserMenuTest, CreatesEveryRoleWithoutEchoingPasswords) {
    int choice = 0;
    for (const Role role : {Role::Student, Role::Instructor, Role::Admin}) {
        ++choice;
        const std::string username = "created_" + std::to_string(choice);
        const std::string text = run("2\n" + username + "\n" + std::to_string(choice) +
                                    "\n" + kPassword + "\n" + kPassword + "\nYES\n0\n");
        const AuthResult login = loginUser(users_, username, kPassword);
        ASSERT_TRUE(login.ok());
        EXPECT_EQ(login.role, role);
        EXPECT_NE(text.find("Account created: " + username), std::string::npos);
        EXPECT_EQ(text.find(kPassword), std::string::npos);
        EXPECT_NE(user(username).password_hash, kPassword);
    }
}

TEST_F(AdminUserMenuTest, CreationRejectsBadNamesRolesPasswordsAndDuplicates) {
    EXPECT_NE(run("2\nx\n0\n").find("Invalid username."), std::string::npos);
    EXPECT_NE(run("2\nnew_user\ninvalid\n0\n").find("Invalid role."), std::string::npos);
    EXPECT_NE(run("2\nnew_user\n1\nshort\nshort\n0\n").find("8-1024"), std::string::npos);
    EXPECT_NE(run("2\nnew_user\n1\nfirstsecret\ndifferentsecret\n0\n").find("did not match"), std::string::npos);
    const std::string duplicate = run(std::string("2\nLEARNER\n3\n") + kPassword + "\n" + kPassword + "\nYES\n0\n");
    EXPECT_NE(duplicate.find("already taken"), std::string::npos);
    EXPECT_EQ(user("learner").role, Role::Student);
    bool exists = true;
    ASSERT_TRUE(users_.usernameExists("new_user", exists));
    EXPECT_FALSE(exists);
}

TEST_F(AdminUserMenuTest, CreationRequiresConfirmationAndStopsOnEndOfInput) {
    const std::string commands = std::string("2\nnew_user\n1\n") + kPassword + "\n" + kPassword + "\nNO\n0\n";
    EXPECT_NE(run(commands).find("Cancelled."), std::string::npos);
    run("2\nnew_user\n0\n0\n");
    run("2\nnew_user\n1\n");
    bool exists = true;
    ASSERT_TRUE(users_.usernameExists("new_user", exists));
    EXPECT_FALSE(exists);
}


TEST_F(AdminUserMenuTest, RoleChangesAreConfirmedAndAffectTheNextLogin) {
    const std::string text = run("3\nlearner\n2\nYES\n0\n");
    EXPECT_NE(text.find("change learner from Student to Instructor"), std::string::npos);
    EXPECT_NE(text.find("Role updated: learner -> Instructor"), std::string::npos);
    const AuthResult login = loginUser(users_, "learner", kPassword);
    ASSERT_TRUE(login.ok());
    EXPECT_EQ(login.role, Role::Instructor);
    EXPECT_NE(run("3\nlearner\n3\nYES\n0\n").find("Role updated"), std::string::npos);
    EXPECT_EQ(user("learner").role, Role::Admin);
}

TEST_F(AdminUserMenuTest, RoleChangeCancellationInvalidRoleMissingUserAndSelfProtection) {
    EXPECT_NE(run("3\nlearner\n3\nNO\n0\n").find("Cancelled."), std::string::npos);
    EXPECT_NE(run("3\nlearner\ninvalid\n0\n").find("Invalid role."), std::string::npos);
    EXPECT_NE(run("3\nmissing_user\n0\n").find("No such user."), std::string::npos);
    run("3\nlearner\n2\n");
    EXPECT_EQ(user("learner").role, Role::Student);
    EXPECT_NE(run("3\nroot_admin\n1\nYES\n0\n").find("Permission denied"), std::string::npos);
    EXPECT_EQ(user("root_admin").role, Role::Admin);
}


TEST_F(AdminUserMenuTest, DisablingBlocksLoginAndEnablingRestoresAccess) {
    EXPECT_NE(run("4\nlearner\n2\nYES\n0\n").find("Account disabled: learner"), std::string::npos);
    EXPECT_FALSE(user("learner").enabled);
    EXPECT_EQ(loginUser(users_, "learner", kPassword).status, AuthStatus::AccountDisabled);
    EXPECT_NE(run("4\nlearner\n1\nYES\n0\n").find("Account enabled: learner"), std::string::npos);
    EXPECT_TRUE(loginUser(users_, "learner", kPassword).ok());
}

TEST_F(AdminUserMenuTest, StatusChangeNeedsValidInputAndConfirmationAndCannotDisableSelf) {
    EXPECT_NE(run("4\nlearner\n2\nno\n0\n").find("Cancelled."), std::string::npos);
    EXPECT_NE(run("4\nlearner\ninvalid\n0\n").find("Invalid status."), std::string::npos);
    run("4\nlearner\n0\n0\n");
    run("4\nlearner\n2\n");
    EXPECT_TRUE(user("learner").enabled);
    EXPECT_NE(run("4\nroot_admin\n2\nYES\n0\n").find("Permission denied"), std::string::npos);
    EXPECT_TRUE(user("root_admin").enabled);
}


TEST_F(AdminUserMenuTest, RemovalRequiresExplicitConfirmationAndDeletesOnlyTheChosenAccount) {
    const std::string text = run("5\nlearner\nYES\n0\n");
    EXPECT_NE(text.find("permanently remove learner"), std::string::npos);
    EXPECT_NE(text.find("Account removed: learner"), std::string::npos);
    bool exists = true;
    ASSERT_TRUE(users_.usernameExists("learner", exists));
    EXPECT_FALSE(exists);
    EXPECT_EQ(loginUser(users_, "learner", kPassword).status, AuthStatus::InvalidCredentials);
    EXPECT_EQ(user("teacher").role, Role::Instructor);
    EXPECT_TRUE(loginUser(users_, "root_admin", kPassword).ok());
}

TEST_F(AdminUserMenuTest, RemovalCancellationEofMissingUserAndSelfRemovalLeaveAccountsSafe) {
    EXPECT_NE(run("5\nlearner\nyes\n0\n").find("Cancelled."), std::string::npos);
    run("5\nlearner\n");
    EXPECT_TRUE(user("learner").enabled);
    EXPECT_NE(run("5\nmissing_user\n0\n").find("No such user."), std::string::npos);
    EXPECT_NE(run("5\nroot_admin\nYES\n0\n").find("Permission denied"), std::string::npos);
    EXPECT_TRUE(loginUser(users_, "root_admin", kPassword).ok());
}

}  // namespace
}  // namespace quizsphere
