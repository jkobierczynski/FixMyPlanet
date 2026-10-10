// SPDX-License-Identifier: GPL-3.0-or-later
// A very small test harness: CHECK macros that count failures.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace testing {
inline int& failures() {
    static int n = 0;
    return n;
}
inline int& checks() {
    static int n = 0;
    return n;
}
inline int finish(const char* name) {
    std::printf("%s: %d checks, %d failed\n", name, checks(), failures());
    return failures() == 0 ? 0 : 1;
}
}  // namespace testing

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++testing::checks();                                                     \
        if (!(cond)) {                                                           \
            ++testing::failures();                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        ++testing::checks();                                                     \
        double va_ = (a), vb_ = (b), vt_ = (tol);                                \
        if (!(std::fabs(va_ - vb_) <= vt_)) {                                    \
            ++testing::failures();                                               \
            std::printf("FAIL %s:%d: %s = %.9g, expected %.9g +- %g\n", __FILE__, __LINE__, #a, va_, vb_, vt_); \
        }                                                                        \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                       \
    do {                                                                         \
        ++testing::checks();                                                     \
        std::string sa_ = (a), sb_ = (b);                                        \
        if (sa_ != sb_) {                                                        \
            ++testing::failures();                                               \
            std::printf("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, sa_.c_str(), sb_.c_str()); \
        }                                                                        \
    } while (0)
