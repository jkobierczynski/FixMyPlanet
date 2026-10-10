// SPDX-License-Identifier: GPL-3.0-or-later
// Locale-independent number <-> text conversion. The C library versions follow
// the user's locale, which breaks on systems that write decimals with a comma.
#pragma once

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace fixmyplanet {

// Parse a complete decimal number; returns false if `text` is not one.
inline bool parseDouble(const std::string& text, double& out) {
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    size_t b = text.find_last_not_of(" \t\r\n");
    std::string t = text.substr(a, b - a + 1);
    if (!t.empty() && t[0] == '+') t.erase(0, 1);
    // Plain decimal notation only: some standard libraries would also take
    // "inf", "nan" and hexadecimal floats here.
    for (char& ch : t) {
        if (ch == 'D' || ch == 'd') ch = 'E';  // FITS allows 'D' as the exponent letter
        const bool ok = (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || ch == 'e' || ch == 'E';
        if (!ok) return false;
    }
    std::istringstream is(t);
    is.imbue(std::locale::classic());
    double v;
    is >> v;
    if (is.fail()) return false;
    is >> std::ws;
    if (!is.eof() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

inline std::string formatDouble(double v, int significant = 10) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(significant) << v;
    return os.str();
}

inline std::string formatFixed(double v, int decimals) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::fixed << std::setprecision(decimals) << v;
    return os.str();
}

}  // namespace fixmyplanet
