#include "console_input.hpp"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace quizsphere {

bool readConsolePassword(std::istream& input, std::ostream& output,
                         const std::string& prompt, std::string& password) {
    output << prompt << std::flush;
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool terminal = &input == &std::cin && handle != INVALID_HANDLE_VALUE &&
                          GetConsoleMode(handle, &mode) != 0;
    if (terminal) SetConsoleMode(handle, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    const bool ok = static_cast<bool>(std::getline(input, password));
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
    const bool ok = static_cast<bool>(std::getline(input, password));
    if (terminal) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
        output << '\n';
    }
#endif
    return ok;
}

}  // namespace quizsphere
