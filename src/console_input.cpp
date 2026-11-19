#include "console_input.hpp"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace quizsphere {
namespace {
int readConsoleByte(std::istream& input) {
#if defined(_WIN32)
    // Normal and timed real input share the raw handle, without CRT lookahead.
    if (&input == &std::cin) {
        unsigned char byte = 0;
        DWORD count = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &count, nullptr) || count == 0)
            return std::char_traits<char>::eof();
        return byte;
    }
#endif
    return input.get();
}
}  // namespace

bool readConsoleLine(std::istream& input, std::ostream& output, std::string& value, std::size_t max_bytes,
                     ConsoleLineStatus* status) {
    value.clear();
    if (status) *status = ConsoleLineStatus::EndOfInput;
    bool rejected = false;
    bool received = false;
    for (;;) {
        const int raw = readConsoleByte(input);
        if (raw == std::char_traits<char>::eof()) break;
        const char byte = static_cast<char>(raw);
        received = true;
        if (byte == '\n') break;
        if (value.size() < max_bytes) value.push_back(byte);
        else rejected = true;
    }
    // Accept CRLF streams without permitting embedded carriage-return controls.
    if (!value.empty() && value.back() == '\r') value.pop_back();
    if (rejected || !validInputText(value, max_bytes, true) || value.find_first_of("\r\n") != std::string::npos) {
        value.clear();
        if (status) *status = ConsoleLineStatus::Rejected;
        output << "Invalid input: line is too long or contains unsupported characters.\n";
        return false;
    }
    if (received && status) *status = ConsoleLineStatus::Accepted;
    return received;
}

bool readConsolePassword(std::istream& input, std::ostream& output,
                         const std::string& prompt, std::string& password) {
    output << prompt << std::flush;
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool terminal = &input == &std::cin && handle != INVALID_HANDLE_VALUE &&
                          GetConsoleMode(handle, &mode) != 0;
    if (terminal) SetConsoleMode(handle, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    const bool ok = readConsoleLine(input, output, password, 1024);
    if (terminal) {
        SetConsoleMode(handle, mode);
        output << '\n';
    }
#else
    termios original{};
    const bool terminal = &input == &std::cin && isatty(STDIN_FILENO) != 0 &&
                          tcgetattr(STDIN_FILENO, &original) == 0;
    if (terminal) {
        termios silent = original;
        silent.c_lflag &= static_cast<tcflag_t>(~ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &silent);
    }
    const bool ok = readConsoleLine(input, output, password, 1024);
    if (terminal) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
        output << '\n';
    }
#endif
    return ok;
}

}  // namespace quizsphere
