#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace quizsphere {

class ImportLogger {
public:
    virtual ~ImportLogger() = default;
    // Returns false if the line could not be written.
    virtual bool logRejectedRow(std::size_t row_number, const std::string& reason) = 0;
};

// Appends "ISO-8601 UTC timestamp | row N | reason" lines to the given file
// (the directory is created if missing). Never logs question content.
std::unique_ptr<ImportLogger> createFileImportLogger(std::string log_path = "logs/question_import.log");

}  // namespace quizsphere
