// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>

#include "geometry.h"
#include "measure.h"
#include "overlay.h"
#include "planets.h"
#include "testing.h"

using namespace fixmyplanet;

int main() {
    // Pixel <-> view round trips for all orientations and both parities.
    for (double angle : {0.0, 37.0, 90.0, 180.0, -123.4})
        for (bool mirror : {false, true}) {
            ImageGeometry g;
            g.cx = 321.5;
            g.cy = 240.25;
            g.radius = 187.3;
            g.northAngle = angle;
            g.mirror = mirror;
            for (double x : {-0.9, 0.0, 0.4})
                for (double y : {-0.7, 0.0, 0.8}) {
                    double u, v, x2, y2;
                    g.viewToPixel(x, y, u, v);
                    g.pixelToView(u, v, x2, y2);
                    CHECK_NEAR(x2, x, 1e-12);
                    CHECK_NEAR(y2, y, 1e-12);
                }
        }
    {
        // North up: +y in the view is up in the image, +x is to the right.
        ImageGeometry g;
        g.cx = 100; g.cy = 100; g.radius = 50; g.northAngle = 0;
        double u, v;
        g.viewToPixel(0, 1, u, v);
        CHECK_NEAR(u, 100, 1e-9);
        CHECK_NEAR(v, 50, 1e-9);
        g.viewToPixel(1, 0, u, v);
        CHECK_NEAR(u, 150, 1e-9);
        // North angle 90: the pole points to the left.
        g.northAngle = 90;
        g.viewToPixel(0, 1, u, v);
        CHECK_NEAR(u, 50, 1e-9);
        CHECK_NEAR(v, 100, 1e-9);
        // Mirror flips left and right but keeps the pole where it is.
        g.northAngle = 0;
        g.mirror = true;
        g.viewToPixel(1, 0, u, v);
        CHECK_NEAR(u, 50, 1e-9);
        g.viewToPixel(0, 1, u, v);
        CHECK_NEAR(v, 50, 1e-9);
    }

    for (Planet planet : allPlanets()) {
        const BodyShape shape = bodyShape(planet);
        for (double de : {-26.0, -3.0, 0.0, 12.0, 74.0})
            for (double lon : {0.0, 77.0, 245.0}) {
                Orientation o(lon, de);
                // The disc centre is the sub-observer point.
                Vec3 b;
                double mu;
                CHECK(o.surfacePoint(0, 0, shape, b, mu));
                double lonE, latG;
                bodyToLonLat(b, shape, lonE, latG);
                CHECK_NEAR(wrap180(lonE - lon), 0.0, 1e-9);
                // Its planetocentric latitude is the declination of the observer.
                CHECK_NEAR(std::atan2(b.z, std::sqrt(b.x * b.x + b.y * b.y)) * kRadToDeg, de, 1e-9);

                // Surface point -> view -> surface point.
                for (double x : {-0.6, 0.0, 0.3, 0.7})
                    for (double y : {-0.6, 0.1, 0.55}) {
                        if (!o.surfacePoint(x, y, shape, b, mu)) continue;
                        CHECK(mu > 0);
                        // On the spheroid.
                        double c = shape.polarRatio;
                        CHECK_NEAR(b.x * b.x + b.y * b.y + b.z * b.z / (c * c), 1.0, 1e-12);
                        Vec3 v = o.bodyToView(b);
                        CHECK_NEAR(v.x, x, 1e-12);
                        CHECK_NEAR(v.y, y, 1e-12);
                        CHECK_NEAR(o.emissionCos(b, shape), mu, 1e-12);
                        // Longitude/latitude round trip.
                        bodyToLonLat(b, shape, lonE, latG);
                        Vec3 b2 = lonLatToBody(lonE, latG, shape);
                        CHECK_NEAR(b2.x, b.x, 1e-10);
                        CHECK_NEAR(b2.y, b.y, 1e-10);
                        CHECK_NEAR(b2.z, b.z, 1e-10);
                    }

                // The limb is the ellipse with semi-axes 1 and projectedPolarRadius.
                const double cp = projectedPolarRadius(shape, de);
                CHECK(o.surfacePoint(0.999999, 0, shape, b, mu));
                CHECK(mu < 0.01);
                CHECK(!o.surfacePoint(1.000001, 0, shape, b, mu));
                CHECK(o.surfacePoint(0, cp * 0.999999, shape, b, mu));
                CHECK(!o.surfacePoint(0, cp * 1.000001, shape, b, mu));
                // The north pole projects onto the +y axis.
                Vec3 np = o.bodyToView(Vec3{0, 0, shape.polarRatio});
                CHECK_NEAR(np.x, 0.0, 1e-12);
                CHECK(np.y > 0);
            }
    }

    {
        // Direction of rotation: a prograde planet (west longitudes) carries a
        // feature from left to right when north is up, and its central
        // meridian longitude grows.
        const BodyShape shape = bodyShape(Planet::Jupiter);
        Vec3 feature = lonLatToBody(-100.0, 10.0, shape);  // west longitude 100
        Orientation early = Orientation::fromCentralMeridian(95.0, 2.0, true);
        Orientation late = Orientation::fromCentralMeridian(105.0, 2.0, true);
        CHECK(late.bodyToView(feature).x > early.bodyToView(feature).x);
        CHECK(early.bodyToView(feature).x < 0 && late.bodyToView(feature).x > 0);
        // Uranus turns the other way about its IAU north pole (its longitudes
        // are east longitudes): with that pole up, features go right to left.
        const BodyShape us = bodyShape(Planet::Uranus);
        Vec3 f2 = lonLatToBody(100.0, 10.0, us);
        Orientation u1 = Orientation::fromCentralMeridian(95.0, 2.0, false);
        Orientation u2 = Orientation::fromCentralMeridian(105.0, 2.0, false);
        CHECK(u1.bodyToView(f2).x > 0 && u2.bodyToView(f2).x < 0);
    }

    {
        // Rings: a line of sight through the ansa hits the ring, not the globe.
        const BodyShape shape = bodyShape(Planet::Saturn);
        CHECK(shape.hasRings);
        CHECK_NEAR(shape.ringOuter, 136775.0 / 60268.0, 1e-9);
        Orientation o(0.0, 20.0);
        double z;
        CHECK(o.ringPoint(2.0, 0.0, shape, z));
        CHECK_NEAR(z, 0.0, 1e-12);
        CHECK(!o.ringPoint(2.5, 0.0, shape, z));
        CHECK(!o.ringPoint(1.1, 0.0, shape, z));   // gap between globe and C ring
        // In front of the globe below the centre (observer north of the ring plane).
        CHECK(o.ringPoint(0.0, -0.55, shape, z));
        Vec3 b;
        double mu;
        CHECK(o.surfacePoint(0.0, -0.55, shape, b, mu));
        CHECK(z > o.bodyToView(b).z);
        // Edge-on rings cover nothing.
        Orientation edge(0.0, 0.0);
        CHECK(!edge.ringPoint(2.0, 0.0, shape, z));
    }

    {
        // Outline model: an ellipse at zero phase; pulled in on the dark side otherwise.
        const BodyShape shape = bodyShape(Planet::Jupiter);
        OutlineModel full(shape, 3.0, Vec3{0, 0, 1}, true);
        const double cp = projectedPolarRadius(shape, 3.0);
        CHECK_NEAR(full.radius(0.0), 1.0, 1e-5);
        CHECK_NEAR(full.radius(kPi / 2), cp, 1e-5);
        CHECK_NEAR(full.radius(kPi), 1.0, 1e-5);
        // Sun 40 deg to the right of the observer: the left edge is the terminator at cos(40 deg).
        const BodyShape sphere{1.0, false, 0, 0};
        Vec3 sun{std::sin(40 * kDegToRad), 0, std::cos(40 * kDegToRad)};
        OutlineModel gibbous(sphere, 0.0, sun, true);
        CHECK_NEAR(gibbous.radius(0.0), 1.0, 1e-5);
        CHECK_NEAR(gibbous.radius(kPi), std::cos(40 * kDegToRad), 1e-5);
        CHECK_NEAR(gibbous.radius(kPi / 2), 1.0, 1e-4);
        // Saturn: the outline follows the ring ansae.
        const BodyShape sat = bodyShape(Planet::Saturn);
        OutlineModel ringed(sat, 25.0, Vec3{0, 0, 1}, true);
        CHECK_NEAR(ringed.radius(0.0), sat.ringOuter, 1e-5);
        CHECK(ringed.radius(kPi / 2) > projectedPolarRadius(sat, 25.0) - 1e-9);
    }

    {
        // Overlay curves exist and stay inside the outline.
        const BodyShape sat = bodyShape(Planet::Saturn);
        std::vector<Curve> curves = overlayCurves(sat, -20.0, Vec3{0.1, 0, 1}, true, {1.95});
        bool limb = false, ring = false, pole = false, equator = false;
        for (const Curve& c : curves) {
            if (c.kind == CurveKind::Limb) limb = c.points.size() > 100;
            if (c.kind == CurveKind::Ring) ring = true;
            if (c.kind == CurveKind::NorthPole) pole = c.points.size() == 1;
            if (c.kind == CurveKind::Equator) equator = true;
            for (const auto& p : c.points) CHECK(std::fabs(p.first) < 2.3 && std::fabs(p.second) < 2.3);
        }
        CHECK(limb && ring && pole && equator);
    }

    return testing::finish("test_geometry");
}
