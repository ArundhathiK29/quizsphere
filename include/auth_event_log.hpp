#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace quizsphere {

// SEC-006: failed logins are logged with a timestamp, never with the password.
class IAuthEventLog {
public:
    virtual ~IAuthEventLog() = default;
    // `username` is only passed through if it is a well-formed username; otherwise "<invalid>".
    virtual void logFailedLogin(std::int64_t unix_time, const std::string& username, const std::string& reason) = 0;
};

// Appends one line per event to `path`: "<ISO-8601 UTC> LOGIN_FAILED user=<name> reason=<reason>".
std::unique_ptr<IAuthEventLog> createFileAuthEventLog(const std::string& path);

std::string formatUtcTimestamp(std::int64_t unix_time);

}  // namespace quizsphere
