// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeutil.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fixmyplanet {

double julianDate(int year, int month, int day, int hour, int minute, double second) {
    // Fliegel & Van Flandern style integer algorithm (Gregorian calendar).
    long a = (14 - month) / 12;
    long y = year + 4800 - a;
    long m = month + 12 * a - 3;
    long jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    return static_cast<double>(jdn) - 0.5 + (hour + (minute + second / 60.0) / 60.0) / 24.0;
}

void calendarDate(double jd, int& year, int& month, int& day, int& hour, int& minute, double& second) {
    double z = std::floor(jd + 0.5);
    double f = jd + 0.5 - z;
    long jdn = static_cast<long>(z);
    long a = jdn + 32044;
    long b = (4 * a + 3) / 146097;
    long c = a - 146097 * b / 4;
    long d = (4 * c + 3) / 1461;
    long e = c - 1461 * d / 4;
    long m = (5 * e + 2) / 153;
    day = static_cast<int>(e - (153 * m + 2) / 5 + 1);
    month = static_cast<int>(m + 3 - 12 * (m / 10));
    year = static_cast<int>(100 * b + d - 4800 + m / 10);
    double secs = f * 86400.0;
    if (secs < 0) secs = 0;
    hour = static_cast<int>(secs / 3600.0);
    if (hour > 23) hour = 23;
    secs -= hour * 3600.0;
    minute = static_cast<int>(secs / 60.0);
    if (minute > 59) minute = 59;
    second = secs - minute * 60.0;
}

double leapSeconds(double jdUtc) {
    struct Entry { int y, m; double taiUtc; };
    static const Entry table[] = {
        {1972, 1, 10}, {1972, 7, 11}, {1973, 1, 12}, {1974, 1, 13}, {1975, 1, 14}, {1976, 1, 15},
        {1977, 1, 16}, {1978, 1, 17}, {1979, 1, 18}, {1980, 1, 19}, {1981, 7, 20}, {1982, 7, 21},
        {1983, 7, 22}, {1985, 7, 23}, {1988, 1, 24}, {1990, 1, 25}, {1991, 1, 26}, {1992, 7, 27},
        {1993, 7, 28}, {1994, 7, 29}, {1996, 1, 30}, {1997, 7, 31}, {1999, 1, 32}, {2006, 1, 33},
        {2009, 1, 34}, {2012, 7, 35}, {2015, 7, 36}, {2017, 1, 37},
    };
    double result = 10.0;
    for (const Entry& e : table) {
        if (jdUtc >= julianDate(e.y, e.m, 1)) result = e.taiUtc;
        else break;
    }
    return result;
}

double utcToTT(double jdUtc) { return jdUtc + (leapSeconds(jdUtc) + 32.184) / 86400.0; }

static bool validDateTime(int y, int mo, int d, int h, int mi, double s) {
    if (y < 1600 || y > 2999 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s >= 61.0) return false;
    static const int dim[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (d > dim[mo - 1]) return false;
    if (mo == 2 && d == 29) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        if (!leap) return false;
    }
    return true;
}

// Read exactly `n` digits at position `pos`; advances pos on success.
static bool readDigits(const std::string& s, size_t& pos, int n, int& out) {
    if (pos + n > s.size()) return false;
    int v = 0;
    for (int i = 0; i < n; ++i) {
        char ch = s[pos + i];
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
    }
    pos += n;
    out = v;
    return true;
}

bool parseTime(const std::string& textIn, double& jdUtc) {
    std::string t;
    for (char ch : textIn)
        if (!std::isspace(static_cast<unsigned char>(ch)) || !t.empty()) t += ch;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    size_t p = 0;
    int y, mo, d, h, mi;
    if (!readDigits(t, p, 4, y)) return false;
    if (p >= t.size() || (t[p] != '-' && t[p] != '/')) return false;
    ++p;
    if (!readDigits(t, p, 2, mo)) return false;
    if (p >= t.size() || (t[p] != '-' && t[p] != '/')) return false;
    ++p;
    if (!readDigits(t, p, 2, d)) return false;
    if (p >= t.size() || (t[p] != 'T' && t[p] != 't' && t[p] != ' ' && t[p] != '_')) return false;
    while (p < t.size() && (t[p] == 'T' || t[p] == 't' || t[p] == ' ' || t[p] == '_')) ++p;
    if (!readDigits(t, p, 2, h)) return false;
    if (p >= t.size() || t[p] != ':') return false;
    ++p;
    if (!readDigits(t, p, 2, mi)) return false;
    double sec = 0.0;
    if (p < t.size() && t[p] == ':') {
        ++p;
        int si;
        if (!readDigits(t, p, 2, si)) return false;
        sec = si;
        if (p < t.size() && (t[p] == '.' || t[p] == ',')) {
            ++p;
            double scale = 0.1;
            bool any = false;
            while (p < t.size() && t[p] >= '0' && t[p] <= '9') {
                sec += (t[p] - '0') * scale;
                scale *= 0.1;
                ++p;
                any = true;
            }
            if (!any) return false;
        }
    }
    while (p < t.size() && t[p] == ' ') ++p;
    if (p < t.size() && (t[p] == 'Z' || t[p] == 'z')) ++p;
    else if (t.compare(p, std::string::npos, "UTC") == 0 || t.compare(p, std::string::npos, "UT") == 0) p = t.size();
    if (p != t.size()) return false;
    if (!validDateTime(y, mo, d, h, mi, sec)) return false;
    jdUtc = julianDate(y, mo, d, h, mi, sec);
    return true;
}

std::string formatTime(double jdUtc, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 3) decimals = 3;
    // Work in whole ticks of 10^-decimals seconds so that rounding can carry
    // into the minutes, hours and date, and no locale-dependent decimal point
    // is involved.
    long long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const long long ticksPerDay = 86400LL * scale;
    double z = std::floor(jdUtc + 0.5);
    double f = jdUtc + 0.5 - z;
    long long ticks = static_cast<long long>(std::floor(f * static_cast<double>(ticksPerDay) + 0.5));
    if (ticks >= ticksPerDay) {
        ticks -= ticksPerDay;
        z += 1.0;
    }
    int y, mo, d, h0, mi0;
    double s0;
    calendarDate(z - 0.5 + 1e-6, y, mo, d, h0, mi0, s0);
    long long secTicks = ticks % (60LL * scale);
    long long minutes = ticks / (60LL * scale);
    int h = static_cast<int>(minutes / 60), mi = static_cast<int>(minutes % 60);
    char buf[64];
    if (decimals == 0)
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02lldZ", y, mo, d, h, mi, secTicks);
    else
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02lld.%0*lldZ", y, mo, d, h, mi, secTicks / scale,
                      decimals, secTicks % scale);
    return buf;
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool isSep(char c) { return c == '-' || c == '_' || c == '.' || c == ' ' || c == 'T' || c == 't'; }

bool timeFromFileName(const std::string& path, double& jdUtc) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t n = name.size();

    for (size_t start = 0; start + 8 <= n; ++start) {
        if (start > 0 && isDigit(name[start - 1])) continue;  // must begin a digit run
        size_t p = start;
        int y, mo, d;
        if (!readDigits(name, p, 4, y)) continue;
        bool dashed = false;
        if (p < n && (name[p] == '-' || name[p] == '_' || name[p] == '.')) {
            dashed = true;
            ++p;
        }
        if (!readDigits(name, p, 2, mo)) continue;
        if (dashed) {
            if (p >= n || !(name[p] == '-' || name[p] == '_' || name[p] == '.')) continue;
            ++p;
        }
        if (!readDigits(name, p, 2, d)) continue;
        if (p < n && isDigit(name[p])) continue;  // date digits must end here
        // Separator(s) between date and time.
        size_t q = p;
        while (q < n && isSep(name[q])) ++q;
        if (q == p || q - p > 2) continue;
        p = q;
        int h, mi;
        if (!readDigits(name, p, 2, h)) continue;
        bool timeSep = false;
        if (p < n && (name[p] == '-' || name[p] == '_' || name[p] == ':' || name[p] == '.')) {
            // Only a separator if two more digits follow.
            if (p + 2 < n && isDigit(name[p + 1]) && isDigit(name[p + 2])) {
                timeSep = true;
                ++p;
            }
        }
        if (!readDigits(name, p, 2, mi)) continue;
        double sec = 0.0;
        bool gotSeconds = false;
        if (timeSep) {
            // HH-MM-SS expected.
            if (p < n && (name[p] == '-' || name[p] == '_' || name[p] == ':' || name[p] == '.')) {
                size_t r = p + 1;
                int si;
                if (readDigits(name, r, 2, si) && !(r < n && isDigit(name[r]))) {
                    sec = si;
                    p = r;
                    gotSeconds = true;
                }
            }
        } else {
            // HHMM followed by SS, or by "_T" / ".T" (tenths of a minute, WinJUPOS).
            size_t r = p;
            int si;
            if (readDigits(name, r, 2, si) && !(r < n && isDigit(name[r]))) {
                sec = si;
                p = r;
                gotSeconds = true;
            } else if (p + 1 < n && (name[p] == '_' || name[p] == '.') && isDigit(name[p + 1]) &&
                       !(p + 2 < n && isDigit(name[p + 2]))) {
                sec = (name[p + 1] - '0') * 6.0;
                p += 2;
                gotSeconds = true;
            } else if (p < n && isDigit(name[p])) {
                continue;  // odd digit count, not a time
            }
        }
        (void)gotSeconds;
        if (!validDateTime(y, mo, d, h, mi, sec)) continue;
        jdUtc = julianDate(y, mo, d, h, mi, sec);
        return true;
    }
    return false;
}

}  // namespace fixmyplanet
