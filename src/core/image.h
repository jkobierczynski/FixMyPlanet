// SPDX-License-Identifier: GPL-3.0-or-later
// Floating point image container and resampling.
#pragma once

#include <cstddef>
#include <vector>

namespace fixmyplanet {

// Interleaved float image. Integer formats are scaled to 0..1 on load.
class Image {
public:
    Image() = default;
    Image(int width, int height, int channels, float fill = 0.0f)
        : w_(width), h_(height), c_(channels), data_(static_cast<size_t>(width) * height * channels, fill) {}

    int width() const { return w_; }
    int height() const { return h_; }
    int channels() const { return c_; }
    bool empty() const { return data_.empty(); }

    float* data() { return data_.data(); }
    const float* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }

    float& at(int x, int y, int ch = 0) { return data_[(static_cast<size_t>(y) * w_ + x) * c_ + ch]; }
    float at(int x, int y, int ch = 0) const { return data_[(static_cast<size_t>(y) * w_ + x) * c_ + ch]; }

    // Single-channel copy: the channel itself for mono, Rec.709 luma for colour.
    Image luminance() const;
    // One channel as a mono image.
    Image channel(int ch) const;

private:
    int w_ = 0, h_ = 0, c_ = 0;
    std::vector<float> data_;
};

enum class Interpolation { Bilinear, Bicubic, Lanczos3 };

// Sample all channels at (x, y); `out` must hold image.channels() values.
// Returns false (and writes zeros) when the point is outside the image.
bool sample(const Image& img, double x, double y, Interpolation mode, float* out);

// Separable Gaussian blur (edges replicated).
Image gaussianBlur(const Image& img, double sigma);

// Percentile (0..100) of one channel, optionally over every `step`-th pixel.
float percentile(const Image& img, int ch, double pct, int step = 1);

}  // namespace fixmyplanet
