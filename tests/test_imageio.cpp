// SPDX-License-Identifier: GPL-3.0-or-later
// Image readers checked against files written by tifffile, Pillow and astropy
// (see make_fixtures.py), and writers checked by round trip.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "imageio.h"
#include "testing.h"

using namespace fixmyplanet;

// The library takes UTF-8 paths; string() would give the ANSI code page on Windows.
static std::string u8(const std::filesystem::path& p) { return p.u8string(); }

static double expect8(int x, int y, int c) { return ((x * 7 + y * 13 + c * 29) % 251) / 255.0; }
static double expect16(int x, int y, int c) { return ((x * 701 + y * 1303 + c * 2909) % 65521) / 65535.0; }
static double expectF(int x, int y, int c) { return ((x * 7 + y * 13 + c * 29) % 251) / 250.0; }

static void checkFile(const char* name, int channels, double (*expect)(int, int, int), double tol, int bits) {
    Image img;
    ImageFileInfo info;
    std::string err;
    bool ok = loadImage(std::string(FIXMYPLANET_TEST_DATA) + "/" + name, img, &info, err);
    if (!ok) std::printf("  %s: %s\n", name, err.c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK(img.width() == 37 && img.height() == 23 && img.channels() == channels);
    CHECK(info.bitsPerSample == bits);
    double worst = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            for (int c = 0; c < img.channels(); ++c) worst = std::max(worst, std::fabs(img.at(x, y, c) - expect(x, y, c)));
    if (worst > tol) std::printf("  %s: worst pixel error %g\n", name, worst);
    CHECK(worst <= tol);
}

int main() {
    const double t = 1e-6;
    checkFile("gray16_none.tif", 1, expect16, t, 16);
    checkFile("gray16_lzw_pred.tif", 1, expect16, t, 16);
    checkFile("gray16_strips.tif", 1, expect16, t, 16);
    checkFile("gray16_tiled_lzw.tif", 1, expect16, t, 16);
    checkFile("rgb16_deflate_pred.tif", 3, expect16, t, 16);
    checkFile("rgb16_planar_be.tif", 3, expect16, t, 16);
    checkFile("rgb8_lzw.tif", 3, expect8, t, 8);
    checkFile("rgba8.tif", 3, expect8, t, 8);
    checkFile("gray8_packbits.tif", 1, expect8, t, 8);
    checkFile("gray_f32.tif", 1, expectF, t, 32);
    checkFile("gray8.png", 1, expect8, t, 8);
    checkFile("rgb8.png", 3, expect8, t, 8);
    checkFile("gray16.png", 1, expect16, t, 16);
    checkFile("rgb8.bmp", 3, expect8, t, 8);
    checkFile("gray16.fits", 1, expect16, t, 16);
    checkFile("gray16_topdown.fits", 1, expect16, t, 16);
    checkFile("rgb16.fits", 3, expect16, t, 16);
    checkFile("gray_f32.fits", 1, expectF, t, 32);

    {
        // DATE-OBS is passed on.
        Image img;
        ImageFileInfo info;
        std::string err;
        CHECK(loadImage(std::string(FIXMYPLANET_TEST_DATA) + "/gray16.fits", img, &info, err));
        CHECK_EQ_STR(info.dateObs, "2026-01-10T22:03:30.5");
        CHECK_EQ_STR(info.format, "FITS");
    }

    // Round trips through our own writers.
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "fixmyplanet_test_imageio";
    std::filesystem::create_directories(tmp);
    for (int channels : {1, 3}) {
        Image src(41, 29, channels);
        for (int y = 0; y < src.height(); ++y)
            for (int x = 0; x < src.width(); ++x)
                for (int c = 0; c < channels; ++c) src.at(x, y, c) = static_cast<float>(expect16(x, y, c));
        struct Case { const char* ext; SampleDepth depth; double tol; };
        const Case cases[] = {
            {"tif", SampleDepth::U16, 0.6 / 65535}, {"tif", SampleDepth::U8, 0.6 / 255}, {"tif", SampleDepth::F32, 1e-7},
            {"png", SampleDepth::U16, 0.6 / 65535}, {"png", SampleDepth::U8, 0.6 / 255},
            {"fits", SampleDepth::U16, 0.6 / 65535}, {"fits", SampleDepth::F32, 1e-7}, {"fits", SampleDepth::U8, 0.6 / 255},
        };
        int n = 0;
        for (const Case& cs : cases) {
            std::string path = u8(tmp / ("rt" + std::to_string(channels) + "_" + std::to_string(n++) + "." + cs.ext));
            SaveOptions so;
            so.depth = cs.depth;
            so.description = "fixmyplanet test; Jupiter; CM II 85.3";
            so.dateObs = "2026-01-10T22:08:00.0Z";
            std::string err;
            CHECK(saveImage(path, src, so, err));
            Image back;
            ImageFileInfo info;
            bool ok = loadImage(path, back, &info, err);
            if (!ok) std::printf("  %s: %s\n", path.c_str(), err.c_str());
            CHECK(ok);
            if (!ok) continue;
            CHECK(back.width() == src.width() && back.height() == src.height() && back.channels() == channels);
            double worst = 0;
            for (size_t i = 0; i < src.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(src.data()[i] - back.data()[i])));
            if (worst > cs.tol) std::printf("  %s: worst error %g\n", path.c_str(), worst);
            CHECK(worst <= cs.tol);
        }
    }
    {
        // Values outside 0..1 and NaN are clamped on integer output.
        Image src(4, 4, 1);
        src.at(0, 0) = -0.5f;
        src.at(1, 0) = 1.7f;
        src.at(2, 0) = std::nanf("");
        std::string path = u8(tmp / "clamp.tif"), err;
        CHECK(saveImage(path, src, SaveOptions{}, err));
        Image back;
        CHECK(loadImage(path, back, nullptr, err));
        CHECK_NEAR(back.at(0, 0), 0.0, 1e-9);
        CHECK_NEAR(back.at(1, 0), 1.0, 1e-9);
        CHECK_NEAR(back.at(2, 0), 0.0, 1e-9);
    }

    // Errors are reported, not crashes.
    {
        Image img;
        std::string err;
        CHECK(!loadImage(u8(tmp / "does_not_exist.tif"), img, nullptr, err));
        CHECK(!err.empty());
        CHECK(!saveImage(u8(tmp / "x.jpg"), Image(4, 4, 1), SaveOptions{}, err));
        CHECK(!saveImage(u8(tmp / "x.tif"), Image(), SaveOptions{}, err));
        SaveOptions so;
        so.depth = SampleDepth::F32;
        CHECK(!saveImage(u8(tmp / "x.png"), Image(4, 4, 1), so, err));
        // A folder is not an image.
        err.clear();
        CHECK(!loadImage(u8(tmp), img, nullptr, err));
        CHECK(!err.empty());
        // Not-a-number and infinite samples in a floating point file become 0.
        Image bad(8, 8, 1, 0.5f);
        bad.at(1, 1) = std::nanf("");
        bad.at(2, 2) = HUGE_VALF;
        CHECK(saveImage(u8(tmp / "nan.tif"), bad, so, err));
        Image back;
        CHECK(loadImage(u8(tmp / "nan.tif"), back, nullptr, err));
        CHECK(back.at(1, 1) == 0.0f && back.at(2, 2) == 0.0f && back.at(3, 3) == 0.5f);
        // A header claiming far more pixels than the file holds is refused.
        std::vector<uint8_t> bmp;
        CHECK(readFile(std::string(FIXMYPLANET_TEST_DATA) + "/rgb8.bmp", bmp, err));
        bmp.resize(54);
        for (int k = 0; k < 4; ++k) {
            bmp[18 + k] = static_cast<uint8_t>((12000 >> (8 * k)) & 0xff);  // width
            bmp[22 + k] = static_cast<uint8_t>((12000 >> (8 * k)) & 0xff);  // height
        }
        CHECK(writeFile(u8(tmp / "huge.bmp"), bmp, err));
        CHECK(!loadImage(u8(tmp / "huge.bmp"), back, nullptr, err));
    }
    {
        // Truncated and corrupted files: every prefix of a valid file, and the
        // file with single bytes flipped, must be rejected or read without a crash.
        for (const char* name : {"gray16_lzw_pred.tif", "rgb16_deflate_pred.tif", "gray16_tiled_lzw.tif", "gray16.fits", "gray8_packbits.tif"}) {
            std::vector<uint8_t> bytes;
            std::string err;
            CHECK(readFile(std::string(FIXMYPLANET_TEST_DATA) + "/" + name, bytes, err));
            const bool isFits = std::string(name).find(".fits") != std::string::npos;
            for (size_t len = 0; len < bytes.size(); len += (len < 300 ? 1 : 37)) {
                std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + len);
                Image img;
                if (isFits) decodeFits(cut, img, nullptr, err);
                else decodeTiff(cut, img, nullptr, err);
            }
            for (size_t pos = 0; pos < bytes.size(); pos += (pos < 400 ? 1 : 11)) {
                std::vector<uint8_t> bad = bytes;
                bad[pos] = static_cast<uint8_t>(bad[pos] ^ 0xFF);
                Image img;
                if (isFits) decodeFits(bad, img, nullptr, err);
                else decodeTiff(bad, img, nullptr, err);
                bad[pos] = static_cast<uint8_t>(bytes[pos] + 1);
                if (isFits) decodeFits(bad, img, nullptr, err);
                else decodeTiff(bad, img, nullptr, err);
            }
        }
        CHECK(true);
    }

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    return testing::finish("test_imageio");
}
