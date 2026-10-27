#include <gtest/gtest.h>

#include <string>

#include "password_hasher.hpp"

using quizsphere::hashPassword;
using quizsphere::kMinPbkdf2Iterations;
using quizsphere::verifyPassword;

namespace {
// Low iteration count keeps the unit tests fast; production uses kDefaultPbkdf2Iterations.
constexpr unsigned long kFast = kMinPbkdf2Iterations;
}  // namespace

TEST(PasswordHasher, UT_AUTH_05_EncodedFormatIsSaltedPbkdf2) {
    std::string encoded;
    ASSERT_TRUE(hashPassword("CorrectHorse9!", encoded, kFast));
    EXPECT_EQ(encoded.rfind("pbkdf2-sha256$10000$", 0), 0u);
    EXPECT_EQ(encoded.find("CorrectHorse9!"), std::string::npos);
}

TEST(PasswordHasher, SameSecretGetsDifferentSalts) {
    std::string a;
    std::string b;
    ASSERT_TRUE(hashPassword("CorrectHorse9!", a, kFast));
    ASSERT_TRUE(hashPassword("CorrectHorse9!", b, kFast));
    EXPECT_NE(a, b);
    EXPECT_TRUE(verifyPassword("CorrectHorse9!", a));
    EXPECT_TRUE(verifyPassword("CorrectHorse9!", b));
}

TEST(PasswordHasher, RejectsWrongPassword) {
    std::string encoded;
    ASSERT_TRUE(hashPassword("CorrectHorse9!", encoded, kFast));
    EXPECT_FALSE(verifyPassword("correcthorse9!", encoded));
    EXPECT_FALSE(verifyPassword("", encoded));
}

TEST(PasswordHasher, EmbeddedNulBytesAreSignificant) {
    const std::string one("pass\0one", 8);
    const std::string two("pass\0two", 8);
    std::string encoded;
    ASSERT_TRUE(hashPassword(one, encoded, kFast));
    EXPECT_TRUE(verifyPassword(one, encoded));
    EXPECT_FALSE(verifyPassword(two, encoded));
}

TEST(PasswordHasher, RefusesIterationCountsOutsidePolicy) {
    std::string encoded;
    EXPECT_FALSE(hashPassword("CorrectHorse9!", encoded, 100));
    EXPECT_FALSE(hashPassword("CorrectHorse9!", encoded, quizsphere::kMaxPbkdf2Iterations + 1));
}

TEST(PasswordHasher, RejectsMalformedStoredValues) {
    std::string good;
    ASSERT_TRUE(hashPassword("CorrectHorse9!", good, kFast));

    EXPECT_FALSE(verifyPassword("CorrectHorse9!", ""));
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "plaintext-password"));
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "pbkdf2-sha256$"));
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "pbkdf2-sha256$abc$00$00"));
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "pbkdf2-sha256$1$00$00"));            // below policy
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "pbkdf2-sha256$999999999$00$00"));    // above policy
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "pbkdf2-sha256$10000$zz$zz"));        // not hex
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", "md5$10000$00$00"));                  // wrong scheme

    // Truncated hash part.
    EXPECT_FALSE(verifyPassword("CorrectHorse9!", good.substr(0, good.size() - 2)));
}
