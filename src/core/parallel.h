// SPDX-License-Identifier: GPL-3.0-or-later
// Tiny row-parallel loop helper.
#pragma once

#include <algorithm>
#include <functional>
#include <thread>
#include <vector>

namespace fixmyplanet {

// Calls fn(y0, y1) for disjoint row ranges covering [0, rows) on several threads.
// threads <= 0 means "use all hardware threads".
inline void parallelRows(int rows, int threads, const std::function<void(int, int)>& fn) {
    if (rows <= 0) return;
    int n = threads > 0 ? threads : static_cast<int>(std::thread::hardware_concurrency());
    n = std::max(1, std::min(n, rows));
    if (n == 1) {
        fn(0, rows);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (int i = 0; i < n; ++i) {
        int y0 = static_cast<int>(static_cast<long long>(rows) * i / n);
        int y1 = static_cast<int>(static_cast<long long>(rows) * (i + 1) / n);
        pool.emplace_back([&fn, y0, y1] { fn(y0, y1); });
    }
    for (std::thread& t : pool) t.join();
}

}  // namespace fixmyplanet
