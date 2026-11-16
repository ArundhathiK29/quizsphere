#pragma once

#include <chrono>
#include <string>

namespace quizsphere {

enum class TimedRead { Line, Timeout, Closed, Error };
using ConsoleDeadline = std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds>;

// For the executable's stdin. Call before reading stdin so C stdio cannot
// prefetch bytes that the OS readiness check cannot see.
bool prepareTimedConsoleInput();
TimedRead readConsoleLineUntil(ConsoleDeadline deadline, std::string* line);

}  // namespace quizsphere
