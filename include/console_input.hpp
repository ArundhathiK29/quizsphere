#pragma once

#include <istream>
#include <ostream>
#include <string>

namespace quizsphere {

// Suppresses echo only for the real terminal input. Streams used by tests/pipes
// remain line-based; passwords are never copied into the output stream.
bool readConsolePassword(std::istream& input, std::ostream& output,
                         const std::string& prompt, std::string& password);

}  // namespace quizsphere
