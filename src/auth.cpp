
#include "auth.h"
#include <cctype>

namespace QuizSphere {

// Temporary validation helper.
// Database-backed registration and login will be added after the DAL
// interface is agreed upon.

bool isValidUsername(const std::string& username) {
    for (char ch : username) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            return true;
        }
    }

    return false;
}

bool isValidRole(UserRole role) {
    return role == UserRole::Student ||
           role == UserRole::Instructor;
}

} // namespace QuizSphere
