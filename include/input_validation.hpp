#pragma once

#include <cstddef>
#include <string>

namespace quizsphere {

constexpr std::size_t kMaxConsoleLineBytes = 4096;
constexpr std::size_t kMaxImportBytes = 10U * 1024U * 1024U;

// Reject malformed/overlong UTF-8, surrogate code points and terminal controls.
// Text-area fields may contain LF/CR/tab; single-line fields may not.
bool validInputText(const std::string& value, std::size_t max_characters, bool multiline = false);
bool validInputPath(const std::string& value);

}  // namespace quizsphere
