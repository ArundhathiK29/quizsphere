#include "timed_console_input.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace quizsphere {
namespace {

using std::chrono::steady_clock;

ConsoleDeadline consoleNow() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(steady_clock::now());
}

int waitMilliseconds(ConsoleDeadline deadline) {
    const auto wait = deadline - consoleNow();
    return wait.count() <= 0 ? 0 : static_cast<int>(std::min<long long>(wait.count(), INT_MAX));
}

TimedRead expired() {
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(input, &mode)) FlushConsoleInputBuffer(input);
#else
    if (isatty(STDIN_FILENO)) tcflush(STDIN_FILENO, TCIFLUSH);
#endif
    return TimedRead::Timeout;
}

}  // namespace

bool prepareTimedConsoleInput() {
    return std::setvbuf(stdin, nullptr, _IONBF, 0) == 0;
}

TimedRead readConsoleLineUntil(ConsoleDeadline deadline, std::string* line) {
    line->clear();
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const DWORD type = GetFileType(input);
    DWORD mode = 0;
    const bool terminal = GetConsoleMode(input, &mode) != 0;
    bool extended_key = false;
#else
    pollfd input{STDIN_FILENO, POLLIN, 0};
#endif
    for (;;) {
        const int wait = waitMilliseconds(deadline);
        if (wait == 0) return expired();
#ifdef _WIN32
        if (terminal) {
            if (!_kbhit()) {
                Sleep(static_cast<DWORD>(std::min(wait, 20)));
                continue;
            }
            const int key = _getch();
            if (extended_key) { extended_key = false; continue; }
            if (key == 0 || key == 224) { extended_key = true; continue; }
            if (key == '\r') {
                std::cout << '\n' << std::flush;
                return consoleNow() < deadline ? TimedRead::Line : expired();
            }
            if (key == '\b' && !line->empty()) {
                line->pop_back();
                std::cout << "\b \b" << std::flush;
            } else if (key >= 32 && key <= 126) {
                line->push_back(static_cast<char>(key));
                std::cout << static_cast<char>(key) << std::flush;
            } else if (key == 26) {
                return TimedRead::Closed;
            }
            continue;
        }
        if (type == FILE_TYPE_PIPE) {
            DWORD available = 0;
            if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
                return GetLastError() == ERROR_BROKEN_PIPE ? TimedRead::Closed : TimedRead::Error;
            if (available == 0) {
                Sleep(static_cast<DWORD>(std::min(wait, 20)));
                continue;
            }
        } else if (type != FILE_TYPE_DISK) {
            return TimedRead::Error;
        }
        if (consoleNow() >= deadline) return expired();
        unsigned char raw = 0;
        DWORD count = 0;
        if (!ReadFile(input, &raw, 1, &count, nullptr))
            return GetLastError() == ERROR_BROKEN_PIPE ? TimedRead::Closed : TimedRead::Error;
        const int byte = count == 0 ? std::char_traits<char>::eof() : raw;
#else
        const int ready = poll(&input, 1, wait);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0 || (input.revents & (POLLERR | POLLNVAL))) return TimedRead::Error;
        if (consoleNow() >= deadline) return expired();
        const int byte = std::cin.get();
#endif
        if (byte == std::char_traits<char>::eof())
            return line->empty() ? TimedRead::Closed : TimedRead::Line;
        if (byte == '\n') return TimedRead::Line;
        if (byte != '\r') line->push_back(static_cast<char>(byte));
    }
}

}  // namespace quizsphere
