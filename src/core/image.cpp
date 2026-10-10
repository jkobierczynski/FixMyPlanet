// SPDX-License-Identifier: GPL-3.0-or-later
#include "image.h"

#include <algorithm>
#include <cmath>

namespace fixmyplanet {

Image Image::luminance() const {
    if (c_ == 1) return *this;
    Image out(w_, h_, 1);
    const float* src = data_.data();
    float* dst = out.data();
    const size_t n = static_cast<size_t>(w_) * h_;
    if (c_ >= 3) {
        for (size_t i = 0; i < n; ++i)
            dst[i] = 0.2126f * src[i * c_] + 0.7152f * src[i * c_ + 1] + 0.0722f * src[i * c_ + 2];
    } else {
        for (size_t i = 0; i < n; ++i) dst[i] = src[i * c_];
    }
    return out;
}

Image Image::channel(int ch) const {
    Image out(w_, h_, 1);
    const size_t n = static_cast<size_t>(w_) * h_;
    for (size_t i = 0; i < n; ++i) out.data()[i] = data_[i * c_ + ch];
    return out;
}

namespace {

inline double cubicWeight(double t) {
    // Catmull-Rom kernel.
    t = std::fabs(t);
    if (t < 1.0) return 1.5 * t * t * t - 2.5 * t * t + 1.0;
    if (t < 2.0) return -0.5 * t * t * t + 2.5 * t * t - 4.0 * t + 2.0;
    return 0.0;
}

inline double lanczosWeight(double t) {
    t = std::fabs(t);
    if (t < 1e-9) return 1.0;
    if (t >= 3.0) return 0.0;
    const double pi = 3.14159265358979323846;
    double a = pi * t;
    return 3.0 * std::sin(a) * std::sin(a / 3.0) / (a * a);
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

bool sample(const Image& img, double x, double y, Interpolation mode, float* out) {
    const int w = img.width(), h = img.height(), c = img.channels();
    if (!(x >= -0.5 && y >= -0.5 && x <= w - 0.5 && y <= h - 0.5)) {
        for (int k = 0; k < c; ++k) out[k] = 0.0f;
        return false;
    }
    const float* d = img.data();
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    double fx = x - x0, fy = y - y0;

    if (mode == Interpolation::Bilinear) {
        int xa = clampi(x0, 0, w - 1), xb = clampi(x0 + 1, 0, w - 1);
        int ya = clampi(y0, 0, h - 1), yb = clampi(y0 + 1, 0, h - 1);
        for (int k = 0; k < c; ++k) {
            double v00 = d[(static_cast<size_t>(ya) * w + xa) * c + k];
            double v10 = d[(static_cast<size_t>(ya) * w + xb) * c + k];
            double v01 = d[(static_cast<size_t>(yb) * w + xa) * c + k];
            double v11 = d[(static_cast<size_t>(yb) * w + xb) * c + k];
            out[k] = static_cast<float>((v00 * (1 - fx) + v10 * fx) * (1 - fy) + (v01 * (1 - fx) + v11 * fx) * fy);
        }
        return true;
    }

    const int radius = mode == Interpolation::Bicubic ? 2 : 3;
    double wx[6], wy[6];
    int ix[6], iy[6];
    double sx = 0, sy = 0;
    const int taps = 2 * radius;
    for (int i = 0; i < taps; ++i) {
        int o = i - radius + 1;
        double tx = fx - o, ty = fy - o;
        wx[i] = mode == Interpolation::Bicubic ? cubicWeight(tx) : lanczosWeight(tx);
        wy[i] = mode == Interpolation::Bicubic ? cubicWeight(ty) : lanczosWeight(ty);
        sx += wx[i];
        sy += wy[i];
        ix[i] = clampi(x0 + o, 0, w - 1);
        iy[i] = clampi(y0 + o, 0, h - 1);
    }
    for (int i = 0; i < taps; ++i) {
        wx[i] /= sx;
        wy[i] /= sy;
    }
    for (int k = 0; k < c; ++k) {
        double acc = 0;
        for (int j = 0; j < taps; ++j) {
            const float* row = d + static_cast<size_t>(iy[j]) * w * c + k;
            double r = 0;
            for (int i = 0; i < taps; ++i) r += wx[i] * row[static_cast<size_t>(ix[i]) * c];
            acc += wy[j] * r;
        }
        out[k] = static_cast<float>(acc);
    }
    return true;
}

namespace {

// One pass of a box filter of the given radius along x or y (edges replicated),
// done with a running sum so the cost does not depend on the radius.
void boxPass(const Image& src, Image& dst, int radius, bool horizontal) {
    const int w = src.width(), h = src.height(), c = src.channels();
    const int len = horizontal ? w : h, lines = horizontal ? h : w;
    const double norm = 1.0 / (2 * radius + 1);
    for (int line = 0; line < lines; ++line)
        for (int ch = 0; ch < c; ++ch) {
            auto get = [&](int i) -> double {
                i = clampi(i, 0, len - 1);
                return horizontal ? src.at(i, line, ch) : src.at(line, i, ch);
            };
            double acc = 0;
            for (int i = -radius; i <= radius; ++i) acc += get(i);
            for (int i = 0; i < len; ++i) {
                float v = static_cast<float>(acc * norm);
                if (horizontal) dst.at(i, line, ch) = v;
                else dst.at(line, i, ch) = v;
                acc += get(i + radius + 1) - get(i - radius);
            }
        }
}

}  // namespace

Image gaussianBlur(const Image& img, double sigma) {
    if (sigma <= 0.05 || img.empty()) return img;
    const int w = img.width(), h = img.height(), c = img.channels();
    if (sigma > 4.0) {
        // Three box passes approximate a Gaussian closely and cost O(pixels).
        int radius = static_cast<int>(std::floor(0.5 * (std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) + 0.5));
        if (radius < 1) radius = 1;
        Image a = img, b(w, h, c);
        for (int pass = 0; pass < 3; ++pass) {
            boxPass(a, b, radius, true);
            boxPass(b, a, radius, false);
        }
        return a;
    }
    int r = static_cast<int>(std::ceil(sigma * 3.0));
    std::vector<float> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; ++i) {
        k[i + r] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
        sum += k[i + r];
    }
    for (float& v : k) v = static_cast<float>(v / sum);

    Image tmp(w, h, c), out(w, h, c);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0;
                for (int i = -r; i <= r; ++i) acc += k[i + r] * img.at(clampi(x + i, 0, w - 1), y, ch);
                tmp.at(x, y, ch) = acc;
            }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0;
                for (int i = -r; i <= r; ++i) acc += k[i + r] * tmp.at(x, clampi(y + i, 0, h - 1), ch);
                out.at(x, y, ch) = acc;
            }
    return out;
}

float percentile(const Image& img, int ch, double pct, int step) {
    if (img.empty()) return 0.0f;
    if (step < 1) step = 1;
    std::vector<float> v;
    const size_t n = static_cast<size_t>(img.width()) * img.height();
    v.reserve(n / step + 1);
    const float* d = img.data();
    const int c = img.channels();
    for (size_t i = 0; i < n; i += step) v.push_back(d[i * c + ch]);
    size_t k = static_cast<size_t>(std::clamp(pct / 100.0, 0.0, 1.0) * (v.size() - 1) + 0.5);
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

}  // namespace fixmyplanet
