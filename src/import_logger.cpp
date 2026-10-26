#include "import_logger.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace quizsphere {
namespace {

std::string nowIso8601Utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ts.str();
}

std::string singleLine(std::string text) {
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    return text;
}

class FileImportLogger final : public ImportLogger {
public:
    explicit FileImportLogger(std::string path) : path_(std::move(path)) {}

    bool logRejectedRow(std::size_t row_number, const std::string& reason) override {
        const std::filesystem::path target(path_);
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                return false;
            }
        }

        std::ofstream out(path_, std::ios::app);
        if (!out.is_open()) {
            return false;
        }
        out << nowIso8601Utc() << " | row " << row_number << " | " << singleLine(reason) << '\n';
        out.flush();
        return out.good();
    }

private:
    std::string path_;
};

}  // namespace

std::unique_ptr<ImportLogger> createFileImportLogger(std::string log_path) {
    return std::make_unique<FileImportLogger>(std::move(log_path));
}

}  // namespace quizsphere
