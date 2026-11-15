#include "admin_user_menu.hpp"

#include <string>

namespace quizsphere {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

const char* statusMessage(AuthStatus status) {
    switch (status) {
        case AuthStatus::Success: return "Done.";
        case AuthStatus::InvalidInput: return "Invalid account details.";
        case AuthStatus::UsernameAlreadyExists: return "That username is already taken.";
        case AuthStatus::PermissionDenied: return "Permission denied for this account action.";
        case AuthStatus::NotFound: return "No such user.";
        case AuthStatus::StorageError: return "Storage error: account data could not be read or saved.";
        default: return "Account action could not be completed.";
    }
}

class AdminUserMenu {
public:
    AdminUserMenu(const Session& session, IUserRepository& users, std::istream& input, std::ostream& output)
        : session_(session), users_(users), input_(input), output_(output) {}

    void run() {
        while (running_) {
            if (!authorized()) return;
            output_ << "\n=== Manage users ===\n"
                    << "1. Find a user\n"
                    << "0. Back\n";
            std::string choice;
            if (!prompt("Select: ", choice) || choice == "0") return;
            if (choice == "1") {
                User user;
                findTarget(user);
            } else {
                output_ << "Invalid option.\n";
            }
        }
    }

private:
    bool authorized() {
        const AuthStatus status = checkAdminAccess(users_, session_);
        if (status == AuthStatus::Success) return true;
        output_ << statusMessage(status) << '\n';
        running_ = false;
        return false;
    }

    bool prompt(const std::string& label, std::string& value) {
        output_ << label << std::flush;
        if (!std::getline(input_, value)) {
            running_ = false;
            return false;
        }
        value = trim(value);
        return true;
    }

    bool findTarget(User& user) {
        std::string username;
        if (!prompt("Username (blank to cancel): ", username) || username.empty()) return false;
        if (!authorized()) return false;
        if (!isValidUsername(username)) {
            output_ << "Invalid username. Use 3-32 letters, digits, '_', '-' or '.'.\n";
            return false;
        }
        bool found = false;
        if (!users_.findUserByUsername(username, user, found)) {
            output_ << statusMessage(AuthStatus::StorageError) << '\n';
            return false;
        }
        if (!found) {
            output_ << statusMessage(AuthStatus::NotFound) << '\n';
            return false;
        }
        output_ << "User: " << user.username << " | Role: " << roleToString(user.role)
                << " | Status: " << (user.enabled ? "Enabled" : "Disabled") << '\n';
        return true;
    }

    const Session& session_;
    IUserRepository& users_;
    std::istream& input_;
    std::ostream& output_;
    bool running_{true};
};

}  // namespace

void runAdminUserMenu(const Session& session, IUserRepository& users,
                      std::istream& input, std::ostream& output) {
    AdminUserMenu(session, users, input, output).run();
}

}  // namespace quizsphere
