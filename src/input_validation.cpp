#include "input_validation.hpp"

#include <cstdint>

namespace quizsphere {

bool validInputText(const std::string& value, std::size_t max_characters, bool multiline) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < value.size(); ++count) {
        if (count >= max_characters) return false;
        const auto first = static_cast<unsigned char>(value[i++]);
        std::uint32_t code = first;
        std::size_t remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { remaining = 1; code &= 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { remaining = 2; code &= 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { remaining = 3; code &= 0x07; minimum = 0x10000; }
        else if (first >= 0x80) return false;
        if (remaining > value.size() - i) return false;
        while (remaining-- > 0) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6U) | (next & 0x3FU);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
        if (code < 32 || (code >= 127 && code <= 159)) {
            if (!(multiline && (code == '\n' || code == '\r' || code == '\t'))) return false;
        }
    }
    return true;
}

bool validInputPath(const std::string& value) {
    return !value.empty() && value.find_first_not_of(" \t\r\n") != std::string::npos &&
           value.size() <= kMaxConsoleLineBytes && validInputText(value, kMaxConsoleLineBytes);
}

}  // namespace quizsphere
