#pragma once

#include <istream>
#include <ostream>
#include <string>

#include "input_validation.hpp"

namespace quizsphere {

// Drains rejected lines without retaining their suffix as another command.
// False cancels the current action; no partial value is returned or echoed.
bool readConsoleLine(std::istream& input, std::ostream& output, std::string& value,
                     std::size_t max_bytes = kMaxConsoleLineBytes);

// Suppresses echo only for the real terminal input. Streams used by tests/pipes
// remain line-based; passwords are never copied into the output stream.
bool readConsolePassword(std::istream& input, std::ostream& output,
                         const std::string& prompt, std::string& password);

}  // namespace quizsphere
