#include "password_hasher.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstddef>
#include <limits>
#include <vector>

namespace quizsphere {
namespace {

constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;
const char kPrefix[] = "pbkdf2-sha256$";

std::string toHex(const std::vector<unsigned char>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool fromHex(const std::string& input, std::vector<unsigned char>& output) {
    output.clear();
    if (input.size() % 2 != 0) {
        return false;
    }
    output.reserve(input.size() / 2);
    for (std::size_t i = 0; i < input.size(); i += 2) {
        const int high = hexValue(input[i]);
        const int low = hexValue(input[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        output.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

bool derive(const std::string& password,
            const std::vector<unsigned char>& salt,
            unsigned long iterations,
            std::vector<unsigned char>& hash) {
    if (password.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        iterations > static_cast<unsigned long>(std::numeric_limits<int>::max())) {
        return false;
    }
    hash.assign(kHashBytes, 0);
    const int ok = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                                     salt.data(), static_cast<int>(salt.size()),
                                     static_cast<int>(iterations), EVP_sha256(),
                                     static_cast<int>(hash.size()), hash.data());
    return ok == 1;
}

// Parses an unsigned decimal without exceptions. Rejects empty/overflowing input.
bool parseIterations(const std::string& text, unsigned long& value) {
    if (text.empty() || text.size() > 9) {
        return false;
    }
    unsigned long result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + static_cast<unsigned long>(c - '0');
    }
    value = result;
    return true;
}

}  // namespace

bool hashPassword(const std::string& password, std::string& encoded, unsigned long iterations) {
    if (iterations < kMinPbkdf2Iterations || iterations > kMaxPbkdf2Iterations) {
        return false;
    }

    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        return false;
    }

    std::vector<unsigned char> hash;
    if (!derive(password, salt, iterations, hash)) {
        return false;
    }

    encoded = std::string(kPrefix) + std::to_string(iterations) + "$" + toHex(salt) + "$" + toHex(hash);
    OPENSSL_cleanse(hash.data(), hash.size());
    return true;
}

bool verifyPassword(const std::string& password, const std::string& encoded) {
    const std::string prefix(kPrefix);
    if (encoded.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    const std::size_t first = encoded.find('$', prefix.size());
    if (first == std::string::npos) {
        return false;
    }
    const std::size_t second = encoded.find('$', first + 1);
    if (second == std::string::npos) {
        return false;
    }

    unsigned long iterations = 0;
    if (!parseIterations(encoded.substr(prefix.size(), first - prefix.size()), iterations) ||
        iterations < kMinPbkdf2Iterations || iterations > kMaxPbkdf2Iterations) {
        return false;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!fromHex(encoded.substr(first + 1, second - first - 1), salt) ||
        !fromHex(encoded.substr(second + 1), expected) ||
        salt.size() < kSaltBytes || expected.size() != kHashBytes) {
        return false;
    }

    std::vector<unsigned char> actual;
    if (!derive(password, salt, iterations, actual)) {
        return false;
    }

    const bool match = CRYPTO_memcmp(expected.data(), actual.data(), kHashBytes) == 0;
    OPENSSL_cleanse(actual.data(), actual.size());
    return match;
}

}  // namespace quizsphere
