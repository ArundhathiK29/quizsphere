#include "auth_event_log.hpp"

#include <ctime>
#include <fstream>
#include <utility>

#include "auth.hpp"

namespace quizsphere {
namespace {

class FileAuthEventLog final : public IAuthEventLog {
public:
    explicit FileAuthEventLog(std::string path) : path_(std::move(path)) {}

    void logFailedLogin(std::int64_t unix_time, const std::string& username, const std::string& reason) override {
        // Defence in depth: never write free-form text (e.g. a password typed into the username field).
        const std::string safe_user = isValidUsername(username) ? username : std::string("<invalid>");
        std::ofstream out(path_, std::ios::app);
        if (out) {
            out << formatUtcTimestamp(unix_time) << " LOGIN_FAILED user=" << safe_user
                << " reason=" << reason << '\n';
        }
    }

private:
    std::string path_;
};

}  // namespace

std::string formatUtcTimestamp(std::int64_t unix_time) {
    const std::time_t raw = static_cast<std::time_t>(unix_time);
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &raw);
#else
    gmtime_r(&raw, &parts);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts) == 0) {
        return "1970-01-01T00:00:00Z";
    }
    return buffer;
}

std::unique_ptr<IAuthEventLog> createFileAuthEventLog(const std::string& path) {
    return std::make_unique<FileAuthEventLog>(path);
}

}  // namespace quizsphere
