#pragma once

#include <cstdint>
#include <string>

#include "common_types.hpp"

namespace quizsphere {

struct User {
    int id{0};
    std::string username;
    std::string password_hash;
    Role role{Role::Student};
    bool enabled{true};
    int failed_attempts{0};            // consecutive failed logins since the last success/lock
    std::int64_t locked_until{0};      // unix seconds; 0 = not locked
};

// Storage boundary for accounts (SAD: only the data access layer touches storage).
// Every method returns false on a storage failure, true otherwise.
class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    virtual bool usernameExists(const std::string& username, bool& exists) = 0;
    virtual bool createUser(const User& user, int& new_user_id) = 0;
    virtual bool findUserByUsername(const std::string& username, User& user, bool& found) = 0;
    virtual bool findUserById(int user_id, User& user, bool& found) = 0;
    virtual bool updateUserStatus(int user_id, bool enabled, bool& updated) = 0;
    virtual bool updateUserRole(int user_id, Role role, bool& updated) = 0;
    virtual bool deleteUser(int user_id, bool& deleted) = 0;
    virtual bool setLoginFailureState(int user_id, int failed_attempts, std::int64_t locked_until) = 0;
    virtual bool countAdmins(int& count) = 0;
};

}  // namespace quizsphere
