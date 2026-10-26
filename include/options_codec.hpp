#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace quizsphere {

// Options are stored in one cell: each option has '\' and '|' escaped with '\',
// and options are joined with '|'. Decode exactly once, in reverse order.
inline std::string encodeOptionsCell(const std::vector<std::string>& options) {
    std::string out;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i > 0) {
            out.push_back('|');
        }
        for (const char c : options[i]) {
            if (c == '\\' || c == '|') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
    }
    return out;
}

inline std::vector<std::string> decodeOptionsCell(const std::string& cell) {
    std::vector<std::string> options;
    std::string current;
    bool escape = false;
    for (const char c : cell) {
        if (!escape) {
            if (c == '\\') {
                escape = true;
            } else if (c == '|') {
                options.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '\\' || c == '|') {
            current.push_back(c);
        } else {
            current.push_back('\\');
            current.push_back(c);
        }
        escape = false;
    }
    if (escape) {
        current.push_back('\\');
    }
    options.push_back(current);
    return options;
}

}  // namespace quizsphere
