// SPDX-License-Identifier: GPL-3.0-or-later
// TIFF reader and writer.
//
// Reader: classic TIFF (not BigTIFF), first image in the file; strips or tiles;
// compression none, LZW, Deflate, PackBits; horizontal predictor; 8/16/32-bit
// unsigned or signed integers and 32/64-bit floats; 1 to 4 samples per pixel,
// chunky or planar. Palette images are not supported.
// Writer: uncompressed little-endian, one strip, 8/16-bit integer or 32-bit float.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "imageio.h"
#include "lodepng.h"

namespace fixmyplanet {

namespace {

struct Reader {
    const std::vector<uint8_t>& b;
    bool little = true;

    bool has(size_t off, size_t n) const { return off <= b.size() && n <= b.size() - off; }
    uint16_t u16(size_t off) const {
        return little ? static_cast<uint16_t>(b[off] | (b[off + 1] << 8))
                      : static_cast<uint16_t>((b[off] << 8) | b[off + 1]);
    }
    uint32_t u32(size_t off) const {
        return little ? (static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
                         (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24))
                      : ((static_cast<uint32_t>(b[off]) << 24) | (static_cast<uint32_t>(b[off + 1]) << 16) |
                         (static_cast<uint32_t>(b[off + 2]) << 8) | static_cast<uint32_t>(b[off + 3]));
    }
};

// Read the values of one IFD entry as unsigned integers (types BYTE, SHORT, LONG).
bool entryValues(const Reader& r, size_t entry, std::vector<uint32_t>& out) {
    uint16_t type = r.u16(entry + 2);
    uint32_t count = r.u32(entry + 4);
    size_t unit = type == 1 ? 1 : type == 3 ? 2 : type == 4 ? 4 : 0;
    if (unit == 0 || count == 0 || count > (1u << 26)) return false;
    size_t total = unit * count;
    size_t off = total <= 4 ? entry + 8 : r.u32(entry + 8);
    if (!r.has(off, total)) return false;
    out.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (unit == 1) out[i] = r.b[off + i];
        else if (unit == 2) out[i] = r.u16(off + 2 * i);
        else out[i] = r.u32(off + 4 * i);
    }
    return true;
}

bool lzwDecode(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expected) {
    static const int kMax = 4096;
    std::vector<uint16_t> prefix(kMax);
    std::vector<uint8_t> suffix(kMax), first(kMax);
    std::vector<uint32_t> length(kMax);
    for (int i = 0; i < 256; ++i) {
        prefix[i] = 0;
        suffix[i] = first[i] = static_cast<uint8_t>(i);
        length[i] = 1;
    }
    out.clear();
    out.reserve(expected);
    int bits = 9, next = 258, old = -1;
    uint32_t acc = 0;
    int nacc = 0;
    size_t pos = 0;
    while (out.size() < expected) {
        while (nacc < bits) {
            if (pos >= n) return !out.empty();  // truncated stream: keep what we have
            acc = (acc << 8) | src[pos++];
            nacc += 8;
        }
        int code = static_cast<int>((acc >> (nacc - bits)) & ((1u << bits) - 1));
        nacc -= bits;
        if (code == 256) {
            bits = 9;
            next = 258;
            old = -1;
            continue;
        }
        if (code == 257) break;
        if (old < 0) {
            if (code >= 256) return false;
            out.push_back(static_cast<uint8_t>(code));
            old = code;
            continue;
        }
        if (code > next || code >= kMax) return false;
        if (next < kMax) {
            // New table entry: string(old) + first char of the string being output.
            uint8_t firstChar = code < next ? first[code] : first[old];
            prefix[next] = static_cast<uint16_t>(old);
            suffix[next] = firstChar;
            first[next] = first[old];
            length[next] = length[old] + 1;
        } else if (code == next) {
            return false;
        }
        // Output string(code), written back to front.
        uint32_t len = length[code];
        size_t base = out.size();
        out.resize(base + len);
        int c = code;
        for (uint32_t i = len; i-- > 0;) {
            out[base + i] = suffix[c];
            c = prefix[c];
        }
        if (next < kMax) ++next;
        if (next + 1 >= (1 << bits) && bits < 12) ++bits;
        old = code;
    }
    return true;
}

bool packBitsDecode(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expected) {
    out.clear();
    out.reserve(expected);
    size_t pos = 0;
    while (pos < n && out.size() < expected) {
        int8_t h = static_cast<int8_t>(src[pos++]);
        if (h >= 0) {
            size_t cnt = static_cast<size_t>(h) + 1;
            if (pos + cnt > n) return false;
            out.insert(out.end(), src + pos, src + pos + cnt);
            pos += cnt;
        } else if (h != -128) {
            size_t cnt = static_cast<size_t>(1 - h);
            if (pos >= n) return false;
            out.insert(out.end(), cnt, src[pos++]);
        }
    }
    return true;
}

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xff));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xff));
}

}  // namespace

bool decodeTiff(const std::vector<uint8_t>& bytes, Image& image, ImageFileInfo* info, std::string& error) {
    Reader r{bytes};
    if (bytes.size() < 8) { error = "TIFF: file too short"; return false; }
    if (bytes[0] == 'I' && bytes[1] == 'I') r.little = true;
    else if (bytes[0] == 'M' && bytes[1] == 'M') r.little = false;
    else { error = "TIFF: bad byte order mark"; return false; }
    uint16_t magic = r.u16(2);
    if (magic == 43) { error = "TIFF: BigTIFF files are not supported"; return false; }
    if (magic != 42) { error = "TIFF: bad magic number"; return false; }
    size_t ifd = r.u32(4);
    if (!r.has(ifd, 2)) { error = "TIFF: bad directory offset"; return false; }
    uint16_t nEntries = r.u16(ifd);
    if (!r.has(ifd + 2, static_cast<size_t>(nEntries) * 12)) { error = "TIFF: truncated directory"; return false; }

    uint32_t width = 0, height = 0, compression = 1, photometric = 1, spp = 1, rowsPerStrip = 0xffffffffu;
    uint32_t planar = 1, predictor = 1, sampleFormat = 1, tileW = 0, tileH = 0;
    std::vector<uint32_t> bitsPer{1}, offsets, counts, tileOffsets, tileCounts, tmp;
    for (uint16_t i = 0; i < nEntries; ++i) {
        size_t e = ifd + 2 + static_cast<size_t>(i) * 12;
        uint16_t tag = r.u16(e);
        switch (tag) {
            case 256: if (entryValues(r, e, tmp)) width = tmp[0]; break;
            case 257: if (entryValues(r, e, tmp)) height = tmp[0]; break;
            case 258: entryValues(r, e, bitsPer); break;
            case 259: if (entryValues(r, e, tmp)) compression = tmp[0]; break;
            case 262: if (entryValues(r, e, tmp)) photometric = tmp[0]; break;
            case 273: entryValues(r, e, offsets); break;
            case 277: if (entryValues(r, e, tmp)) spp = tmp[0]; break;
            case 278: if (entryValues(r, e, tmp)) rowsPerStrip = tmp[0]; break;
            case 279: entryValues(r, e, counts); break;
            case 284: if (entryValues(r, e, tmp)) planar = tmp[0]; break;
            case 317: if (entryValues(r, e, tmp)) predictor = tmp[0]; break;
            case 322: if (entryValues(r, e, tmp)) tileW = tmp[0]; break;
            case 323: if (entryValues(r, e, tmp)) tileH = tmp[0]; break;
            case 324: entryValues(r, e, tileOffsets); break;
            case 325: entryValues(r, e, tileCounts); break;
            case 339: if (entryValues(r, e, tmp)) sampleFormat = tmp[0]; break;
            default: break;
        }
    }

    if (width == 0 || height == 0 || width > 65535 || height > 65535) { error = "TIFF: bad image size"; return false; }
    if (spp < 1 || spp > 8) { error = "TIFF: unsupported number of samples per pixel"; return false; }
    if (bitsPer.empty()) { error = "TIFF: missing BitsPerSample"; return false; }
    uint32_t bits = bitsPer[0];
    for (uint32_t v : bitsPer)
        if (v != bits) { error = "TIFF: samples with different bit depths are not supported"; return false; }
    if (photometric == 3) { error = "TIFF: palette images are not supported"; return false; }
    if (photometric > 2) { error = "TIFF: unsupported photometric interpretation"; return false; }
    bool isFloat = sampleFormat == 3;
    bool isSigned = sampleFormat == 2;
    if (isFloat ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 32)) {
        error = "TIFF: unsupported bit depth " + std::to_string(bits);
        return false;
    }
    if (compression != 1 && compression != 5 && compression != 8 && compression != 32946 && compression != 32773) {
        error = "TIFF: unsupported compression " + std::to_string(compression);
        return false;
    }
    if (predictor != 1 && predictor != 2) { error = "TIFF: unsupported predictor"; return false; }
    if (planar != 1 && planar != 2) { error = "TIFF: bad planar configuration"; return false; }

    const bool tiled = tileW > 0 && tileH > 0 && !tileOffsets.empty();
    if (tiled) {
        offsets = tileOffsets;
        counts = tileCounts;
    }
    if (offsets.empty()) { error = "TIFF: no image data"; return false; }
    if (counts.size() != offsets.size()) {
        // Tolerate a missing byte-count tag for uncompressed single-strip files.
        if (compression == 1 && offsets.size() == 1 && bytes.size() > offsets[0]) counts = {static_cast<uint32_t>(bytes.size() - offsets[0])};
        else { error = "TIFF: strip offsets and byte counts do not match"; return false; }
    }

    const uint32_t bytesPerSample = bits / 8;
    const uint32_t planes = planar == 2 ? spp : 1;          // separately stored planes
    const uint32_t chunkSamples = planar == 2 ? 1 : spp;    // samples per pixel within a chunk
    const uint32_t blockW = tiled ? tileW : width;
    uint32_t blockH = tiled ? tileH : std::min(rowsPerStrip, height);
    if (blockH == 0) blockH = height;
    const uint32_t blocksAcross = tiled ? (width + tileW - 1) / tileW : 1;
    const uint32_t blocksDown = (height + blockH - 1) / blockH;
    const size_t blocksPerPlane = static_cast<size_t>(blocksAcross) * blocksDown;
    if (offsets.size() < blocksPerPlane * planes) { error = "TIFF: too few strips or tiles"; return false; }

    // Refuse sizes that the file cannot possibly hold, before allocating:
    // a damaged header must not make us ask for gigabytes. Deflate and LZW
    // top out near 1000:1, uncompressed data needs every byte.
    const uint64_t rawBytes = static_cast<uint64_t>(width) * height * spp * bytesPerSample;
    const uint64_t maxRatio = compression == 1 ? 1 : 1100;
    if (static_cast<uint64_t>(width) * height > (1ull << 28) || rawBytes > static_cast<uint64_t>(bytes.size()) * maxRatio) {
        error = "TIFF: the image size does not fit the file";
        return false;
    }
    // The same for one strip or tile (tile sizes come from the file too).
    const uint64_t blockBytes = static_cast<uint64_t>(blockW) * blockH * chunkSamples * bytesPerSample;
    if (blockW > 65536 || blockH > 65536 || blockBytes > (1ull << 30) ||
        blockBytes > static_cast<uint64_t>(bytes.size()) * maxRatio + 65536) {
        error = "TIFF: the strip or tile size does not fit the file";
        return false;
    }
    const int outChannels = spp >= 3 ? 3 : 1;
    image = Image(static_cast<int>(width), static_cast<int>(height), outChannels);
    const size_t rowBytes = static_cast<size_t>(blockW) * chunkSamples * bytesPerSample;
    std::vector<uint8_t> buf;

    // Host is assumed little-endian or big-endian; samples are assembled bytewise.
    auto sampleValue = [&](const uint8_t* p) -> double {
        if (bytesPerSample == 1) return isSigned ? (static_cast<int8_t>(p[0]) + 128.0) / 255.0 : p[0] / 255.0;
        if (bytesPerSample == 2) {
            uint16_t v = r.little ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : static_cast<uint16_t>((p[0] << 8) | p[1]);
            return isSigned ? (static_cast<int16_t>(v) + 32768.0) / 65535.0 : v / 65535.0;
        }
        if (bytesPerSample == 4) {
            uint32_t v = r.little ? (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                     (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24))
                                  : ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                                     (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
            if (isFloat) {
                float f;
                std::memcpy(&f, &v, 4);
                return f;
            }
            return isSigned ? (static_cast<int32_t>(v) + 2147483648.0) / 4294967295.0 : v / 4294967295.0;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[r.little ? i : 7 - i]) << (8 * i);
        double dv;
        std::memcpy(&dv, &v, 8);
        return dv;
    };

    for (uint32_t plane = 0; plane < planes; ++plane) {
        for (size_t blk = 0; blk < blocksPerPlane; ++blk) {
            size_t idx = plane * blocksPerPlane + blk;
            size_t off = offsets[idx], cnt = counts[idx];
            if (!r.has(off, cnt)) { error = "TIFF: image data runs past the end of the file"; return false; }
            uint32_t by = static_cast<uint32_t>(blk / blocksAcross), bx = static_cast<uint32_t>(blk % blocksAcross);
            uint32_t y0 = by * blockH, x0 = bx * blockW;
            uint32_t rows = tiled ? blockH : std::min(blockH, height - y0);
            size_t expected = rowBytes * rows;

            const uint8_t* src = bytes.data() + off;
            if (compression == 1) {
                if (cnt < expected) { error = "TIFF: strip is shorter than expected"; return false; }
                buf.assign(src, src + expected);
            } else if (compression == 5) {
                if (!lzwDecode(src, cnt, buf, expected)) { error = "TIFF: corrupt LZW data"; return false; }
            } else if (compression == 32773) {
                if (!packBitsDecode(src, cnt, buf, expected)) { error = "TIFF: corrupt PackBits data"; return false; }
            } else {
                unsigned char* z = nullptr;
                size_t zn = 0;
                unsigned err = lodepng_zlib_decompress(&z, &zn, src, cnt, &lodepng_default_decompress_settings);
                struct Free { void operator()(unsigned char* p) const { std::free(p); } };
                std::unique_ptr<unsigned char, Free> guard(z);
                if (err) {
                    error = "TIFF: corrupt Deflate data";
                    return false;
                }
                buf.assign(z, z + zn);
            }
            if (buf.size() < expected) buf.resize(expected, 0);

            if (predictor == 2) {
                for (uint32_t row = 0; row < rows; ++row) {
                    uint8_t* p = buf.data() + rowBytes * row;
                    if (bytesPerSample == 1) {
                        for (size_t i = chunkSamples; i < rowBytes; ++i) p[i] = static_cast<uint8_t>(p[i] + p[i - chunkSamples]);
                    } else if (bytesPerSample == 2) {
                        size_t n = static_cast<size_t>(blockW) * chunkSamples;
                        for (size_t i = chunkSamples; i < n; ++i) {
                            uint8_t* a = p + 2 * i;
                            const uint8_t* q = p + 2 * (i - chunkSamples);
                            uint16_t va = r.little ? static_cast<uint16_t>(a[0] | (a[1] << 8)) : static_cast<uint16_t>((a[0] << 8) | a[1]);
                            uint16_t vq = r.little ? static_cast<uint16_t>(q[0] | (q[1] << 8)) : static_cast<uint16_t>((q[0] << 8) | q[1]);
                            uint16_t s = static_cast<uint16_t>(va + vq);
                            if (r.little) { a[0] = static_cast<uint8_t>(s & 0xff); a[1] = static_cast<uint8_t>(s >> 8); }
                            else { a[0] = static_cast<uint8_t>(s >> 8); a[1] = static_cast<uint8_t>(s & 0xff); }
                        }
                    } else {
                        error = "TIFF: predictor with this bit depth is not supported";
                        return false;
                    }
                }
            }

            for (uint32_t row = 0; row < rows; ++row) {
                uint32_t y = y0 + row;
                if (y >= height) break;
                const uint8_t* p = buf.data() + rowBytes * row;
                for (uint32_t col = 0; col < blockW; ++col) {
                    uint32_t x = x0 + col;
                    if (x >= width) break;
                    const uint8_t* px = p + static_cast<size_t>(col) * chunkSamples * bytesPerSample;
                    if (planar == 2) {
                        if (static_cast<int>(plane) < outChannels) image.at(x, y, plane) = static_cast<float>(sampleValue(px));
                    } else {
                        for (int ch = 0; ch < outChannels; ++ch)
                            image.at(x, y, ch) = static_cast<float>(sampleValue(px + static_cast<size_t>(ch) * bytesPerSample));
                    }
                }
            }
        }
    }

    if (isFloat) {
        // NaN and infinity would poison every statistic taken later.
        float* d = image.data();
        for (size_t i = 0; i < image.size(); ++i)
            if (!std::isfinite(d[i])) d[i] = 0.0f;
    }
    if (photometric == 0 && !isFloat) {
        float* d = image.data();
        for (size_t i = 0; i < image.size(); ++i) d[i] = 1.0f - d[i];
    }
    if (info) {
        info->format = "TIFF";
        info->bitsPerSample = static_cast<int>(bits);
        info->floatingPoint = isFloat;
    }
    return true;
}

std::vector<uint8_t> encodeTiff(const Image& image, const SaveOptions& options) {
    const uint32_t w = image.width(), h = image.height();
    const uint32_t spp = image.channels() >= 3 ? 3 : 1;
    SampleDepth depth = options.depth == SampleDepth::Auto ? SampleDepth::U16 : options.depth;
    const uint32_t bits = depth == SampleDepth::U8 ? 8 : depth == SampleDepth::U16 ? 16 : 32;
    const uint32_t bytesPer = bits / 8;
    const size_t dataBytes = static_cast<size_t>(w) * h * spp * bytesPer;

    std::string software = "fixmyplanet";
    // Values of four bytes or fewer would have to be stored inline; a
    // description that short is not worth the special case.
    std::string desc = options.description.size() >= 4 ? options.description : std::string();

    std::vector<uint8_t> out;
    out.reserve(dataBytes + 1024);
    out.push_back('I'); out.push_back('I');
    put16(out, 42);
    put32(out, 0);  // IFD offset, patched below

    const size_t dataOffset = out.size();
    out.resize(dataOffset + dataBytes);
    uint8_t* p = out.data() + dataOffset;
    const float* src = image.data();
    const int c = image.channels();
    const size_t n = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < n; ++i) {
        for (uint32_t ch = 0; ch < spp; ++ch) {
            float v = src[i * c + ch];
            if (depth == SampleDepth::F32) {
                uint32_t u;
                std::memcpy(&u, &v, 4);
                for (int k = 0; k < 4; ++k) *p++ = static_cast<uint8_t>((u >> (8 * k)) & 0xff);
            } else {
                double cl = std::isfinite(v) ? std::min(1.0, std::max(0.0, static_cast<double>(v))) : 0.0;
                if (bits == 8) *p++ = static_cast<uint8_t>(cl * 255.0 + 0.5);
                else {
                    uint16_t q = static_cast<uint16_t>(cl * 65535.0 + 0.5);
                    *p++ = static_cast<uint8_t>(q & 0xff);
                    *p++ = static_cast<uint8_t>(q >> 8);
                }
            }
        }
    }
    if (out.size() & 1) out.push_back(0);

    // Out-of-line values.
    auto appendBlob = [&](const void* data, size_t len) {
        size_t off = out.size();
        const uint8_t* b = static_cast<const uint8_t*>(data);
        out.insert(out.end(), b, b + len);
        if (out.size() & 1) out.push_back(0);
        return static_cast<uint32_t>(off);
    };
    uint32_t bitsOff = 0;
    if (spp == 3) {
        std::vector<uint8_t> b3;
        for (int i = 0; i < 3; ++i) put16(b3, static_cast<uint16_t>(bits));
        bitsOff = appendBlob(b3.data(), b3.size());
    }
    uint32_t fmtOff = 0;
    const uint16_t fmt = depth == SampleDepth::F32 ? 3 : 1;
    if (spp == 3) {
        std::vector<uint8_t> b3;
        for (int i = 0; i < 3; ++i) put16(b3, fmt);
        fmtOff = appendBlob(b3.data(), b3.size());
    }
    std::vector<uint8_t> rat;
    put32(rat, 72); put32(rat, 1);
    uint32_t resOff = appendBlob(rat.data(), rat.size());
    uint32_t softOff = appendBlob(software.c_str(), software.size() + 1);
    uint32_t descOff = desc.empty() ? 0 : appendBlob(desc.c_str(), desc.size() + 1);

    struct Entry { uint16_t tag, type; uint32_t count, value; };
    std::vector<Entry> entries;
    entries.push_back({256, 4, 1, w});
    entries.push_back({257, 4, 1, h});
    entries.push_back({258, 3, spp, spp == 3 ? bitsOff : bits});
    entries.push_back({259, 3, 1, 1});
    entries.push_back({262, 3, 1, spp == 3 ? 2u : 1u});
    if (!desc.empty()) entries.push_back({270, 2, static_cast<uint32_t>(desc.size() + 1), descOff});
    entries.push_back({273, 4, 1, static_cast<uint32_t>(dataOffset)});
    entries.push_back({277, 3, 1, spp});
    entries.push_back({278, 4, 1, h});
    entries.push_back({279, 4, 1, static_cast<uint32_t>(dataBytes)});
    entries.push_back({282, 5, 1, resOff});
    entries.push_back({283, 5, 1, resOff});
    entries.push_back({284, 3, 1, 1});
    entries.push_back({296, 3, 1, 2});
    entries.push_back({305, 2, static_cast<uint32_t>(software.size() + 1), softOff});
    entries.push_back({339, 3, spp, spp == 3 ? fmtOff : fmt});

    uint32_t ifdOff = static_cast<uint32_t>(out.size());
    put16(out, static_cast<uint16_t>(entries.size()));
    for (const Entry& e : entries) {
        put16(out, e.tag);
        put16(out, e.type);
        put32(out, e.count);
        if (e.type == 3 && e.count == 1) {
            put16(out, static_cast<uint16_t>(e.value));
            put16(out, 0);
        } else {
            put32(out, e.value);
        }
    }
    put32(out, 0);
    for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<uint8_t>((ifdOff >> (8 * i)) & 0xff);
    return out;
}

}  // namespace fixmyplanet
