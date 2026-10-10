// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal FITS reader and writer for 2-D mono and 3-plane colour images.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imageio.h"
#include "strutil.h"

namespace fixmyplanet {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// Value part of a header card ("KEYWORD = value / comment"), unquoted.
std::string cardValue(const std::string& card) {
    if (card.size() < 10 || card[8] != '=') return "";
    std::string v = card.substr(10);
    std::string t = trim(v);
    if (!t.empty() && t[0] == '\'') {
        std::string out;
        for (size_t i = 1; i < t.size(); ++i) {
            if (t[i] == '\'') {
                if (i + 1 < t.size() && t[i + 1] == '\'') { out += '\''; ++i; }
                else break;
            } else out += t[i];
        }
        return trim(out);
    }
    size_t slash = t.find('/');
    if (slash != std::string::npos) t = t.substr(0, slash);
    return trim(t);
}

void addCard(std::vector<uint8_t>& out, const std::string& text) {
    std::string card = text;
    card.resize(80, ' ');
    out.insert(out.end(), card.begin(), card.end());
}

std::string keyCard(const char* key, const std::string& value) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%-8s= %20s", key, value.c_str());
    return buf;
}

std::string strCard(const char* key, const std::string& value) {
    std::string v = value.substr(0, 60);
    std::string q;
    for (char ch : v) {
        q += ch;
        if (ch == '\'') q += '\'';
    }
    while (q.size() < 8) q += ' ';
    char buf[128];
    std::snprintf(buf, sizeof buf, "%-8s= '%s'", key, q.c_str());
    return buf;
}

}  // namespace

bool decodeFits(const std::vector<uint8_t>& bytes, Image& image, ImageFileInfo* info, std::string& error) {
    if (bytes.size() < 2880 || std::memcmp(bytes.data(), "SIMPLE  =", 9) != 0) {
        error = "FITS: not a FITS file";
        return false;
    }
    int bitpix = 0, naxis = 0;
    long axes[3] = {0, 0, 1};
    double bzero = 0.0, bscale = 1.0;
    std::string dateObs, rowOrder;
    size_t pos = 0;
    bool ended = false;
    while (!ended) {
        if (pos + 2880 > bytes.size()) { error = "FITS: header has no END card"; return false; }
        for (int i = 0; i < 36; ++i) {
            std::string card(reinterpret_cast<const char*>(bytes.data() + pos + i * 80), 80);
            std::string key = trim(card.substr(0, 8));
            if (key == "END") { ended = true; break; }
            std::string val = cardValue(card);
            if (key == "BITPIX") bitpix = std::atoi(val.c_str());
            else if (key == "NAXIS") naxis = std::atoi(val.c_str());
            else if (key == "NAXIS1") axes[0] = std::atol(val.c_str());
            else if (key == "NAXIS2") axes[1] = std::atol(val.c_str());
            else if (key == "NAXIS3") axes[2] = std::atol(val.c_str());
            else if (key == "BZERO") parseDouble(val, bzero);
            else if (key == "BSCALE") parseDouble(val, bscale);
            else if (key == "DATE-OBS") dateObs = val;
            else if (key == "ROWORDER") rowOrder = val;
        }
        pos += 2880;
    }
    if (naxis < 2 || naxis > 3) { error = "FITS: only 2-D images and 3-plane colour cubes are supported"; return false; }
    if (naxis == 2) axes[2] = 1;
    if (axes[0] <= 0 || axes[1] <= 0 || axes[0] > 65535 || axes[1] > 65535) { error = "FITS: bad image size"; return false; }
    if (axes[2] != 1 && axes[2] != 3) { error = "FITS: unsupported number of planes"; return false; }
    if (bitpix != 8 && bitpix != 16 && bitpix != 32 && bitpix != -32 && bitpix != -64) {
        error = "FITS: unsupported BITPIX";
        return false;
    }
    const int w = static_cast<int>(axes[0]), h = static_cast<int>(axes[1]), c = static_cast<int>(axes[2]);
    const size_t bytesPer = static_cast<size_t>(std::abs(bitpix) / 8);
    const size_t need = static_cast<size_t>(w) * h * c * bytesPer;
    if (bytes.size() - pos < need) { error = "FITS: image data is truncated"; return false; }
    if (static_cast<uint64_t>(w) * h > (1ull << 28)) { error = "FITS: image too large"; return false; }

    // FITS stores the bottom row first unless the writer says otherwise.
    std::string ro = rowOrder;
    std::transform(ro.begin(), ro.end(), ro.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    const bool topDown = ro == "TOP-DOWN";

    image = Image(w, h, c);
    const uint8_t* p = bytes.data() + pos;
    const bool isFloat = bitpix < 0;
    double maxVal = -1e300;
    for (int ch = 0; ch < c; ++ch)
        for (int row = 0; row < h; ++row) {
            int y = topDown ? row : h - 1 - row;
            for (int x = 0; x < w; ++x) {
                double v;
                if (bitpix == 8) v = p[0];
                else if (bitpix == 16) v = static_cast<int16_t>(static_cast<uint16_t>((p[0] << 8) | p[1]));
                else if (bitpix == 32)
                    v = static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                                             (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
                else if (bitpix == -32) {
                    uint32_t u = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                                 (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
                    float f;
                    std::memcpy(&f, &u, 4);
                    v = f;
                } else {
                    uint64_t u = 0;
                    for (int i = 0; i < 8; ++i) u = (u << 8) | p[i];
                    std::memcpy(&v, &u, 8);
                }
                p += bytesPer;
                v = v * bscale + bzero;
                if (!std::isfinite(v)) v = 0.0;
                if (v > maxVal) maxVal = v;
                image.at(x, y, ch) = static_cast<float>(v);
            }
        }

    // Bring the values to 0..1.
    double scale = 1.0;
    if (bitpix == 8) scale = 1.0 / 255.0;
    else if (bitpix == 16) scale = 1.0 / 65535.0;
    else if (bitpix == 32) scale = maxVal > 65535.5 ? 1.0 / 4294967295.0 : 1.0 / 65535.0;
    else if (maxVal > 65535.5) scale = 1.0 / maxVal;
    else if (maxVal > 1.0 + 1e-6) scale = 1.0 / 65535.0;
    if (scale != 1.0) {
        float* d = image.data();
        for (size_t i = 0; i < image.size(); ++i) d[i] = static_cast<float>(d[i] * scale);
    }
    if (info) {
        info->format = "FITS";
        info->bitsPerSample = std::abs(bitpix);
        info->floatingPoint = isFloat;
        info->dateObs = dateObs;
    }
    return true;
}

std::vector<uint8_t> encodeFits(const Image& image, const SaveOptions& options) {
    const int w = image.width(), h = image.height();
    const int c = image.channels() >= 3 ? 3 : 1;
    const bool isFloat = options.depth == SampleDepth::F32;
    const bool is8 = options.depth == SampleDepth::U8;
    const int bitpix = isFloat ? -32 : is8 ? 8 : 16;

    std::vector<uint8_t> out;
    addCard(out, keyCard("SIMPLE", "T"));
    addCard(out, keyCard("BITPIX", std::to_string(bitpix)));
    addCard(out, keyCard("NAXIS", c == 3 ? "3" : "2"));
    addCard(out, keyCard("NAXIS1", std::to_string(w)));
    addCard(out, keyCard("NAXIS2", std::to_string(h)));
    if (c == 3) addCard(out, keyCard("NAXIS3", "3"));
    if (bitpix == 16) {
        addCard(out, keyCard("BZERO", "32768"));
        addCard(out, keyCard("BSCALE", "1"));
    }
    addCard(out, strCard("ROWORDER", "BOTTOM-UP"));
    if (!options.dateObs.empty()) addCard(out, strCard("DATE-OBS", options.dateObs));
    addCard(out, strCard("SWCREATE", "fixmyplanet"));
    if (!options.description.empty()) {
        for (size_t i = 0; i < options.description.size() && i < 70 * 6; i += 70)
            addCard(out, "COMMENT " + options.description.substr(i, 70));
    }
    addCard(out, "END");
    while (out.size() % 2880) out.push_back(' ');

    const float* src = image.data();
    const int ic = image.channels();
    for (int ch = 0; ch < c; ++ch)
        for (int row = h - 1; row >= 0; --row)
            for (int x = 0; x < w; ++x) {
                float v = src[(static_cast<size_t>(row) * w + x) * ic + ch];
                if (isFloat) {
                    uint32_t u;
                    std::memcpy(&u, &v, 4);
                    for (int k = 3; k >= 0; --k) out.push_back(static_cast<uint8_t>((u >> (8 * k)) & 0xff));
                } else {
                    double cl = std::isfinite(v) ? std::min(1.0, std::max(0.0, static_cast<double>(v))) : 0.0;
                    if (is8) out.push_back(static_cast<uint8_t>(cl * 255.0 + 0.5));
                    else {
                        int q = static_cast<int>(cl * 65535.0 + 0.5) - 32768;
                        uint16_t u = static_cast<uint16_t>(static_cast<int16_t>(q));
                        out.push_back(static_cast<uint8_t>(u >> 8));
                        out.push_back(static_cast<uint8_t>(u & 0xff));
                    }
                }
            }
    while (out.size() % 2880) out.push_back(0);
    return out;
}

}  // namespace fixmyplanet
