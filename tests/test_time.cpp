// SPDX-License-Identifier: GPL-3.0-or-later
#include "testing.h"
#include "timeutil.h"

using namespace fixmyplanet;

int main() {
    // Julian dates of well-known instants.
    CHECK_NEAR(julianDate(2000, 1, 1, 12, 0, 0), 2451545.0, 1e-9);
    CHECK_NEAR(julianDate(1992, 12, 16), 2448972.5, 1e-9);
    CHECK_NEAR(julianDate(1858, 11, 17), 2400000.5, 1e-9);  // MJD zero
    CHECK_NEAR(julianDate(2026, 1, 10, 22, 0, 0), 2461051.4166666665, 1e-7);

    // Round trip through the calendar, including a leap day and year ends.
    const int dates[][6] = {{2024, 2, 29, 23, 59, 59}, {1999, 12, 31, 0, 0, 0}, {2026, 10, 9, 22, 32, 7}, {2100, 3, 1, 6, 30, 15}};
    for (const auto& d : dates) {
        double jd = julianDate(d[0], d[1], d[2], d[3], d[4], d[5]);
        int y, mo, da, h, mi;
        double s;
        calendarDate(jd + 1e-9, y, mo, da, h, mi, s);
        CHECK(y == d[0] && mo == d[1] && da == d[2] && h == d[3] && mi == d[4]);
        CHECK_NEAR(s, d[5], 1e-3);
    }

    // Leap seconds and TT.
    CHECK_NEAR(leapSeconds(julianDate(2016, 12, 31, 23, 59, 59)), 36.0, 1e-9);
    CHECK_NEAR(leapSeconds(julianDate(2017, 1, 1)), 37.0, 1e-9);
    CHECK_NEAR(leapSeconds(julianDate(2026, 6, 1)), 37.0, 1e-9);
    CHECK_NEAR(leapSeconds(julianDate(1992, 12, 16)), 27.0, 1e-9);
    CHECK_NEAR((utcToTT(julianDate(2026, 1, 1)) - julianDate(2026, 1, 1)) * 86400.0, 69.184, 1e-4);

    // Parsing.
    double jd = 0;
    CHECK(parseTime("2026-01-10T22:03:30", jd));
    CHECK_NEAR(jd, julianDate(2026, 1, 10, 22, 3, 30), 1e-9);
    CHECK(parseTime("2026-01-10 22:03", jd));
    CHECK_NEAR(jd, julianDate(2026, 1, 10, 22, 3, 0), 1e-9);
    CHECK(parseTime("2026-01-10T22:03:30.5Z", jd));
    CHECK_NEAR(jd, julianDate(2026, 1, 10, 22, 3, 30.5), 1e-9);
    CHECK(parseTime("  2026/01/10T22:03:30 UTC ", jd));
    CHECK(!parseTime("", jd));
    CHECK(!parseTime("2026-01-10", jd));
    CHECK(!parseTime("2026-13-10T22:03:30", jd));
    CHECK(!parseTime("2026-02-30T22:03:30", jd));
    CHECK(!parseTime("2025-02-29T00:00:00", jd));
    CHECK(parseTime("2024-02-29T00:00:00", jd));
    CHECK(!parseTime("2026-01-10T25:03:30", jd));
    CHECK(!parseTime("2026-01-10T22:03:30 tomorrow", jd));
    CHECK(!parseTime("yesterday", jd));

    // Formatting, including rounding that carries over.
    CHECK_EQ_STR(formatTime(julianDate(2026, 1, 10, 22, 3, 30), 0), "2026-01-10T22:03:30Z");
    CHECK_EQ_STR(formatTime(julianDate(2026, 1, 10, 22, 3, 30.5), 1), "2026-01-10T22:03:30.5Z");
    CHECK_EQ_STR(formatTime(julianDate(2025, 12, 31, 23, 59, 59.97), 1), "2026-01-01T00:00:00.0Z");
    CHECK_EQ_STR(formatTime(julianDate(2026, 1, 10, 0, 0, 0), 0), "2026-01-10T00:00:00Z");
    for (int i = 0; i < 2000; ++i) {
        // format -> parse must return the same instant to within the printed precision.
        double t = julianDate(2026, 3, 1) + i * 0.0123457;
        double back = 0;
        CHECK(parseTime(formatTime(t, 1), back));
        CHECK_NEAR((back - t) * 86400.0, 0.0, 0.051);
    }

    // File names.
    CHECK(timeFromFileName("/data/2022-09-26-2130_5-U-RGB-Jup.tif", jd));
    CHECK_NEAR(jd, julianDate(2022, 9, 26, 21, 30, 30), 1e-9);
    CHECK(timeFromFileName("2022-09-26-2130.5-Jup_lapl4_ap25.png", jd));
    CHECK_NEAR(jd, julianDate(2022, 9, 26, 21, 30, 30), 1e-9);
    CHECK(timeFromFileName("Jup_2022-09-26_21-30-15_R.tif", jd));
    CHECK_NEAR(jd, julianDate(2022, 9, 26, 21, 30, 15), 1e-9);
    CHECK(timeFromFileName("C:\\caps\\jupiter_2026-01-10_22-04-00.tif", jd));
    CHECK_NEAR(jd, julianDate(2026, 1, 10, 22, 4, 0), 1e-9);
    CHECK(timeFromFileName("Sat_20220926_213015_g3.fits", jd));
    CHECK_NEAR(jd, julianDate(2022, 9, 26, 21, 30, 15), 1e-9);
    CHECK(timeFromFileName("2022-09-26T21_30_15.ser.tif", jd));
    CHECK_NEAR(jd, julianDate(2022, 9, 26, 21, 30, 15), 1e-9);
    CHECK(timeFromFileName("2022-09-26-2130-Jup.tif", jd));
    CHECK_NEAR(jd, julianDate(2022, 9, 26, 21, 30, 0), 1e-9);
    CHECK(!timeFromFileName("jupiter_final_v2.tif", jd));
    CHECK(!timeFromFileName("IMG_12345678.tif", jd));
    CHECK(!timeFromFileName("2022-09-26.tif", jd));
    // The date in a folder name must not be picked up.
    CHECK(!timeFromFileName("/data/2022-09-26_21-30-15/stack.tif", jd));

    return testing::finish("test_time");
}
