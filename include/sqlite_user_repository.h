
#ifndef QUIZSPHERE_SQLITE_USER_REPOSITORY_H
#define QUIZSPHERE_SQLITE_USER_REPOSITORY_H

#include "user_repository.h"
#include <sqlite3.h>

namespace QuizSphere {

class SQLiteUserRepository final : public IUserRepository {
public:
    explicit SQLiteUserRepository(
        const std::string& databasePath
    );

    ~SQLiteUserRepository() override;

    SQLiteUserRepository(const SQLiteUserRepository&) = delete;
    SQLiteUserRepository& operator=(
        const SQLiteUserRepository&
    ) = delete;

    bool isOpen() const;

    bool usernameExists(
        const std::string& username,
        bool& exists
    ) override;

    bool createUser(
        const User& user,
        int& newUserId
    ) override;

    bool findUserByUsername(
        const std::string& username,
        User& user,
        bool& found
    ) override;

    bool updateUserStatus(
        int userId,
        bool enabled
    ) override;

private:
    sqlite3* db_ = nullptr;

    bool initializeSchema();
};

} // namespace QuizSphere

#endif
