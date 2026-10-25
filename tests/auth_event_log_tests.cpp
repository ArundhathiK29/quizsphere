#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "auth_event_log.hpp"

namespace {

std::string uniqueTempPath() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() / ("quizsphere_log_" + std::to_string(ticks) + ".log")).string();
}

std::string readAll(const std::string& path) {
    std::string content;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return content;
    }
    char buffer[256];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, n);
    }
    std::fclose(file);
    return content;
}

}  // namespace

TEST(AuthEventLog, FormatsUtcTimestamps) {
    EXPECT_EQ(quizsphere::formatUtcTimestamp(0), "1970-01-01T00:00:00Z");
    EXPECT_EQ(quizsphere::formatUtcTimestamp(1700000000), "2023-11-14T22:13:20Z");
}

TEST(AuthEventLog, UT_AUTH_16_WritesTimestampedLineWithoutAnyPassword) {
    const std::string path = uniqueTempPath();
    {
        auto log = quizsphere::createFileAuthEventLog(path);
        log->logFailedLogin(1700000000, "aruu_123", "wrong_password");
    }
    const std::string content = readAll(path);
    EXPECT_EQ(content, "2023-11-14T22:13:20Z LOGIN_FAILED user=aruu_123 reason=wrong_password\n");
    std::remove(path.c_str());
}

TEST(AuthEventLog, NeverWritesFreeFormTextFromTheUsernameField) {
    const std::string path = uniqueTempPath();
    {
        auto log = quizsphere::createFileAuthEventLog(path);
        // A user pasting their password (or a log-injection attempt) into the username prompt.
        log->logFailedLogin(1700000000, "My Secret Pass!\nFAKE LINE", "invalid_input");
    }
    const std::string content = readAll(path);
    EXPECT_EQ(content.find("Secret"), std::string::npos);
    EXPECT_EQ(content.find("FAKE LINE"), std::string::npos);
    EXPECT_NE(content.find("user=<invalid>"), std::string::npos);
    std::remove(path.c_str());
}

TEST(AuthEventLog, AppendsRatherThanOverwrites) {
    const std::string path = uniqueTempPath();
    {
        auto log = quizsphere::createFileAuthEventLog(path);
        log->logFailedLogin(1, "alice", "unknown_user");
        log->logFailedLogin(2, "bob", "wrong_password");
    }
    const std::string content = readAll(path);
    EXPECT_NE(content.find("user=alice"), std::string::npos);
    EXPECT_NE(content.find("user=bob"), std::string::npos);
    std::remove(path.c_str());
}
