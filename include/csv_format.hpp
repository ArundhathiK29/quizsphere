#pragma once

#include <algorithm>
#include <string>

// Shared CSV cell formatting (BL-16 question export, BL-42 transcript export): RFC 4180 quoting
// plus spreadsheet formula neutralisation, so a value like "=1+1" or "-5" is never executed when
// the file is opened in a spreadsheet.
namespace quizsphere {

inline std::string csvEscape(const std::string& value) {
    const bool needs_quotes = std::any_of(value.begin(), value.end(), [](char c) {
        return c == ',' || c == '"' || c == '\n' || c == '\r';
    });
    if (!needs_quotes) {
        return value;
    }
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

inline bool csvIsFormulaChar(char c) {
    return c == '=' || c == '+' || c == '-' || c == '@';
}

// True if the value is (apostrophes)* followed by a formula character. Such values get one
// extra leading apostrophe on export and lose exactly one on import, so the round trip is
// lossless even for values that already start with an apostrophe.
inline bool csvNeedsApostrophe(const std::string& value) {
    std::size_t i = 0;
    while (i < value.size() && value[i] == '\'') {
        ++i;
    }
    return i < value.size() && csvIsFormulaChar(value[i]);
}

inline std::string csvNeutralizeFormula(const std::string& value) {
    return csvNeedsApostrophe(value) ? "'" + value : value;
}

// One fully safe text cell: formula-neutralised, then quoted if needed.
inline std::string csvTextCell(const std::string& value) {
    return csvEscape(csvNeutralizeFormula(value));
}

}  // namespace quizsphere
