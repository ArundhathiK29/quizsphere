
#include "sqlite_user_repository.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

int main() {
    using namespace QuizSphere;

    const std::string databasePath =
        "quizsphere_integration_test.db";

    std::remove(databasePath.c_str());

    {
        SQLiteUserRepository repository(databasePath);

        // 1. Database opens and schema initializes.
        assert(repository.isOpen());

        // 2. New username does not already exist.
        bool exists = true;
        assert(repository.usernameExists("aruu_test", exists));
        assert(!exists);

        // 3. Insert a user.
        User user;
        user.username = "aruu_test";
        user.passwordHash = "test_hash_not_a_real_password";
        user.role = UserRole::Student;
        user.enabled = true;

        int newUserId = 0;
        assert(repository.createUser(user, newUserId));
        assert(newUserId > 0);

        // 4. Username now exists.
        assert(repository.usernameExists("aruu_test", exists));
        assert(exists);

        // 5. Duplicate username is rejected.
        int duplicateId = 0;
        assert(!repository.createUser(user, duplicateId));

        // 6. Retrieve the saved user.
        User retrieved;
        bool found = false;

        assert(repository.findUserByUsername(
            "aruu_test", retrieved, found
        ));

        assert(found);
        assert(retrieved.id == newUserId);
        assert(retrieved.username == "aruu_test");
        assert(retrieved.passwordHash == user.passwordHash);
        assert(retrieved.role == UserRole::Student);
        assert(retrieved.enabled);

        // 7. Disable the account.
        assert(repository.updateUserStatus(newUserId, false));

        User disabledUser;
        found = false;

        assert(repository.findUserByUsername(
            "aruu_test", disabledUser, found
        ));

        assert(found);
        assert(!disabledUser.enabled);

        // 8. Re-enable the account.
        assert(repository.updateUserStatus(newUserId, true));

        User enabledUser;
        found = false;

        assert(repository.findUserByUsername(
            "aruu_test", enabledUser, found
        ));

        assert(found);
        assert(enabledUser.enabled);
    }

    // Repository is closed before the test database is removed.
    std::remove(databasePath.c_str());

    std::cout << "All SQLite repository integration tests passed.\n";
    return 0;
}
