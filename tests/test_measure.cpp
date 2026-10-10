// SPDX-License-Identifier: GPL-3.0-or-later
// The automatic measurement is run on rendered planets whose position, size
// and orientation are known exactly.
#include <cstdio>

#include "ephem.h"
#include "measure.h"
#include "planets.h"
#include "synth.h"
#include "testing.h"
#include "timeutil.h"

using namespace fixmyplanet;

static Image render(Planet planet, double jd, const ImageGeometry& g, int size, double psf, double noise, bool colour) {
    static SurfaceMap maps[5];
    SurfaceMap& map = maps[static_cast<int>(planet)];
    if (map.image.empty()) map = makeProceduralMap(planet, 1440, 720, 7, true);
    PhysicalEphemeris e = computeEphemeris(planet, jd);
    RenderOptions ro;
    ro.psfSigma = psf;
    ro.noiseSigma = noise;
    ro.noiseSeed = 99;
    Image img = renderPlanet(map, bodyShape(planet), Orientation::fromCentralMeridian(e.cm[0], e.de, e.westLongitude), g, size,
                             size, e.sunDirView, ro);
    return colour ? img : img.luminance();
}

struct Case {
    const char* label;
    Planet planet;
    const char* utc;
    ImageGeometry truth;
    double hint;
    int size;
    double psf, noise;
    double tolCentre;      // pixels
    double tolRadius;      // pixels
    double tolAngle;       // degrees
    bool expectAngleFit;
};

int main() {
    // Tolerances reflect two known properties of the limb detection (see
    // docs/ALGORITHM.md): a blurred, limb-darkened edge is found about half a
    // blur width inside the true limb, which makes the radius about a pixel
    // too small; and away from opposition the soft terminator pulls the
    // centre towards the sunlit side by up to about 0.5 % of the radius.
    const Case cases[] = {
        {"Jupiter, north up, opposition", Planet::Jupiter, "2026-01-10T22:00:00", {331.4, 298.7, 180.5, 12.0, false}, 0.0, 640, 1.5, 0.01, 0.3, 1.4, 0.6, true},
        {"Jupiter, south up, mirrored", Planet::Jupiter, "2022-09-26T21:30:00", {300.2, 325.9, 210.0, 178.0, true}, 180.0, 640, 1.0, 0.005, 0.3, 1.4, 0.6, true},
        {"Jupiter, tilted 60 deg, phase 9 deg", Planet::Jupiter, "2030-03-15T03:00:00", {310.0, 322.0, 150.0, -60.0, false}, -40.0, 640, 2.0, 0.01, 1.0, 1.6, 0.8, true},
        {"Saturn, rings open", Planet::Saturn, "2017-06-15T01:00:00", {322.3, 316.8, 105.0, -35.0, false}, -20.0, 640, 1.5, 0.01, 0.3, 0.5, 0.3, true},
        {"Saturn, rings nearly edge-on, phase 5 deg", Planet::Saturn, "2026-01-10T22:00:00", {318.1, 321.2, 118.0, 25.0, false}, 10.0, 640, 1.5, 0.01, 0.8, 1.4, 0.5, true},
        {"Mars, gibbous, phase 38 deg", Planet::Mars, "2020-08-20T04:30:00", {250.3, 262.1, 160.0, 20.0, false}, 20.0, 512, 1.5, 0.01, 1.0, 1.4, 1e-9, false},
        {"Uranus, small disc", Planet::Uranus, "2026-11-20T22:00:00", {101.3, 98.6, 40.0, 0.0, false}, 0.0, 200, 1.2, 0.005, 0.3, 1.2, 1e-9, false},
        {"Neptune, tiny disc", Planet::Neptune, "2026-09-25T22:00:00", {64.4, 63.2, 22.0, 0.0, false}, 0.0, 128, 1.0, 0.005, 0.3, 1.2, 1e-9, false},
    };
    for (const Case& c : cases) {
        double jd = 0;
        CHECK(parseTime(c.utc, jd));
        Image img = render(c.planet, jd, c.truth, c.size, c.psf, c.noise, true);
        MeasureOptions mo;
        mo.northAngleHint = c.hint;
        mo.mirror = c.truth.mirror;
        MeasureResult r = measureDisc(img, computeEphemeris(c.planet, jd), mo);
        std::printf("%-42s centre %+.2f %+.2f px  radius %+.2f px  angle %+.2f deg  rms %.2f  %s\n", c.label, r.geometry.cx - c.truth.cx,
                    r.geometry.cy - c.truth.cy, r.geometry.radius - c.truth.radius,
                    wrap180(r.geometry.northAngle - c.truth.northAngle), r.residualRms, r.message.c_str());
        CHECK(r.ok);
        CHECK_NEAR(r.geometry.cx, c.truth.cx, c.tolCentre);
        CHECK_NEAR(r.geometry.cy, c.truth.cy, c.tolCentre);
        CHECK_NEAR(r.geometry.radius, c.truth.radius, c.tolRadius);
        CHECK(r.geometry.radius <= c.truth.radius + 0.2);  // the bias is always inwards
        CHECK_NEAR(wrap180(r.geometry.northAngle - c.truth.northAngle), 0.0, c.tolAngle);
        CHECK(r.orientationFitted == c.expectAngleFit);
        CHECK(r.geometry.mirror == c.truth.mirror);
        CHECK(r.residualRms < 1.0);
    }

    double jd = 0;
    parseTime("2026-01-10T22:00:00", jd);
    const PhysicalEphemeris eph = computeEphemeris(Planet::Jupiter, jd);
    const ImageGeometry truth{331.4, 298.7, 180.5, 12.0, false};
    Image jup = render(Planet::Jupiter, jd, truth, 640, 1.5, 0.01, false);
    {
        // Mono input works like colour input.
        MeasureResult r = measureDisc(jup, eph, MeasureOptions{});
        CHECK(r.ok);
        CHECK_NEAR(r.geometry.cx, truth.cx, 0.3);
        CHECK_NEAR(wrap180(r.geometry.northAngle - truth.northAngle), 0.0, 0.6);
    }
    {
        // The limb cannot tell north from south: a hint on the wrong side
        // yields the opposite pole.
        MeasureOptions mo;
        mo.northAngleHint = 180.0;
        MeasureResult r = measureDisc(jup, eph, mo);
        CHECK(r.ok);
        CHECK_NEAR(wrap180(r.geometry.northAngle - (truth.northAngle + 180.0)), 0.0, 0.8);
    }
    {
        // Orientation switched off: the hint is returned untouched.
        MeasureOptions mo;
        mo.northAngleHint = 10.0;
        mo.fitOrientation = false;
        MeasureResult r = measureDisc(jup, eph, mo);
        CHECK(r.ok && !r.orientationFitted);
        CHECK_NEAR(r.geometry.northAngle, 10.0, 1e-9);
    }
    {
        // Known image scale: the radius comes from the ephemeris.
        MeasureOptions mo;
        mo.pixelScaleArcsec = 0.5 * eph.diameterArcsec / truth.radius;
        MeasureResult r = measureDisc(jup, eph, mo);
        CHECK(r.ok);
        CHECK_NEAR(r.geometry.radius, truth.radius, 1e-6);
        CHECK_NEAR(r.geometry.cx, truth.cx, 0.3);
    }
    {
        // A bright moon next to the planet is ignored.
        Image withMoon = jup;
        for (int y = -3; y <= 3; ++y)
            for (int x = -3; x <= 3; ++x)
                if (x * x + y * y <= 9) withMoon.at(60 + x, 300 + y) = 0.9f;
        MeasureResult r = measureDisc(withMoon, eph, MeasureOptions{});
        CHECK(r.ok);
        CHECK_NEAR(r.geometry.cx, truth.cx, 0.3);
        CHECK_NEAR(r.geometry.cy, truth.cy, 0.3);
    }
    {
        // Nothing to measure.
        CHECK(!measureDisc(Image(200, 200, 1, 0.1f), eph, MeasureOptions{}).ok);
        CHECK(!measureDisc(Image(8, 8, 1, 0.5f), eph, MeasureOptions{}).ok);
        CHECK(!measureDisc(Image(), eph, MeasureOptions{}).ok);
        MeasureResult r = measureDisc(Image(200, 200, 1, 0.1f), eph, MeasureOptions{});
        CHECK(!r.message.empty());
        // Planet cut off by the image border: still measured, with a warning.
        ImageGeometry off = truth;
        off.cx = 560;
        Image cut = render(Planet::Jupiter, jd, off, 640, 1.5, 0.01, false);
        r = measureDisc(cut, eph, MeasureOptions{});
        CHECK(!r.message.empty());
    }

    // Making a session consistent.
    {
        std::vector<ImageGeometry> g(6);
        std::vector<double> t(6);
        for (int i = 0; i < 6; ++i) {
            t[i] = 2461051.4 + i * 2.0 / 1440.0;           // every 2 minutes
            g[i].radius = 200.0 + 0.1 * (i % 3 - 1);
            g[i].northAngle = 178.0 + 0.5 * i + (i == 2 ? 7.0 : 0.0);  // drifts across +-180, one outlier
        }
        std::vector<ImageGeometry> a = g;
        RegulariseReport rep = regulariseMeasurements(a, t, AngleMode::Trend, true);
        CHECK(rep.radiusChanged && rep.angleChanged);
        CHECK_NEAR(rep.radius, 200.0, 0.11);
        CHECK_NEAR(rep.driftDegPerMinute, 0.25, 1e-6);
        for (int i = 0; i < 6; ++i) {
            CHECK_NEAR(a[i].radius, rep.radius, 1e-12);
            CHECK_NEAR(wrap180(a[i].northAngle - (178.0 + 0.5 * i)), 0.0, 1e-6);
        }
        a = g;
        rep = regulariseMeasurements(a, t, AngleMode::Same, false);
        CHECK(!rep.radiusChanged);
        CHECK_NEAR(a[0].radius, g[0].radius, 1e-12);
        for (int i = 0; i < 6; ++i) CHECK_NEAR(wrap180(a[i].northAngle - a[0].northAngle), 0.0, 1e-9);
        CHECK_NEAR(wrap180(a[0].northAngle - 179.5), 0.0, 0.26);
        a = g;
        rep = regulariseMeasurements(a, t, AngleMode::Individual, false);
        CHECK(!rep.angleChanged);
        for (int i = 0; i < 6; ++i) CHECK_NEAR(a[i].northAngle, g[i].northAngle, 1e-12);
        // Too few images for a trend: falls back to the median.
        std::vector<ImageGeometry> few(g.begin(), g.begin() + 3);
        std::vector<double> tf(t.begin(), t.begin() + 3);
        rep = regulariseMeasurements(few, tf, AngleMode::Trend, true);
        CHECK_NEAR(rep.driftDegPerMinute, 0.0, 1e-12);
        CHECK_NEAR(wrap180(few[0].northAngle - few[2].northAngle), 0.0, 1e-9);
        // Nothing to do for one image or mismatched input.
        std::vector<ImageGeometry> one(1);
        CHECK(!regulariseMeasurements(one, {t[0]}, AngleMode::Trend, true).radiusChanged);
        CHECK(!regulariseMeasurements(a, {t[0]}, AngleMode::Trend, true).angleChanged);
    }

    return testing::finish("test_measure");
}
