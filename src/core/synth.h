// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic planets: a procedural surface map and a renderer. Used by the
// test-suite (it gives exact ground truth) and by `fixmyplanet synth` to make
// practice data.
#pragma once

#include <cstdint>

#include "geometry.h"
#include "image.h"
#include "planets.h"

namespace fixmyplanet {

// Equirectangular map: column 0 is east longitude 0 deg, increasing to the
// right; row 0 is planetographic latitude +90 deg.
struct SurfaceMap {
    Image image;
    // Bilinear lookup with wrap-around in longitude; `out` holds image.channels() values.
    void sample(double lonEastDeg, double latGraphicDeg, float* out) const;
};

// Banded, turbulent texture for the gas giants; blotchy albedo map for Mars.
SurfaceMap makeProceduralMap(Planet planet, int width, int height, uint32_t seed, bool colour);

struct RenderOptions {
    double limbExponent = 0.6;    // brightness ~ cos(incidence)^limbExponent
    double psfSigma = 0.0;        // Gaussian blur in pixels applied after rendering
    double noiseSigma = 0.0;      // Gaussian noise added last (0..1 scale)
    uint32_t noiseSeed = 1;
    int supersample = 3;          // samples per pixel edge
    bool drawRings = true;        // Saturn only
    int threads = 0;
};

// Render the planet as it appears with the given orientation and image geometry.
// sunDirView is the unit vector towards the Sun in the view frame.
Image renderPlanet(const SurfaceMap& map, const BodyShape& shape, const Orientation& orientation,
                   const ImageGeometry& geometry, int width, int height, const Vec3& sunDirView,
                   const RenderOptions& options);

}  // namespace fixmyplanet
