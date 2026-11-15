#include "admin_user_menu.hpp"

#include <string>

#include "console_input.hpp"

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
                    << "2. Create a user\n"
                    << "0. Back\n";
            std::string choice;
            if (!prompt("Select: ", choice) || choice == "0") return;
            if (choice == "1") {
                User user;
                findTarget(user);
            } else if (choice == "2") {
                createAccount();
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

    bool promptRole(Role& role) {
        std::string choice;
        if (!prompt("Role: 1. Student  2. Instructor  3. Admin (0 to cancel): ", choice) ||
            choice.empty() || choice == "0") return false;
        if (choice == "1") role = Role::Student;
        else if (choice == "2") role = Role::Instructor;
        else if (choice == "3") role = Role::Admin;
        else {
            output_ << "Invalid role. Choose 1, 2 or 3.\n";
            return false;
        }
        return true;
    }

    bool confirm(const std::string& action) {
        std::string answer;
        if (!prompt("Type YES to " + action + ": ", answer)) return false;
        if (answer == "YES") return true;
        output_ << "Cancelled.\n";
        return false;
    }

    void createAccount() {
        std::string username;
        if (!prompt("New username (blank to cancel): ", username) || username.empty()) return;
        if (!isValidUsername(username)) {
            output_ << "Invalid username. Use 3-32 letters, digits, '_', '-' or '.'.\n";
            return;
        }
        Role role;
        if (!promptRole(role)) return;
        std::string password;
        std::string repeated;
        if (!readConsolePassword(input_, output_, "New password: ", password) ||
            !readConsolePassword(input_, output_, "Repeat password: ", repeated)) {
            running_ = false;
            return;
        }
        if (password != repeated) {
            output_ << "Passwords did not match.\n";
            return;
        }
        if (!isValidPassword(password)) {
            output_ << "Password must contain 8-1024 characters.\n";
            return;
        }
        if (!confirm("create " + username + " as " + roleToString(role))) return;
        const AuthResult result = createUserAsAdmin(users_, session_, username, password, role);
        if (result.ok()) output_ << "Account created: " << username << " (" << roleToString(role) << ").\n";
        else output_ << statusMessage(result.status) << '\n';
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
