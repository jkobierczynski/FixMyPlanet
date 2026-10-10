// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>

#include "lodepng.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fixmyplanet {

namespace {

std::filesystem::path toPath(const std::string& utf8) {
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

std::string extensionLower(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool decodePng(const std::vector<uint8_t>& bytes, Image& image, ImageFileInfo* info, std::string& error) {
    unsigned w = 0, h = 0;
    lodepng::State state;
    unsigned err = lodepng_inspect(&w, &h, &state, bytes.data(), bytes.size());
    if (err) { error = std::string("PNG: ") + lodepng_error_text(err); return false; }
    const LodePNGColorType srcType = state.info_png.color.colortype;
    const unsigned srcBits = state.info_png.color.bitdepth;
    const bool grey = srcType == LCT_GREY || srcType == LCT_GREY_ALPHA;
    // lodepng converts grey images to 8-bit grey only, so 16 bits are asked
    // for just where the file has them (then no conversion is involved).
    const unsigned bits = (grey && !(srcType == LCT_GREY && srcBits == 16)) ? 8 : 16;
    std::vector<unsigned char> px;
    err = lodepng::decode(px, w, h, bytes, grey ? LCT_GREY : LCT_RGB, bits);
    if (err) { error = std::string("PNG: ") + lodepng_error_text(err); return false; }
    const int c = grey ? 1 : 3;
    image = Image(static_cast<int>(w), static_cast<int>(h), c);
    float* d = image.data();
    const size_t n = static_cast<size_t>(w) * h * c;
    if (bits == 16)
        for (size_t i = 0; i < n; ++i) d[i] = static_cast<float>(((px[2 * i] << 8) | px[2 * i + 1]) / 65535.0);
    else
        for (size_t i = 0; i < n; ++i) d[i] = static_cast<float>(px[i] / 255.0);
    if (info) {
        info->format = "PNG";
        info->bitsPerSample = static_cast<int>(srcBits);
        info->floatingPoint = false;
    }
    return true;
}

bool decodeStb(const std::vector<uint8_t>& bytes, Image& image, ImageFileInfo* info, std::string& error) {
    int w = 0, h = 0, comp = 0;
    if (bytes.size() > 0x7fffffff) { error = "file too large"; return false; }
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp)) {
        error = "unrecognised image format";
        return false;
    }
    // A header may claim any size; refuse what the file could not hold.
    if (w <= 0 || h <= 0 || static_cast<uint64_t>(w) * h > (1ull << 28) ||
        static_cast<uint64_t>(w) * h > static_cast<uint64_t>(bytes.size()) * 4096 + 65536) {
        error = "the image size does not fit the file";
        return false;
    }
    const int want = comp >= 3 ? 3 : 1;
    struct StbFree { void operator()(unsigned char* p) const { stbi_image_free(p); } };
    std::unique_ptr<unsigned char, StbFree> px(stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, want));
    if (!px) { error = std::string("image decode failed: ") + stbi_failure_reason(); return false; }
    if (w <= 0 || h <= 0) { error = "empty image"; return false; }
    image = Image(w, h, want);
    float* d = image.data();
    const size_t n = static_cast<size_t>(w) * h * want;
    for (size_t i = 0; i < n; ++i) d[i] = px.get()[i] / 255.0f;
    if (info) {
        info->format = bytes.size() > 1 && bytes[0] == 'B' && bytes[1] == 'M' ? "BMP" : "JPEG";
        info->bitsPerSample = 8;
        info->floatingPoint = false;
    }
    return true;
}

}  // namespace

bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
    // Path conversion can throw (invalid UTF-8 on Windows) and so can the
    // allocation; neither may escape.
    try {
        const std::filesystem::path p = toPath(path);
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) { error = path + " is a folder, not a file"; return false; }
        std::ifstream f(p, std::ios::binary);
        if (!f) { error = "cannot open " + path; return false; }
        f.seekg(0, std::ios::end);
        std::streamoff len = f.tellg();
        if (len < 0 || len > (std::streamoff(1) << 32)) { error = "cannot read " + path; return false; }
        f.seekg(0, std::ios::beg);
        bytes.resize(static_cast<size_t>(len));
        if (len > 0) f.read(reinterpret_cast<char*>(bytes.data()), len);
        if (!f) { error = "cannot read " + path; return false; }
        return true;
    } catch (const std::bad_alloc&) {
        error = "not enough memory to read " + path;
    } catch (const std::exception& e) {
        error = "cannot read " + path + ": " + e.what();
    }
    return false;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error) {
    try {
        std::ofstream f(toPath(path), std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot create " + path; return false; }
        if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (!f) { error = "cannot write " + path; return false; }
        return true;
    } catch (const std::exception& e) {
        error = "cannot write " + path + ": " + e.what();
    }
    return false;
}

bool loadImage(const std::string& path, Image& image, ImageFileInfo* info, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) return false;
    if (bytes.size() < 8) { error = path + ": file too short to be an image"; return false; }
    bool ok;
    static const uint8_t pngSig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    try {
        if (std::memcmp(bytes.data(), pngSig, 8) == 0) ok = decodePng(bytes, image, info, error);
        else if ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M')) ok = decodeTiff(bytes, image, info, error);
        else if (std::memcmp(bytes.data(), "SIMPLE", 6) == 0) ok = decodeFits(bytes, image, info, error);
        else ok = decodeStb(bytes, image, info, error);
    } catch (const std::bad_alloc&) {
        ok = false;
        error = "not enough memory for this image";
    }
    if (!ok) error = path + ": " + error;
    return ok;
}

bool saveImage(const std::string& path, const Image& image, const SaveOptions& options, std::string& error) {
    if (image.empty()) { error = "nothing to save"; return false; }
    std::string ext = extensionLower(path);
    if (ext == "tif" || ext == "tiff") return writeFile(path, encodeTiff(image, options), error);
    if (ext == "fit" || ext == "fits" || ext == "fts") return writeFile(path, encodeFits(image, options), error);
    if (ext == "png") {
        if (options.depth == SampleDepth::F32) { error = "PNG cannot store floating point samples"; return false; }
        const bool eight = options.depth == SampleDepth::U8;
        const int c = image.channels() >= 3 ? 3 : 1;
        const int ic = image.channels();
        const size_t n = static_cast<size_t>(image.width()) * image.height();
        std::vector<unsigned char> px;
        px.reserve(n * c * (eight ? 1 : 2));
        const float* d = image.data();
        for (size_t i = 0; i < n; ++i)
            for (int ch = 0; ch < c; ++ch) {
                float v = d[i * ic + ch];
                double cl = std::isfinite(v) ? std::min(1.0, std::max(0.0, static_cast<double>(v))) : 0.0;
                if (eight) px.push_back(static_cast<unsigned char>(cl * 255.0 + 0.5));
                else {
                    unsigned q = static_cast<unsigned>(cl * 65535.0 + 0.5);
                    px.push_back(static_cast<unsigned char>(q >> 8));
                    px.push_back(static_cast<unsigned char>(q & 0xff));
                }
            }
        lodepng::State state;
        state.info_raw.colortype = c == 3 ? LCT_RGB : LCT_GREY;
        state.info_raw.bitdepth = eight ? 8 : 16;
        state.info_png.color.colortype = state.info_raw.colortype;
        state.info_png.color.bitdepth = state.info_raw.bitdepth;
        state.encoder.auto_convert = 0;
        if (!options.description.empty()) lodepng_add_text(&state.info_png, "Description", options.description.c_str());
        std::vector<unsigned char> out;
        unsigned err = lodepng::encode(out, px, static_cast<unsigned>(image.width()), static_cast<unsigned>(image.height()), state);
        if (err) { error = std::string("PNG: ") + lodepng_error_text(err); return false; }
        return writeFile(path, out, error);
    }
    error = "unsupported output format '." + ext + "' (use .tif, .png or .fits)";
    return false;
}

}  // namespace fixmyplanet
