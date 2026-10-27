#include "import_logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace quizsphere {

void logRejectedImportRow(const std::string& reason, std::size_t row_number) {
    std::error_code mkdir_ec;
    std::filesystem::create_directories("logs", mkdir_ec);
    if (mkdir_ec) {
        return;
    }

    std::ofstream out("logs/question_import.log", std::ios::app);
    if (!out.is_open()) {
        return;
    }

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
    out << ts.str() << " | row " << row_number << " | " << reason << '\n';
}

}  // namespace quizsphere
