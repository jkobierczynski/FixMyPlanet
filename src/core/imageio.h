// SPDX-License-Identifier: GPL-3.0-or-later
// Image file reading and writing: TIFF, PNG, FITS (read/write), JPEG and BMP (read).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "image.h"

namespace fixmyplanet {

struct ImageFileInfo {
    std::string format;        // "TIFF", "PNG", "FITS", "JPEG", "BMP"
    int bitsPerSample = 0;
    bool floatingPoint = false;
    std::string dateObs;       // FITS DATE-OBS, if present
};

enum class SampleDepth { Auto, U8, U16, F32 };

struct SaveOptions {
    SampleDepth depth = SampleDepth::Auto;   // Auto = 16-bit integer
    std::string description;                 // stored as TIFF ImageDescription / FITS COMMENT / PNG text
    std::string dateObs;                     // FITS DATE-OBS
};

// Load an image; the format is detected from the file contents.
// Integer samples are scaled to 0..1. Returns false and sets `error` on failure.
bool loadImage(const std::string& path, Image& image, ImageFileInfo* info, std::string& error);

// Save an image; the format follows the file extension
// (.tif/.tiff, .png, .fit/.fits/.fts). Returns false and sets `error` on failure.
bool saveImage(const std::string& path, const Image& image, const SaveOptions& options, std::string& error);

// Whole-file helpers. Paths are UTF-8 on every platform.
bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error);
bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error);

// In-memory decoders/encoders (used by loadImage/saveImage and by the tests).
bool decodeTiff(const std::vector<uint8_t>& bytes, Image& image, ImageFileInfo* info, std::string& error);
bool decodeFits(const std::vector<uint8_t>& bytes, Image& image, ImageFileInfo* info, std::string& error);
std::vector<uint8_t> encodeTiff(const Image& image, const SaveOptions& options);
std::vector<uint8_t> encodeFits(const Image& image, const SaveOptions& options);

}  // namespace fixmyplanet
