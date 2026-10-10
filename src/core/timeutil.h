// SPDX-License-Identifier: GPL-3.0-or-later
// Time handling: UTC <-> Julian date, TT, and timestamps found in file names.
#pragma once

#include <string>

namespace fixmyplanet {

constexpr double kJ2000 = 2451545.0;

// Julian date for a proleptic Gregorian calendar date and time of day.
double julianDate(int year, int month, int day, int hour = 0, int minute = 0, double second = 0.0);

// Inverse of julianDate(). `second` is not rounded.
void calendarDate(double jd, int& year, int& month, int& day, int& hour, int& minute, double& second);

// TAI - UTC in seconds for a UTC Julian date (leap second table, 1972 onwards;
// a constant 10 s is returned for earlier dates).
double leapSeconds(double jdUtc);

// Terrestrial Time (used as TDB) for a UTC Julian date.
double utcToTT(double jdUtc);

// Parse "YYYY-MM-DD[T| ]HH:MM[:SS[.fff]][Z]" (also accepts '/' in the date).
// Returns false when the text is not a complete date and time.
bool parseTime(const std::string& text, double& jdUtc);

// "YYYY-MM-DDTHH:MM:SS[.fff]Z"
std::string formatTime(double jdUtc, int decimals = 0);

// Try to find a capture time in a file name. Recognised patterns:
//   2022-09-26-2130_5      WinJUPOS style, last digit is tenths of a minute
//   2022-09-26-2130.5      same with a dot
//   2022-09-26_21-30-15    / 2022-09-26T21_30_15 / 2022-09-26-213015
//   20220926_213015        compact
// Only the file name is examined, not the directory part.
bool timeFromFileName(const std::string& path, double& jdUtc);

}  // namespace fixmyplanet
