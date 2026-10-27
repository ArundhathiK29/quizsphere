#pragma once

#include <cstddef>
#include <string>

namespace quizsphere {

void logRejectedImportRow(const std::string& reason, std::size_t row_number);

}  // namespace quizsphere
