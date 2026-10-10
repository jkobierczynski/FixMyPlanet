// SPDX-License-Identifier: GPL-3.0-or-later
#include "synth.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "parallel.h"

namespace fixmyplanet {

namespace {

inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t s) {
    uint32_t h = x * 0x9E3779B1u ^ (y + 0x7F4A7C15u) * 0x85EBCA77u ^ (s + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

inline double lattice(int ix, int iy, int periodX, uint32_t seed) {
    int px = ((ix % periodX) + periodX) % periodX;
    return hash3(static_cast<uint32_t>(px), static_cast<uint32_t>(iy + 100000), seed) / 4294967295.0;
}

// Value noise in [0, 1], periodic in x with the given integer period.
double valueNoise(double x, double y, int periodX, uint32_t seed) {
    double fx = std::floor(x), fy = std::floor(y);
    int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    double tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    double a = lattice(ix, iy, periodX, seed), b = lattice(ix + 1, iy, periodX, seed);
    double c = lattice(ix, iy + 1, periodX, seed), d = lattice(ix + 1, iy + 1, periodX, seed);
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

// Fractal noise, periodic in longitude. u in [0,1) around the planet, v in [0,1] pole to pole.
double fbm(double u, double v, int freqX, int freqY, int octaves, uint32_t seed) {
    double sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * valueNoise(u * freqX, v * freqY, freqX, seed + 977u * o);
        norm += amp;
        amp *= 0.55;
        freqX *= 2;
        freqY *= 2;
    }
    return sum / norm;
}

double gauss(double x, double centre, double width) {
    double t = (x - centre) / width;
    return std::exp(-0.5 * t * t);
}

// 1 inside an oval centred at (lon0, lat0) with the given semi-axes, soft edge.
double oval(double lon, double lat, double lon0, double lat0, double a, double b) {
    double dl = lon - lon0;
    while (dl > 180) dl -= 360;
    while (dl < -180) dl += 360;
    double q = std::sqrt((dl / a) * (dl / a) + ((lat - lat0) / b) * ((lat - lat0) / b));
    return 1.0 / (1.0 + std::exp((q - 1.0) * 8.0));
}

struct Rgb { double r, g, b; };
Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

}  // namespace

void SurfaceMap::sample(double lonEastDeg, double latGraphicDeg, float* out) const {
    const int w = image.width(), h = image.height(), c = image.channels();
    double fx = lonEastDeg / 360.0;
    fx -= std::floor(fx);
    double x = fx * w - 0.5;
    double y = (90.0 - latGraphicDeg) / 180.0 * h - 0.5;
    y = std::min(std::max(y, 0.0), h - 1.0);
    int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    double tx = x - x0, ty = y - y0;
    int xa = ((x0 % w) + w) % w, xb = (xa + 1) % w;
    int ya = std::min(std::max(y0, 0), h - 1), yb = std::min(ya + 1, h - 1);
    for (int k = 0; k < c; ++k) {
        double v = (image.at(xa, ya, k) * (1 - tx) + image.at(xb, ya, k) * tx) * (1 - ty) +
                   (image.at(xa, yb, k) * (1 - tx) + image.at(xb, yb, k) * tx) * ty;
        out[k] = static_cast<float>(v);
    }
}

SurfaceMap makeProceduralMap(Planet planet, int width, int height, uint32_t seed, bool colour) {
    SurfaceMap map;
    map.image = Image(width, height, colour ? 3 : 1);
    const bool giant = planet != Planet::Mars;
    const bool ice = planet == Planet::Uranus || planet == Planet::Neptune;

    parallelRows(height, 0, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            double v = (y + 0.5) / height;
            double lat = 90.0 - 180.0 * v;
            for (int x = 0; x < width; ++x) {
                double u = (x + 0.5) / width;
                double lon = 360.0 * u;
                double value;
                Rgb col;
                if (giant) {
                    // Wavy belt edges: displace the latitude a little along the longitude.
                    double wav = fbm(u, v, 24, 6, 3, seed + 11) - 0.5;
                    double latW = lat + 2.4 * wav;
                    double strength = planet == Planet::Jupiter ? 1.0 : ice ? 0.15 : 0.45;
                    double belts = 0.34 * gauss(latW, 13.0, 5.0) + 0.30 * gauss(latW, -14.0, 6.0) +
                                   0.16 * gauss(latW, 33.0, 4.0) + 0.14 * gauss(latW, -36.0, 4.5) +
                                   0.12 * gauss(latW, 52.0, 6.0) + 0.12 * gauss(latW, -55.0, 6.0) +
                                   0.10 * gauss(latW, 0.0, 2.0);
                    double polar = 0.22 * (1.0 / (1.0 + std::exp(-(std::fabs(latW) - 62.0) / 5.0)));
                    double band = 0.88 - strength * (belts + polar);
                    // Streaky turbulence, stronger inside the belts.
                    double turb = fbm(u, v, 48, 96, 5, seed + 3) - 0.5;
                    double fine = fbm(u, v, 160, 200, 3, seed + 7) - 0.5;
                    double detail = (0.20 + 0.55 * belts) * strength;
                    value = band + detail * (0.9 * turb + 0.5 * fine);
                    Rgb zone{0.96, 0.91, 0.80}, belt{0.60, 0.40, 0.27};
                    if (planet == Planet::Saturn) { zone = {0.93, 0.86, 0.66}; belt = {0.70, 0.58, 0.40}; }
                    if (planet == Planet::Uranus) { zone = {0.70, 0.90, 0.92}; belt = {0.55, 0.78, 0.84}; }
                    if (planet == Planet::Neptune) { zone = {0.45, 0.60, 0.95}; belt = {0.30, 0.42, 0.80}; }
                    double t = std::min(1.0, std::max(0.0, (0.90 - value) / 0.45));
                    col = mix(zone, belt, t);
                    if (planet == Planet::Jupiter) {
                        // Great Red Spot and a few smaller ovals and barges.
                        double grs = oval(lon, lat, 120.0, -22.0, 9.0, 5.5);
                        value = value * (1 - grs) + 0.58 * grs;
                        col = mix(col, Rgb{0.78, 0.36, 0.22}, grs);
                        static const double ovals[][3] = {{40, -33, 1}, {205, -33, 1}, {290, -34, 1}, {330, -41, 1},
                                                          {75, 16, -1}, {160, 17, -1}, {250, 15, -1}, {310, 41, 1}};
                        for (const auto& o : ovals) {
                            double m = oval(lon, lat, o[0], o[1], 3.2, 2.2);
                            double target = o[2] > 0 ? 0.97 : 0.38;
                            value = value * (1 - m) + target * m;
                            col = mix(col, o[2] > 0 ? Rgb{0.98, 0.96, 0.90} : Rgb{0.42, 0.26, 0.18}, m);
                        }
                    } else {
                        // A handful of bright storms so that there is something to track.
                        static const double spots[][2] = {{30, 28}, {140, -40}, {210, 35}, {300, -25}, {80, -60}};
                        for (const auto& s : spots) {
                            double m = oval(lon, lat, s[0], s[1], 4.0, 2.5);
                            value = value * (1 - m) + 0.98 * m;
                            col = mix(col, Rgb{0.98, 0.98, 0.98}, m);
                        }
                    }
                } else {
                    double a = fbm(u, v, 6, 5, 5, seed + 5);
                    double b = fbm(u, v, 40, 40, 4, seed + 9) - 0.5;
                    double dark = 1.0 / (1.0 + std::exp(-(a - 0.52) * 22.0));
                    dark *= gauss(lat, -10.0, 45.0);
                    value = 0.82 - 0.42 * dark + 0.10 * b;
                    col = mix(Rgb{0.90, 0.58, 0.36}, Rgb{0.42, 0.28, 0.22}, dark);
                    double cap = 1.0 / (1.0 + std::exp(-(lat - 78.0) / 1.5));
                    value = value * (1 - cap) + 0.98 * cap;
                    col = mix(col, Rgb{0.97, 0.97, 0.97}, cap);
                }
                value = std::min(1.0, std::max(0.05, value));
                if (colour) {
                    // Keep the tint, take the brightness from `value`.
                    double lum = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
                    double s = lum > 0 ? value / lum : 1.0;
                    map.image.at(x, y, 0) = static_cast<float>(std::min(1.0, col.r * s));
                    map.image.at(x, y, 1) = static_cast<float>(std::min(1.0, col.g * s));
                    map.image.at(x, y, 2) = static_cast<float>(std::min(1.0, col.b * s));
                } else {
                    map.image.at(x, y, 0) = static_cast<float>(value);
                }
            }
        }
    });
    return map;
}

Image renderPlanet(const SurfaceMap& map, const BodyShape& shape, const Orientation& orientation,
                   const ImageGeometry& geometry, int width, int height, const Vec3& sunDirView,
                   const RenderOptions& options) {
    const int c = map.image.channels();
    Image out(width, height, c);
    const int ss = std::max(1, options.supersample);
    const Vec3 sunBody = orientation.viewToBody(normalized(sunDirView));
    const double inv = 1.0 / (ss * ss);

    parallelRows(height, options.threads, [&](int y0, int y1) {
        std::vector<float> px(c), acc(c);
        for (int py = y0; py < y1; ++py)
            for (int pxl = 0; pxl < width; ++pxl) {
                std::fill(acc.begin(), acc.end(), 0.0f);
                for (int sy = 0; sy < ss; ++sy)
                    for (int sx = 0; sx < ss; ++sx) {
                        double u = pxl + (sx + 0.5) / ss - 0.5;
                        double v = py + (sy + 0.5) / ss - 0.5;
                        double x, y;
                        geometry.pixelToView(u, v, x, y);
                        float colr[4] = {0, 0, 0, 0};
                        Vec3 body;
                        double mu;
                        double zSurface = -1e9;
                        bool hit = orientation.surfacePoint(x, y, shape, body, mu);
                        if (hit) {
                            zSurface = orientation.bodyToView(body).z;
                            double mu0 = dot(surfaceNormal(body, shape), sunBody);
                            if (mu0 > 0) {
                                double lon, lat;
                                bodyToLonLat(body, shape, lon, lat);
                                map.sample(lon, lat, px.data());
                                double shade = std::pow(mu0, options.limbExponent);
                                for (int k = 0; k < c; ++k) colr[k] = static_cast<float>(px[k] * shade);
                            }
                        }
                        double zRing;
                        if (options.drawRings && orientation.ringPoint(x, y, shape, zRing) && zRing > zSurface) {
                            Vec3 p = orientation.viewToBody(Vec3{x, y, zRing});
                            double rho = std::sqrt(p.x * p.x + p.y * p.y);
                            // Rough radial structure in Saturn radii: C ring, B ring, Cassini division, A ring.
                            double alpha, bright;
                            if (rho < 1.527) { alpha = 0.18; bright = 0.35; }
                            else if (rho < 1.951) { alpha = 1.0; bright = 0.92 - 0.10 * std::sin(rho * 40.0); }
                            else if (rho < 2.027) { alpha = 0.06; bright = 0.3; }
                            else { alpha = 0.75; bright = 0.70; }
                            static const float tint[3] = {1.0f, 0.95f, 0.82f};
                            for (int k = 0; k < c; ++k) {
                                float rc = static_cast<float>(bright) * (c == 3 ? tint[k] : 1.0f);
                                colr[k] = static_cast<float>(alpha * rc + (1 - alpha) * colr[k]);
                            }
                        }
                        for (int k = 0; k < c; ++k) acc[k] += colr[k];
                    }
                for (int k = 0; k < c; ++k) out.at(pxl, py, k) = static_cast<float>(acc[k] * inv);
            }
    });

    if (options.psfSigma > 0) out = gaussianBlur(out, options.psfSigma);
    if (options.noiseSigma > 0) {
        std::mt19937 rng(options.noiseSeed);
        std::normal_distribution<float> nd(0.0f, static_cast<float>(options.noiseSigma));
        float* d = out.data();
        for (size_t i = 0; i < out.size(); ++i) d[i] = std::max(0.0f, d[i] + nd(rng));
    }
    return out;
}

}  // namespace fixmyplanet
