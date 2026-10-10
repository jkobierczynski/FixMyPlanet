// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry of the planet's disc: body frame <-> view frame <-> image pixels.
//
// Body frame   : z along the rotation axis (IAU north), x through the prime
//                meridian of the chosen longitude system. Unit = equatorial radius.
// View frame   : z towards the observer, y along the projected north pole,
//                x = y cross z, i.e. to the right when north is up. The sky is
//                seen from far away, so the projection is orthographic.
// Image pixels : u to the right, v downwards, pixel centres at integers.
#pragma once

#include "math3.h"

namespace fixmyplanet {

// Where the planet sits in one image.
struct ImageGeometry {
    double cx = 0, cy = 0;     // disc centre, pixels
    double radius = 100;       // equatorial radius, pixels
    double northAngle = 0;     // direction of the planet's north pole in the image,
                               // degrees counter-clockwise from "up"
    bool mirror = false;       // image is flipped left-right (odd number of reflections)

    // View frame (units of equatorial radius) -> pixel.
    void viewToPixel(double x, double y, double& u, double& v) const;
    // Pixel -> view frame.
    void pixelToView(double u, double v, double& x, double& y) const;
};

// The oblate planet and, for Saturn, its rings. Lengths in equatorial radii.
struct BodyShape {
    double polarRatio = 1.0;   // polar radius / equatorial radius
    bool hasRings = false;
    double ringInner = 0, ringOuter = 0;
};

// Orientation of the body relative to the observer at one instant.
class Orientation {
public:
    Orientation() = default;
    // lonEastDeg: body-frame longitude (east positive) of the observer direction;
    // deDeg: planetocentric declination of the observer.
    Orientation(double lonEastDeg, double deDeg);

    // From a planetographic central meridian longitude. `westLongitude` is true
    // for planets whose longitudes increase to the west (all but Uranus).
    static Orientation fromCentralMeridian(double cmDeg, double deDeg, bool westLongitude);

    const Vec3& observerDir() const { return dir_; }
    double de() const { return de_; }
    double lonEast() const { return lonEast_; }

    Vec3 bodyToView(const Vec3& b) const { return bodyToView_ * b; }
    Vec3 viewToBody(const Vec3& v) const { return viewToBody_ * v; }

    // Intersect the line of sight through view point (x, y) with the spheroid.
    // On a hit returns true, the surface point in the body frame, and mu, the
    // cosine of the angle between the surface normal and the observer.
    bool surfacePoint(double x, double y, const BodyShape& shape, Vec3& body, double& mu) const;

    // Cosine of the emission angle for a body-frame surface point (negative on the far side).
    double emissionCos(const Vec3& body, const BodyShape& shape) const;

    // Line of sight through (x, y) against the ring plane. Returns true when it
    // crosses the rings between ringInner and ringOuter; zView is the height of
    // that crossing above the sky plane through the planet's centre.
    bool ringPoint(double x, double y, const BodyShape& shape, double& zView) const;

private:
    Mat3 bodyToView_, viewToBody_;
    Vec3 dir_{1, 0, 0};
    double lonEast_ = 0, de_ = 0;
};

// Unit outward normal of the spheroid at a body-frame surface point.
Vec3 surfaceNormal(const Vec3& body, const BodyShape& shape);

// Planetographic latitude / east longitude (degrees) of a body-frame surface point.
void bodyToLonLat(const Vec3& body, const BodyShape& shape, double& lonEastDeg, double& latGraphicDeg);
Vec3 lonLatToBody(double lonEastDeg, double latGraphicDeg, const BodyShape& shape);

// Semi-minor axis (in equatorial radii) of the projected limb ellipse.
double projectedPolarRadius(const BodyShape& shape, double deDeg);

}  // namespace fixmyplanet
