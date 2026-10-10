// SPDX-License-Identifier: GPL-3.0-or-later
// Curves that show the fitted planet on top of an image: limb, terminator,
// equator, central meridian, latitude circles and rings.
#pragma once

#include <utility>
#include <vector>

#include "geometry.h"
#include "image.h"

namespace fixmyplanet {

enum class CurveKind { Limb, Terminator, Equator, CentralMeridian, Latitude, Ring, NorthPole };

struct Curve {
    CurveKind kind;
    // Points in the view frame (units of the equatorial radius). For
    // NorthPole a single point: the visible end of the axis, or the point of
    // the limb nearest the north pole when the pole itself is on the far side.
    std::vector<std::pair<double, double>> points;
};

// ringMarks: extra ring radii to draw (equatorial radii), besides the inner and outer edge.
std::vector<Curve> overlayCurves(const BodyShape& shape, double deDeg, const Vec3& sunDirView, bool latitudeGrid,
                                 const std::vector<double>& ringMarks = {});

// Draw the curves into a copy of the image (converted to colour) for checking
// a measurement without the GUI.
Image drawOverlay(const Image& image, const ImageGeometry& geometry, const std::vector<Curve>& curves);

}  // namespace fixmyplanet
