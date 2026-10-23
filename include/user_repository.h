
#ifndef QUIZSPHERE_USER_REPOSITORY_H
#define QUIZSPHERE_USER_REPOSITORY_H

#include "auth.h"

namespace QuizSphere {

class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    virtual bool usernameExists(
        const std::string& username,
        bool& exists
    ) = 0;

    virtual bool createUser(
        const User& user,
        int& newUserId
    ) = 0;

    virtual bool findUserByUsername(
        const std::string& username,
        User& user,
        bool& found
    ) = 0;

    virtual bool updateUserStatus(
        int userId,
        bool enabled
    ) = 0;
};

} // namespace QuizSphere

#endif
