#include "auth.hpp"

#include <algorithm>
#include <chrono>
#include <string>

#include "password_hasher.hpp"

namespace quizsphere {
namespace {

std::int64_t systemNow() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t currentTime(const LoginOptions& options) {
    return options.now ? options.now() : systemNow();
}

// Verified against when a username does not exist, so "unknown user" and
// "wrong password" take about the same time (no timing-based user enumeration).
const std::string& dummyHash() {
    static const std::string value = [] {
        std::string encoded;
        hashPassword("quizsphere-timing-equaliser", encoded);
        return encoded;
    }();
    return value;
}

bool parseUserId(const std::string& text, int& id) {
    if (text.empty() || text.size() > 9) {
        return false;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    if (value <= 0) {
        return false;
    }
    id = value;
    return true;
}

void logFailure(const LoginOptions& options, std::int64_t now, const std::string& username, const char* reason) {
    if (options.log != nullptr) {
        options.log->logFailedLogin(now, isValidUsername(username) ? username : std::string("<invalid>"), reason);
    }
}

// Re-reads the actor from storage: the session alone is not trusted for admin actions.
AuthStatus requireAdmin(IUserRepository& repository, const Session& actor, int& actor_id) {
    if (actor.role != Role::Admin || !parseUserId(actor.user_id, actor_id)) {
        return AuthStatus::PermissionDenied;
    }
    User stored;
    bool found = false;
    if (!repository.findUserById(actor_id, stored, found)) {
        return AuthStatus::StorageError;
    }
    if (!found || !stored.enabled || stored.role != Role::Admin) {
        return AuthStatus::PermissionDenied;
    }
    return AuthStatus::Success;
}

// Shared by registration and admin bootstrap.
AuthResult createAccount(IUserRepository& repository, const std::string& username,
                         const std::string& password, Role role) {
    AuthResult result;

    bool exists = false;
    if (!repository.usernameExists(username, exists)) {
        result.status = AuthStatus::StorageError;
        return result;
    }
    if (exists) {
        result.status = AuthStatus::UsernameAlreadyExists;
        return result;
    }

    User user;
    user.username = username;
    user.role = role;
    user.enabled = true;
    if (!hashPassword(password, user.password_hash)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    int new_id = 0;
    if (!repository.createUser(user, new_id)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    result.status = AuthStatus::Success;
    result.user_id = std::to_string(new_id);
    result.role = role;
    return result;
}

}  // namespace

bool isValidUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        return false;
    }
    return std::all_of(username.begin(), username.end(), [](char raw) {
        const auto c = static_cast<unsigned char>(raw);
        const bool ascii_alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        return ascii_alnum || c == '_' || c == '-' || c == '.';
    });
}

bool isValidPassword(const std::string& password) {
    return password.size() >= kMinPasswordLength && password.size() <= kMaxPasswordLength;
}

bool isSelfRegistrationRole(Role role) {
    return role == Role::Student || role == Role::Instructor;
}

AuthResult registerUser(IUserRepository& repository, const std::string& username,
                        const std::string& password, Role role) {
    AuthResult result;
    if (!isValidUsername(username) || !isValidPassword(password) || !isSelfRegistrationRole(role)) {
        result.status = AuthStatus::InvalidInput;
        return result;
    }
    return createAccount(repository, username, password, role);
}

AuthResult bootstrapAdmin(IUserRepository& repository, const std::string& username,
                          const std::string& password) {
    AuthResult result;
    if (!isValidUsername(username) || !isValidPassword(password)) {
        result.status = AuthStatus::InvalidInput;
        return result;
    }
    int admins = 0;
    if (!repository.countAdmins(admins)) {
        result.status = AuthStatus::StorageError;
        return result;
    }
    if (admins > 0) {
        result.status = AuthStatus::AdminAlreadyExists;
        return result;
    }
    return createAccount(repository, username, password, Role::Admin);
}

AuthResult loginUser(IUserRepository& repository, const std::string& username,
                     const std::string& password, const LoginOptions& options) {
    AuthResult result;
    const std::int64_t now = currentTime(options);

    if (!isValidUsername(username) || password.empty() || password.size() > kMaxPasswordLength) {
        result.status = AuthStatus::InvalidCredentials;
        logFailure(options, now, username, "invalid_input");
        return result;
    }

    User user;
    bool found = false;
    if (!repository.findUserByUsername(username, user, found)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    if (!found) {
        verifyPassword(password, dummyHash());
        result.status = AuthStatus::InvalidCredentials;
        logFailure(options, now, username, "unknown_user");
        return result;
    }

    if (user.locked_until > now) {
        result.status = AuthStatus::AccountLocked;
        logFailure(options, now, username, "account_locked");
        return result;
    }

    if (!verifyPassword(password, user.password_hash)) {
        int attempts = user.failed_attempts + 1;
        std::int64_t locked_until = 0;
        const char* reason = "wrong_password";
        if (attempts >= options.max_failures) {
            attempts = 0;
            locked_until = now + options.lock_seconds;
            reason = "wrong_password_account_locked";
        }
        if (!repository.setLoginFailureState(user.id, attempts, locked_until)) {
            result.status = AuthStatus::StorageError;
            return result;
        }
        result.status = AuthStatus::InvalidCredentials;
        logFailure(options, now, username, reason);
        return result;
    }

    if (user.failed_attempts != 0 || user.locked_until != 0) {
        if (!repository.setLoginFailureState(user.id, 0, 0)) {
            result.status = AuthStatus::StorageError;
            return result;
        }
    }

    // Checked only after the password verified, so a disabled account is not revealed to guessers.
    if (!user.enabled) {
        result.status = AuthStatus::AccountDisabled;
        logFailure(options, now, username, "disabled_account");
        return result;
    }

    result.status = AuthStatus::Success;
    result.user_id = std::to_string(user.id);
    result.role = user.role;
    return result;
}

Session makeSession(const AuthResult& login, std::int64_t unix_time) {
    Session session;
    session.user_id = login.user_id;
    session.role = login.role;
    session.logged_in_at = formatUtcTimestamp(unix_time);
    return session;
}

AuthResult createUserAsAdmin(IUserRepository& repository, const Session& actor, const std::string& username,
                             const std::string& password, Role role) {
    AuthResult result;
    int actor_id = 0;
    const AuthStatus authorized = requireAdmin(repository, actor, actor_id);
    if (authorized != AuthStatus::Success) {
        result.status = authorized;
        return result;
    }
    if (!isValidUsername(username) || !isValidPassword(password) ||
        (role != Role::Student && role != Role::Instructor && role != Role::Admin)) {
        result.status = AuthStatus::InvalidInput;
        return result;
    }
    return createAccount(repository, username, password, role);
}

AuthStatus setAccountEnabled(IUserRepository& repository, const Session& actor, int target_user_id, bool enabled) {
    int actor_id = 0;
    const AuthStatus authorized = requireAdmin(repository, actor, actor_id);
    if (authorized != AuthStatus::Success) {
        return authorized;
    }
    if (target_user_id <= 0) {
        return AuthStatus::InvalidInput;
    }
    if (target_user_id == actor_id) {
        return AuthStatus::PermissionDenied;
    }
    bool updated = false;
    if (!repository.updateUserStatus(target_user_id, enabled, updated)) {
        return AuthStatus::StorageError;
    }
    return updated ? AuthStatus::Success : AuthStatus::NotFound;
}

AuthStatus setUserRole(IUserRepository& repository, const Session& actor, int target_user_id, Role role) {
    int actor_id = 0;
    const AuthStatus authorized = requireAdmin(repository, actor, actor_id);
    if (authorized != AuthStatus::Success) {
        return authorized;
    }
    if (target_user_id <= 0 ||
        (role != Role::Student && role != Role::Instructor && role != Role::Admin)) {
        return AuthStatus::InvalidInput;
    }
    if (target_user_id == actor_id) {
        return AuthStatus::PermissionDenied;
    }
    bool updated = false;
    if (!repository.updateUserRole(target_user_id, role, updated)) {
        return AuthStatus::StorageError;
    }
    return updated ? AuthStatus::Success : AuthStatus::NotFound;
}

AuthStatus removeUser(IUserRepository& repository, const Session& actor, int target_user_id) {
    int actor_id = 0;
    const AuthStatus authorized = requireAdmin(repository, actor, actor_id);
    if (authorized != AuthStatus::Success) {
        return authorized;
    }
    if (target_user_id <= 0) {
        return AuthStatus::InvalidInput;
    }
    if (target_user_id == actor_id) {
        return AuthStatus::PermissionDenied;
    }
    bool deleted = false;
    if (!repository.deleteUser(target_user_id, deleted)) {
        return AuthStatus::StorageError;
    }
    return deleted ? AuthStatus::Success : AuthStatus::NotFound;
}

}  // namespace quizsphere
