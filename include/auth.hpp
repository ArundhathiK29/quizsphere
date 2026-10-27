#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "auth_event_log.hpp"
#include "common_types.hpp"
#include "user_repository.hpp"

namespace quizsphere {

// Names follow the SAD error codes (INVALID_CREDENTIALS, ACCOUNT_DISABLED, ...).
enum class AuthStatus {
    Success,
    InvalidInput,
    UsernameAlreadyExists,
    InvalidCredentials,
    AccountDisabled,
    AccountLocked,
    PermissionDenied,
    NotFound,
    AdminAlreadyExists,
    StorageError
};

struct AuthResult {
    AuthStatus status{AuthStatus::InvalidInput};
    std::string user_id;
    Role role{Role::Student};

    [[nodiscard]] bool ok() const { return status == AuthStatus::Success; }
};

struct LoginOptions {
    IAuthEventLog* log{nullptr};                 // optional failed-login log (SEC-006)
    std::function<std::int64_t()> now;           // injectable clock (unix seconds); defaults to system time
    int max_failures{5};                         // consecutive failures before the account is locked
    std::int64_t lock_seconds{300};              // lock duration
};

constexpr std::size_t kMinPasswordLength = 8;
constexpr std::size_t kMaxPasswordLength = 1024;

bool isValidUsername(const std::string& username);   // 3-32 chars: letters, digits, '_', '-', '.'
bool isValidPassword(const std::string& password);   // 8-1024 bytes
bool isSelfRegistrationRole(Role role);              // FR-001: only Student or Instructor

// FR-001. Public self-registration; Admin accounts can never be created through this path.
AuthResult registerUser(IUserRepository& repository, const std::string& username,
                        const std::string& password, Role role);

// First-run only: creates the initial Admin when no Admin exists yet.
AuthResult bootstrapAdmin(IUserRepository& repository, const std::string& username,
                          const std::string& password);

// FR-002 / FR-022 / SEC-006.
AuthResult loginUser(IUserRepository& repository, const std::string& username,
                     const std::string& password, const LoginOptions& options = LoginOptions{});

Session makeSession(const AuthResult& login, std::int64_t unix_time);

// Admin-only operations (SEC-003). Authorization is checked here, not only in the UI:
// the actor is re-read from storage and must be an enabled Admin. An Admin cannot
// disable, demote or remove their own account, so at least one enabled Admin always remains.
AuthResult createUserAsAdmin(IUserRepository& repository, const Session& actor, const std::string& username,
                             const std::string& password, Role role);
AuthStatus setAccountEnabled(IUserRepository& repository, const Session& actor, int target_user_id, bool enabled);
AuthStatus setUserRole(IUserRepository& repository, const Session& actor, int target_user_id, Role role);
AuthStatus removeUser(IUserRepository& repository, const Session& actor, int target_user_id);

}  // namespace quizsphere
