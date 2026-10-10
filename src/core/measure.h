// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic measurement of a planet image: where the disc is, how large it
// is and which way the planet's north pole points.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ephem.h"
#include "geometry.h"
#include "image.h"

namespace fixmyplanet {

// Outline of the sunlit disc, plus the rings for Saturn, in the view frame:
// distance from the disc centre as a function of the polar angle.
class OutlineModel {
public:
    OutlineModel(const BodyShape& shape, double deDeg, const Vec3& sunDirView, bool includeRings);
    // psi in radians, measured from +x towards +y in the view frame.
    double radius(double psi) const;
    // True where the edge in that direction is the terminator (the boundary
    // of the sunlit part) rather than the limb or a ring. The terminator is a
    // soft edge and is found less reliably.
    bool isTerminator(double psi) const;

private:
    std::vector<double> table_;
    std::vector<char> terminator_;
};

struct MeasureOptions {
    // Rough direction of the planet's north pole (degrees counter-clockwise
    // from "up"). The limb alone cannot tell north from south, so the
    // orientation search stays within `orientationRange` of this value.
    double northAngleHint = 0.0;
    bool mirror = false;
    bool fitOrientation = true;      // refine the north angle from belts (Jupiter) or rings (Saturn)
    double orientationRange = 60.0;  // degrees either side of the hint
    double pixelScaleArcsec = 0.0;   // if > 0, the radius is taken from the ephemeris instead of fitted
};

struct MeasureResult {
    bool ok = false;
    ImageGeometry geometry;
    double residualRms = 0.0;        // RMS distance of the limb points from the fitted outline, pixels
    int edgePoints = 0;              // limb points used
    bool orientationFitted = false;
    double orientationContrast = 0.0;  // belt contrast at the chosen angle relative to the median over all angles
    std::string message;             // why it failed, or a warning
};

MeasureResult measureDisc(const Image& image, const PhysicalEphemeris& ephemeris, const MeasureOptions& options);

// Measurements of a whole session can be made more consistent, because the
// telescope does not change during it:
//  * the radius is the same in every image;
//  * the north angle is constant (equatorial mount) or drifts smoothly
//    (field rotation of an alt-azimuth mount).
enum class AngleMode {
    Individual,  // keep each image's own angle
    Trend,       // replace by a robust straight line through angle versus time
    Same         // replace by the median
};

struct RegulariseReport {
    bool radiusChanged = false;
    double radius = 0;            // common radius, pixels
    bool angleChanged = false;
    double angleAtFirst = 0;      // degrees, at the earliest image
    double driftDegPerMinute = 0; // Trend mode
};

// Trend falls back to Same when there are fewer than four images.
RegulariseReport regulariseMeasurements(std::vector<ImageGeometry>& geometry, const std::vector<double>& jdUtc,
                                        AngleMode angleMode, bool commonRadius);

// Raw findings in an image, before any model is fitted; exposed for the GUI and the tests.
struct LimbScan {
    std::vector<std::pair<double, double>> points;  // limb points, pixel coordinates
    double centroidX = 0, centroidY = 0;            // centroid of the object
    double roughRadius = 0;                         // radius of a circle of the same area
    // Long axis of the object's silhouette, as a north angle: the planet's
    // axis is perpendicular to the long axis (equator, rings), so this is the
    // direction of one of the two poles, degrees counter-clockwise from "up",
    // in [0, 180).
    double axisAngle = 0;
    double elongation = 1;                          // long axis / short axis of the silhouette
    std::string message;                            // warning or reason for failure
};

LimbScan findLimbPoints(const Image& luminance);

}  // namespace fixmyplanet
