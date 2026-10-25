#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "auth.hpp"
#include "console_menu.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "sqlite_user_repository.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

using quizsphere::AuthResult;
using quizsphere::AuthStatus;

std::int64_t unixNow() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string envOr(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : std::string(fallback);
}

bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    return static_cast<bool>(std::getline(std::cin, out));
}

// Reads a password without echoing it when stdin is a terminal; falls back to a plain read otherwise (pipes, tests).
bool readPassword(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool is_console = handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0;
    if (is_console) {
        SetConsoleMode(handle, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    }
    const bool ok = static_cast<bool>(std::getline(std::cin, out));
    if (is_console) {
        SetConsoleMode(handle, mode);
        std::cout << '\n';
    }
    return ok;
#else
    termios original{};
    const bool is_tty = isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &original) == 0;
    if (is_tty) {
        termios silent = original;
        silent.c_lflag &= static_cast<tcflag_t>(~ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &silent);
    }
    const bool ok = static_cast<bool>(std::getline(std::cin, out));
    if (is_tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
        std::cout << '\n';
    }
    return ok;
#endif
}

const char* describe(AuthStatus status) {
    switch (status) {
        case AuthStatus::Success: return "OK.";
        case AuthStatus::InvalidInput:
            return "Invalid input: username must be 3-32 letters, digits, '_', '-' or '.', and the password 8-1024 characters.";
        case AuthStatus::UsernameAlreadyExists: return "That username is already taken.";
        case AuthStatus::InvalidCredentials: return "Invalid username or password.";
        case AuthStatus::AccountDisabled: return "This account has been disabled. Contact an administrator.";
        case AuthStatus::AccountLocked: return "Too many failed attempts. This account is temporarily locked; try again later.";
        case AuthStatus::PermissionDenied: return "You do not have permission to do that.";
        case AuthStatus::NotFound: return "No such user.";
        case AuthStatus::AdminAlreadyExists: return "An administrator already exists.";
        case AuthStatus::StorageError: return "Storage error: could not read or write the user database.";
    }
    return "Unknown error.";
}

bool firstRunAdminSetup(quizsphere::IUserRepository& users) {
    std::cout << "\nFirst-run setup: no administrator account exists yet. Create one now.\n";
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string username;
        std::string password;
        std::string confirm;
        if (!readLine("Admin username: ", username) || !readPassword("Admin password: ", password) ||
            !readPassword("Repeat password: ", confirm)) {
            return false;
        }
        if (password != confirm) {
            std::cout << "Passwords did not match.\n";
            continue;
        }
        const AuthResult result = quizsphere::bootstrapAdmin(users, username, password);
        std::cout << describe(result.status) << '\n';
        if (result.ok()) {
            return true;
        }
        if (result.status == AuthStatus::StorageError) {
            return false;
        }
    }
    return false;
}

void doRegister(quizsphere::IUserRepository& users) {
    std::string username;
    std::string role_choice;
    std::string password;
    std::string confirm;
    if (!readLine("Choose a username: ", username) || !readLine("Role - (s)tudent or (i)nstructor: ", role_choice) ||
        !readPassword("Choose a password: ", password) || !readPassword("Repeat password: ", confirm)) {
        return;
    }
    if (password != confirm) {
        std::cout << "Passwords did not match.\n";
        return;
    }
    const quizsphere::Role role = (!role_choice.empty() && (role_choice[0] == 'i' || role_choice[0] == 'I'))
                                      ? quizsphere::Role::Instructor
                                      : quizsphere::Role::Student;
    const AuthResult result = quizsphere::registerUser(users, username, password, role);
    std::cout << (result.ok() ? "Account created. You can now log in." : describe(result.status)) << '\n';
}

void runSession(const quizsphere::Session& session, const std::string& username, quizsphere::QuestionBank& bank) {
    if (session.role == quizsphere::Role::Student) {
        std::cout << "Welcome, " << username << " (Student). Quiz attempts are not available in this build yet.\n";
        return;
    }
    auto menu = quizsphere::createConsoleMenu(session, username, bank, std::cin, std::cout);
    menu->run();
}

}  // namespace

int main() {
    auto repository = quizsphere::createFileQuestionRepository();
    auto question_bank = quizsphere::createQuestionBank(std::move(repository));

    const auto load_result = question_bank->load();
    if (!load_result.ok()) {
        std::cerr << "Startup failure: " << load_result.message << '\n';
        return 1;
    }

#if defined(QUIZSPHERE_DEMO_SESSION_ENABLED)
    std::cerr << "WARNING: QUIZSPHERE_DEMO_SESSION is ON. Demo Instructor session enabled for local development only.\n";
    const auto session = quizsphere::createDemoSession();
    if (!session.has_value()) {
        std::cerr << "Startup failure: unable to create demo session.\n";
        return 1;
    }
    auto menu = quizsphere::createConsoleMenu(*session, "Demo Instructor", *question_bank, std::cin, std::cout);
    menu->run();
    return 0;
#else
    const std::string db_path = envOr("QUIZSPHERE_DB", "quizsphere.db");
    quizsphere::SQLiteUserRepository users(db_path);
    if (!users.isOpen()) {
        std::cerr << "Startup failure: cannot open user database '" << db_path << "': " << users.error() << '\n';
        return 1;
    }
    auto auth_log = quizsphere::createFileAuthEventLog(envOr("QUIZSPHERE_AUTH_LOG", "quizsphere_auth.log"));

    int admins = 0;
    if (!users.countAdmins(admins)) {
        std::cerr << "Startup failure: cannot read the user database.\n";
        return 1;
    }
    if (admins == 0 && !firstRunAdminSetup(users)) {
        std::cerr << "Startup failure: an administrator account is required.\n";
        return 1;
    }

    quizsphere::LoginOptions options;
    options.log = auth_log.get();

    for (;;) {
        std::cout << "\n=== QuizSphere ===\n1) Log in\n2) Register\n0) Exit\n";
        std::string choice;
        if (!readLine("Select: ", choice) || choice == "0") {
            return 0;
        }
        if (choice == "2") {
            doRegister(users);
        } else if (choice == "1") {
            std::string username;
            std::string password;
            if (!readLine("Username: ", username) || !readPassword("Password: ", password)) {
                return 0;
            }
            const AuthResult login = quizsphere::loginUser(users, username, password, options);
            if (!login.ok()) {
                std::cout << describe(login.status) << '\n';
                continue;
            }
            runSession(quizsphere::makeSession(login, unixNow()), username, *question_bank);
        } else {
            std::cout << "Invalid option.\n";
        }
    }
#endif
}
