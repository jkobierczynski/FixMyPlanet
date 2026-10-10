// SPDX-License-Identifier: GPL-3.0-or-later
// Derotation checked against renders of the same synthetic planet: the
// combined image must look like the planet rendered at the reference time.
#include <algorithm>
#include <cstdio>
#include <random>

#include "derotate.h"
#include "ephem.h"
#include "planets.h"
#include "synth.h"
#include "testing.h"
#include "timeutil.h"

using namespace fixmyplanet;

namespace {

const int kSize = 400;

struct Scene {
    Planet planet;
    SurfaceMap map;
    BodyShape shape;
    double t0;        // reference time, UTC JD
    int system;

    Scene(Planet p, const char* utc, int sys = -1) : planet(p), system(sys) {
        map = makeProceduralMap(p, 1440, 720, 3, true);
        shape = bodyShape(p);
        parseTime(utc, t0);
    }
    Image render(double jd, const ImageGeometry& g, double psf, double noise, uint32_t seed) const {
        RotationModel model(planet, system, std::numeric_limits<double>::quiet_NaN(), t0);
        PhysicalEphemeris e = computeEphemeris(planet, jd);
        RenderOptions ro;
        ro.psfSigma = psf;
        ro.noiseSigma = noise;
        ro.noiseSeed = seed;
        return renderPlanet(map, shape, model.at(jd).orientation, g, kSize, kSize, e.sunDirView, ro);
    }
};

ImageGeometry centred(double radius, double angle = 0, bool mirror = false) {
    ImageGeometry g;
    g.cx = g.cy = 0.5 * (kSize - 1);
    g.radius = radius;
    g.northAngle = angle;
    g.mirror = mirror;
    return g;
}

// RMS difference inside the ellipse of relative radius rhoMax around the disc centre.
double rmsDiff(const Image& a, const Image& b, const ImageGeometry& g, double polar, double rhoMin, double rhoMax) {
    double acc = 0;
    size_t n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            double vx, vy;
            g.pixelToView(x, y, vx, vy);
            double rho = std::sqrt(vx * vx + (vy / polar) * (vy / polar));
            if (rho < rhoMin || rho >= rhoMax) continue;
            for (int c = 0; c < a.channels(); ++c) {
                double d = a.at(x, y, c) - b.at(x, y, c);
                acc += d * d;
                ++n;
            }
        }
    return n ? std::sqrt(acc / n) : 0.0;
}

}  // namespace

int main() {
    Scene jup(Planet::Jupiter, "2026-01-10T22:09:00");
    const double polar = projectedPolarRadius(jup.shape, computeEphemeris(Planet::Jupiter, jup.t0).de);
    const double minute = 1.0 / 1440.0;
    const double radius = 140.0;
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);

    // Seven mono frames, three minutes apart, the disc wandering a few pixels.
    std::vector<Frame> clean;
    for (int i = 0; i < 7; ++i) {
        Frame f;
        f.jdUtc = jup.t0 + (i - 3) * 3.0 * minute;
        f.geometry = centred(radius, 15.0);
        if (i != 3) {
            f.geometry.cx += 4.0 * uni(rng);
            f.geometry.cy += 4.0 * uni(rng);
        }
        f.image = jup.render(f.jdUtc, f.geometry, 1.0, 0.0, 0).luminance();
        f.name = "frame" + std::to_string(i);
        clean.push_back(f);
    }
    const ImageGeometry refG = clean[3].geometry;
    const Image truth = jup.render(jup.t0, refG, 1.0, 0.0, 0).luminance();

    DerotateOptions base;
    base.planet = Planet::Jupiter;
    base.refineAlignment = false;

    // --- 1. exact geometry: the result matches the planet at the reference time
    {
        DerotateResult r = derotate(clean, base);
        CHECK(r.ok);
        CHECK(r.referenceFrame == 3);
        CHECK_NEAR((r.referenceJd - jup.t0) * 86400.0, 0.0, 1e-3);
        CHECK(r.image.width() == kSize && r.image.channels() == 1);
        CHECK(r.channels.size() == 1 && r.channels[0].frames == 7);
        double inner = rmsDiff(r.image, truth, refG, polar, 0.0, 0.85);
        double limb = rmsDiff(r.image, truth, refG, polar, 0.85, 1.05);
        double sky = rmsDiff(r.image, truth, refG, polar, 1.1, 3.0);
        // What a frame taken 9 minutes early looks like against the truth, for scale.
        Image early = jup.render(clean[0].jdUtc, refG, 1.0, 0.0, 0).luminance();
        double undone = rmsDiff(early, truth, refG, polar, 0.0, 0.85);
        std::printf("derotated: inner %.5f  limb %.5f  sky %.6f   |  frame 9 min early, not derotated: %.5f\n", inner, limb, sky, undone);
        CHECK(inner < 0.0015);
        CHECK(limb < 0.012);
        CHECK(sky < 1e-4);
        CHECK(undone > 8.0 * inner);
        // Rotation applied: 9 minutes of System II is 5.44 deg, earlier frames negative.
        CHECK_NEAR(r.frames[0].rotationDeg, -870.27 * 9.0 / 1440.0, 0.02);
        CHECK_NEAR(r.frames[6].rotationDeg, 870.27 * 9.0 / 1440.0, 0.02);
        CHECK_NEAR(r.frames[0].minutesFromReference, -9.0, 1e-6);
        CHECK(r.limbDarkeningUsed > 0.5 && r.limbDarkeningUsed < 0.9);
        CHECK(r.systemUsed == 1);

        // Turning the planet the wrong way must be clearly worse.
        DerotateOptions wrong = base;
        wrong.customRate = -870.27;
        DerotateResult w = derotate(clean, wrong);
        CHECK(w.ok);
        CHECK(rmsDiff(w.image, truth, refG, polar, 0.0, 0.85) > 5.0 * inner);
        // The same rate given by hand as System II gives the same picture.
        DerotateOptions same = base;
        same.customRate = 870.270;
        DerotateResult s = derotate(clean, same);
        CHECK(rmsDiff(s.image, r.image, refG, polar, 0.0, 1.05) < 2e-4);
        // System I differs from System II by only 0.05 deg over 9 minutes.
        DerotateOptions sys1 = base;
        sys1.system = 0;
        DerotateResult s1 = derotate(clean, sys1);
        CHECK(s1.systemUsed == 0);
        CHECK(rmsDiff(s1.image, r.image, refG, polar, 0.0, 0.85) < 1e-3);

        // Interpolation choices all work.
        for (Interpolation ip : {Interpolation::Bilinear, Interpolation::Lanczos3}) {
            DerotateOptions o = base;
            o.interpolation = ip;
            DerotateResult ri = derotate(clean, o);
            CHECK(ri.ok && rmsDiff(ri.image, truth, refG, polar, 0.0, 0.85) < 0.002);
        }
        // Another reference time: compare with the planet rendered at that time.
        DerotateOptions later = base;
        later.referenceJd = jup.t0 + 4.0 * minute;
        DerotateResult rl = derotate(clean, later);
        CHECK(rl.ok && rl.referenceFrame == 4);
        Image truthLater = jup.render(later.referenceJd, clean[4].geometry, 1.0, 0.0, 0).luminance();
        CHECK(rmsDiff(rl.image, truthLater, clean[4].geometry, polar, 0.0, 0.85) < 0.002);

        // North-up output: centred, upright.
        DerotateOptions up = base;
        up.northUp = true;
        up.outputWidth = 360;
        up.outputHeight = 330;
        DerotateResult ru = derotate(clean, up);
        CHECK(ru.ok && ru.image.width() == 360 && ru.image.height() == 330);
        CHECK_NEAR(ru.geometry.cx, 179.5, 1e-9);
        CHECK_NEAR(ru.geometry.cy, 164.5, 1e-9);
        CHECK_NEAR(ru.geometry.northAngle, 0.0, 1e-12);
        ImageGeometry gu = ru.geometry;
        RotationModel model(Planet::Jupiter, -1, std::numeric_limits<double>::quiet_NaN(), jup.t0);
        RenderOptions ro;
        ro.psfSigma = 1.0;
        Image truthUp = renderPlanet(jup.map, jup.shape, model.at(jup.t0).orientation, gu, 360, 330,
                                     computeEphemeris(Planet::Jupiter, jup.t0).sunDirView, ro).luminance();
        CHECK(rmsDiff(ru.image, truthUp, gu, polar, 0.0, 0.85) < 0.002);
    }

    // --- 2. alignment: wrong centres are found and corrected
    {
        std::vector<Frame> off = clean;
        std::vector<double> dx(7, 0.0), dy(7, 0.0);
        for (int i = 0; i < 7; ++i) {
            if (i == 3) continue;  // the reference frame stays put
            dx[i] = 2.5 * uni(rng);
            dy[i] = 2.5 * uni(rng);
            off[i].geometry.cx += dx[i];
            off[i].geometry.cy += dy[i];
        }
        DerotateOptions o = base;
        DerotateResult bad = derotate(off, o);
        o.refineAlignment = true;
        DerotateResult good = derotate(off, o);
        CHECK(bad.ok && good.ok);
        double worst = 0;
        for (int i = 0; i < 7; ++i) {
            worst = std::max(worst, std::fabs(good.frames[i].shiftX + dx[i]));
            worst = std::max(worst, std::fabs(good.frames[i].shiftY + dy[i]));
        }
        double eBad = rmsDiff(bad.image, truth, refG, polar, 0.0, 0.85);
        double eGood = rmsDiff(good.image, truth, refG, polar, 0.0, 0.85);
        std::printf("alignment: worst remaining centre error %.3f px; rms %.5f -> %.5f\n", worst, eBad, eGood);
        CHECK(worst < 0.15);
        CHECK(eGood < 0.002);
        CHECK(eBad > 2.0 * eGood);
        CHECK(good.frames[3].shiftX == 0.0 && good.frames[3].shiftY == 0.0);
        // Correct centres are left where they are (to a small fraction of a pixel).
        DerotateResult keep = derotate(clean, o);
        for (int i = 0; i < 7; ++i) {
            CHECK_NEAR(keep.frames[i].shiftX, 0.0, 0.08);
            CHECK_NEAR(keep.frames[i].shiftY, 0.0, 0.08);
        }
    }

    // --- 2b. an error shared by all images hardly matters
    {
        // What the automatic measurement typically does away from opposition:
        // radius a pixel too small, centre pulled 0.7 px to one side, the
        // same in every image.
        std::vector<Frame> biased = clean;
        for (Frame& f : biased) {
            f.geometry.radius -= 1.0;
            f.geometry.cx += 0.7;
        }
        DerotateOptions o = base;
        o.refineAlignment = true;
        DerotateResult r = derotate(biased, o);
        CHECK(r.ok);
        double e = rmsDiff(r.image, truth, refG, polar, 0.0, 0.85);
        std::printf("common measurement error (radius -1 px, centre +0.7 px): rms %.5f\n", e);
        CHECK(e < 0.001);
    }

    // --- 3. noise goes down, brightness differences are evened out
    {
        std::vector<Frame> noisy = clean;
        for (int i = 0; i < 7; ++i) {
            noisy[i].image = jup.render(noisy[i].jdUtc, noisy[i].geometry, 1.0, 0.03, 100 + i).luminance();
        }
        // One frame came out 20% darker.
        for (size_t p = 0; p < noisy[5].image.size(); ++p) noisy[5].image.data()[p] *= 0.8f;
        DerotateOptions o = base;
        o.refineAlignment = true;
        DerotateResult r = derotate(noisy, o);
        CHECK(r.ok);
        double single = rmsDiff(noisy[3].image, truth, refG, polar, 0.0, 0.85);
        double stacked = rmsDiff(r.image, truth, refG, polar, 0.0, 0.85);
        std::printf("noise: single frame %.5f, 7 derotated %.5f\n", single, stacked);
        CHECK(stacked < 0.5 * single);
        // The direction of rotation is still recognised through the noise.
        double cg = 0, cf = 0;
        CHECK(checkRotationSense(noisy, base, &cg, &cf) == 1);
        std::printf("noise: rotation sense correlation %.3f as given, %.3f swapped\n", cg, cf);
        CHECK_NEAR(r.frames[5].brightnessScale, 1.25, 0.01);
        CHECK_NEAR(r.frames[2].brightnessScale, 1.0, 0.01);
        // Without the matching the dark frame drags the result down.
        o.normalizeBrightness = false;
        DerotateResult rn = derotate(noisy, o);
        CHECK(rmsDiff(rn.image, truth, refG, polar, 0.0, 0.85) > 1.5 * stacked);
    }

    // --- 4. R, G and B taken one after the other become one colour image
    {
        const ImageGeometry g = centred(radius, -20.0, true);
        std::vector<Frame> rgb;
        const char* names[3] = {"r", "G", " B "};  // labels are trimmed and upper-cased
        for (int i = 0; i < 3; ++i) {
            Frame f;
            f.jdUtc = jup.t0 + (i - 1) * 2.5 * minute;
            f.geometry = g;
            f.image = jup.render(f.jdUtc, g, 1.0, 0.0, 0).channel(i);
            f.channel = names[i];
            f.name = names[i];
            rgb.push_back(f);
        }
        DerotateOptions o = base;
        DerotateResult r = derotate(rgb, o);
        CHECK(r.ok);
        CHECK(r.channels.size() == 3);
        CHECK(r.image.channels() == 3);
        Image truthColour = jup.render(jup.t0, g, 1.0, 0.0, 0);
        double e = rmsDiff(r.image, truthColour, g, polar, 0.0, 0.85);
        // Simply stacking the three channels leaves colour fringes on every feature.
        Image naive(kSize, kSize, 3);
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x) naive.at(x, y, c) = rgb[c].image.at(x, y);
        double eNaive = rmsDiff(naive, truthColour, g, polar, 0.0, 0.85);
        std::printf("RGB: derotated %.5f, channels stacked as they are %.5f\n", e, eNaive);
        CHECK(e < 0.003);
        CHECK(eNaive > 4.0 * e);
        // With alignment across filters switched on it still holds.
        o.refineAlignment = true;
        DerotateResult ra = derotate(rgb, o);
        CHECK(ra.ok && rmsDiff(ra.image, truthColour, g, polar, 0.0, 0.85) < 0.004);

        // Filters that do not form an RGB set: one output per filter, no assembled image.
        std::vector<Frame> mixed = {rgb[0], rgb[1]};
        mixed[0].channel = "IR";
        mixed[1].channel = "";
        DerotateResult rm = derotate(mixed, base);
        CHECK(rm.ok && rm.channels.size() == 2 && rm.image.empty());

        // Colour frames go through with all three channels.
        std::vector<Frame> colour;
        for (int i = 0; i < 3; ++i) {
            Frame f;
            f.jdUtc = jup.t0 + (i - 1) * 4.0 * minute;
            f.geometry = g;
            f.image = jup.render(f.jdUtc, g, 1.0, 0.0, 0);
            colour.push_back(f);
        }
        DerotateResult rc = derotate(colour, base);
        CHECK(rc.ok && rc.image.channels() == 3);
        CHECK(rmsDiff(rc.image, truthColour, g, polar, 0.0, 0.85) < 0.002);
        // Mono and colour under the same filter label cannot be combined.
        colour[1].image = colour[1].image.luminance();
        CHECK(!derotate(colour, base).ok);
    }

    // --- 5. Saturn: the globe is derotated, the rings are left alone
    {
        Scene sat(Planet::Saturn, "2017-06-15T01:08:00");
        const ImageGeometry g = centred(75.0, 30.0);
        std::vector<Frame> fr;
        for (int i = 0; i < 5; ++i) {
            Frame f;
            f.jdUtc = sat.t0 + (i - 2) * 5.0 * minute;
            f.geometry = g;
            f.image = sat.render(f.jdUtc, g, 1.0, 0.0, 0);
            fr.push_back(f);
        }
        DerotateOptions o = base;
        o.planet = Planet::Saturn;
        DerotateResult r = derotate(fr, o);
        CHECK(r.ok);
        Image truthSat = sat.render(sat.t0, g, 1.0, 0.0, 0);
        const double polarSat = projectedPolarRadius(sat.shape, computeEphemeris(Planet::Saturn, sat.t0).de);
        double globe = rmsDiff(r.image, truthSat, g, polarSat, 0.0, 0.5);
        double all = rmsDiff(r.image, truthSat, g, 1.0, 0.0, 2.4);
        double early = rmsDiff(fr[0].image, truthSat, g, polarSat, 0.0, 0.5);
        std::printf("Saturn: globe %.5f (frame 10 min early %.5f), whole picture with rings %.5f\n", globe, early, all);
        CHECK(globe < 0.004);
        CHECK(early > 3.0 * globe);
        CHECK(all < 0.006);
    }

    // --- 6. the rotation model
    {
        RotationModel sys2(Planet::Jupiter, 1, std::numeric_limits<double>::quiet_NaN(), jup.t0);
        RotationModel sys3(Planet::Jupiter, 2, std::numeric_limits<double>::quiet_NaN(), jup.t0);
        RotationModel custom(Planet::Jupiter, -1, 870.270, jup.t0);
        PhysicalEphemeris e = computeEphemeris(Planet::Jupiter, jup.t0);
        CHECK_NEAR(wrap180(sys2.centralMeridian(jup.t0) - e.cm[1]), 0.0, 1e-9);
        CHECK_NEAR(wrap180(sys3.centralMeridian(jup.t0) - e.cm[2]), 0.0, 1e-9);
        // A custom rate starts from the IAU longitude at the reference time ...
        CHECK_NEAR(wrap180(custom.centralMeridian(jup.t0) - e.cm[2]), 0.0, 1e-9);
        // ... and then advances like the system with that rate.
        double a = wrap180(custom.centralMeridian(jup.t0 + 20 * minute) - custom.centralMeridian(jup.t0));
        double b = wrap180(sys2.centralMeridian(jup.t0 + 20 * minute) - sys2.centralMeridian(jup.t0));
        CHECK_NEAR(a, b, 1e-6);
        CHECK_NEAR(b, 870.27 * 20.0 / 1440.0, 0.02);
        // The Sun is roughly behind the observer at this date (phase angle 0.1 deg).
        Pose p = sys2.at(jup.t0);
        CHECK(dot(p.sunBody, p.orientation.observerDir()) > 0.9999);
        // Uranus turns the other way; its longitudes still increase with time.
        RotationModel ura(Planet::Uranus, -1, std::numeric_limits<double>::quiet_NaN(), jup.t0);
        double du = wrap180(ura.centralMeridian(jup.t0 + 20 * minute) - ura.centralMeridian(jup.t0));
        CHECK_NEAR(du, 501.1600928 * 20.0 / 1440.0, 0.02);
    }

    // --- 7. north and south swapped is noticed from the way the features move
    {
        double given = 0, flipped = 0;
        CHECK(checkRotationSense(clean, base, &given, &flipped) == 1);
        std::printf("rotation sense: correlation %.3f as given, %.3f with the poles swapped\n", given, flipped);
        CHECK(given > 0.8 && flipped < given - 0.3);
        std::vector<Frame> swapped = clean;
        for (Frame& f : swapped) f.geometry.northAngle = wrap180(f.geometry.northAngle + 180.0);
        CHECK(checkRotationSense(swapped, base) == -1);
        // derotate() says so in a warning.
        DerotateResult r = derotate(swapped, base);
        CHECK(r.ok && !r.warnings.empty());
        CHECK(derotate(clean, base).warnings.empty());
        // Swapping the poles and the mirror setting together leaves the motion as it was.
        for (Frame& f : swapped) f.geometry.mirror = !f.geometry.mirror;
        CHECK(checkRotationSense(swapped, base) == 1);
        // Nothing to go on: one image, or two images seconds apart.
        CHECK(checkRotationSense({clean[0]}, base) == 0);
        std::vector<Frame> close = {clean[3], clean[3]};
        close[1].jdUtc += 5.0 / 86400.0;
        CHECK(checkRotationSense(close, base) == 0);
        // Different filters are not compared with each other.
        std::vector<Frame> two = {clean[0], clean[6]};
        two[0].channel = "R";
        two[1].channel = "B";
        CHECK(checkRotationSense(two, base) == 0);
        CHECK(checkRotationSense({}, base) == 0);
    }

    // --- 8. bad input is refused with a reason
    {
        CHECK(!derotate({}, base).ok);
        std::vector<Frame> f = {clean[0]};
        f[0].geometry.radius = 0;
        DerotateResult r = derotate(f, base);
        CHECK(!r.ok && !r.error.empty());
        f = {clean[0]};
        f[0].jdUtc = 0;
        CHECK(!derotate(f, base).ok);
        f = {clean[0]};
        f[0].image = Image();
        CHECK(!derotate(f, base).ok);
        // A single good frame is simply passed through.
        f = {clean[3]};
        r = derotate(f, base);
        CHECK(r.ok);
        CHECK(rmsDiff(r.image, clean[3].image, refG, polar, 0.0, 0.9) < 2e-4);
        // A frame far away in time triggers a warning but not a failure.
        std::vector<Frame> far = {clean[3], clean[0]};
        far[1].jdUtc = jup.t0 - 120 * minute;  // each frame ends up 36 deg from the mean time
        r = derotate(far, base);
        if (!r.ok) std::printf("  far frame: %s\n", r.error.c_str());
        CHECK(r.ok && !r.warnings.empty());
    }

    return testing::finish("test_derotate");
}
