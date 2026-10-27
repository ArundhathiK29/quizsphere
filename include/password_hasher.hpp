#pragma once

#include <string>

namespace quizsphere {

// Passwords are stored as:  pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>
// (PBKDF2-HMAC-SHA256, unique random salt per password). Implemented with
// OpenSSL so it builds the same way on Linux and Windows.

constexpr unsigned long kDefaultPbkdf2Iterations = 600000;  // OWASP guidance for PBKDF2-HMAC-SHA256
constexpr unsigned long kMinPbkdf2Iterations = 10000;
constexpr unsigned long kMaxPbkdf2Iterations = 5000000;

bool hashPassword(const std::string& password,
                  std::string& encoded,
                  unsigned long iterations = kDefaultPbkdf2Iterations);

// Constant-time comparison of the derived hash. Returns false for any
// malformed or out-of-policy encoded value.
bool verifyPassword(const std::string& password, const std::string& encoded);

}  // namespace quizsphere
