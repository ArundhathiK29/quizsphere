
#include "auth.h"
#include "user_repository.h"

#include <cctype>
#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

namespace QuizSphere {
namespace {

constexpr ULONG kIterations = 200000;
constexpr ULONG kSaltBytes = 16;
constexpr ULONG kHashBytes = 32;

std::string toHex(const std::vector<unsigned char>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string result;

    for (unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }

    return result;
}

bool fromHex(
    const std::string& input,
    std::vector<unsigned char>& output
) {
    if (input.size() % 2 != 0) {
        return false;
    }

    output.clear();

    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    for (std::size_t i = 0; i < input.size(); i += 2) {
        int high = value(input[i]);
        int low = value(input[i + 1]);

        if (high < 0 || low < 0) {
            return false;
        }

        output.push_back(
            static_cast<unsigned char>((high << 4) | low)
        );
    }

    return true;
}

bool deriveHash(
    const std::string& password,
    const std::vector<unsigned char>& salt,
    ULONG iterations,
    std::vector<unsigned char>& hash
) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG
    );

    if (status < 0) {
        return false;
    }

    hash.resize(kHashBytes);

    status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(
            const_cast<char*>(password.data())
        ),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        iterations,
        hash.data(),
        static_cast<ULONG>(hash.size()),
        0
    );

    BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0;
}

bool hashPassword(
    const std::string& password,
    std::string& encoded
) {
    std::vector<unsigned char> salt(kSaltBytes);
    std::vector<unsigned char> hash;

    NTSTATUS status = BCryptGenRandom(
        nullptr,
        salt.data(),
        static_cast<ULONG>(salt.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (status < 0 ||
        !deriveHash(password, salt, kIterations, hash)) {
        return false;
    }

    encoded = "pbkdf2-sha256$" +
        std::to_string(kIterations) + "$" +
        toHex(salt) + "$" + toHex(hash);

    return true;
}

bool verifyPassword(
    const std::string& password,
    const std::string& encoded
) {
    const std::string prefix = "pbkdf2-sha256$";

    if (encoded.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    std::size_t first = encoded.find('$', prefix.size());

    if (first == std::string::npos) {
        return false;
    }

    std::size_t second = encoded.find('$', first + 1);

    if (second == std::string::npos) {
        return false;
    }

    ULONG iterations;

    try {
        unsigned long count = std::stoul(
            encoded.substr(prefix.size(), first - prefix.size())
        );

        if (count < 10000 || count > 2000000) {
            return false;
        }

        iterations = static_cast<ULONG>(count);
    } catch (...) {
        return false;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    std::vector<unsigned char> actual;

    if (!fromHex(
            encoded.substr(first + 1, second - first - 1),
            salt
        ) ||
        !fromHex(encoded.substr(second + 1), expected) ||
        salt.size() < 16 ||
        expected.size() != kHashBytes) {
        return false;
    }

    if (!deriveHash(password, salt, iterations, actual)) {
        return false;
    }

    unsigned char difference = 0;

    for (std::size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(
            expected[i] ^ actual[i]
        );
    }

    return difference == 0;
}

} // namespace

bool isValidUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        return false;
    }

    for (unsigned char c : username) {
        if (!(std::isalnum(c) ||
              c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }

    return true;
}

bool isValidRole(UserRole role) {
    return role == UserRole::Student ||
           role == UserRole::Instructor;
}

AuthStatus validateRegistration(
    const std::string& username,
    UserRole role
) {
    if (!isValidUsername(username) || !isValidRole(role)) {
        return AuthStatus::InvalidInput;
    }

    return AuthStatus::Success;
}

AuthResult registerUser(
    IUserRepository& repository,
    const std::string& username,
    const std::string& password,
    UserRole role
) {
    AuthResult result;
    result.status = validateRegistration(username, role);

    if (result.status != AuthStatus::Success ||
        password.size() < 8 ||
        password.size() > 1024) {
        result.status = AuthStatus::InvalidInput;
        return result;
    }

    bool exists = false;

    if (!repository.usernameExists(username, exists)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    if (exists) {
        result.status = AuthStatus::UsernameAlreadyExists;
        return result;
    }

    std::string passwordHash;

    if (!hashPassword(password, passwordHash)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    User user;
    user.username = username;
    user.passwordHash = passwordHash;
    user.role = role;
    user.enabled = true;

    int newUserId = 0;

    if (!repository.createUser(user, newUserId)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    result.status = AuthStatus::Success;
    result.userId = newUserId;
    result.role = role;

    return result;
}

AuthResult loginUser(
    IUserRepository& repository,
    const std::string& username,
    const std::string& password
) {
    AuthResult result;

    if (!isValidUsername(username) || password.empty()) {
        result.status = AuthStatus::InvalidCredentials;
        return result;
    }

    User user;
    bool found = false;

    if (!repository.findUserByUsername(username, user, found)) {
        result.status = AuthStatus::StorageError;
        return result;
    }

    if (!found || !verifyPassword(password, user.passwordHash)) {
        result.status = AuthStatus::InvalidCredentials;
        return result;
    }

    if (!user.enabled) {
        result.status = AuthStatus::AccountDisabled;
        return result;
    }

    result.status = AuthStatus::Success;
    result.userId = user.id;
    result.role = user.role;

    return result;
}

AuthStatus setAccountEnabled(
    IUserRepository& repository,
    int userId,
    bool enabled
) {
    if (userId <= 0) {
        return AuthStatus::InvalidInput;
    }

    return repository.updateUserStatus(userId, enabled)
        ? AuthStatus::Success
        : AuthStatus::StorageError;
}

} // namespace QuizSphere
