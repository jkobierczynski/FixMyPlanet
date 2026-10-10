// SPDX-License-Identifier: GPL-3.0-or-later
// Physical ephemerides compared with JPL Horizons (geocentric observer tables,
// quantities 13, 14, 15, 17, 20 and 24, retrieved 2026-10-09; ephemeris DE441).
#include "ephem.h"
#include "testing.h"
#include "timeutil.h"

using namespace fixmyplanet;

struct Ref {
    Planet planet;
    const char* utc;
    double diameter;     // arcsec
    double obsLon;       // sub-observer longitude, IAU system
    double obsLat;       // sub-observer latitude, planetographic
    double sunLon, sunLat;
    double northPoleAngle;
    double delta;        // AU
    double phase;        // Sun-target-observer angle
};

static const Ref kRefs[] = {
    {Planet::Jupiter, "1992-12-16T00:00:00", 34.82448, 349.614949, -2.837473, 359.543960, -2.510581, 24.7971, 5.66112195207909, 9.9231},
    {Planet::Jupiter, "2009-06-18T00:00:00", 44.06228, 91.659957, 0.517894, 101.679304, -0.023409, 337.7721, 4.47424922227301, 10.0332},
    {Planet::Jupiter, "2022-09-26T21:30:00", 49.87667, 196.249433, 2.944350, 196.219974, 2.570506, 334.6099, 3.95266260059330, 0.3282},
    {Planet::Jupiter, "2026-01-10T22:00:00", 46.58361, 330.866082, 1.590100, 330.744557, 1.643168, 9.9425, 4.23208186436858, 0.1247},
    {Planet::Jupiter, "2030-03-15T03:00:00", 40.36160, 118.303317, -3.802694, 127.751391, -3.565386, 13.3866, 4.88448478158211, 9.4379},
    {Planet::Mars, "2020-08-20T04:30:00", 17.13939, 14.290873, -18.578056, 54.908963, -25.174575, 324.2270, 0.54641971066818, 38.1785},
    {Planet::Mars, "2025-01-16T02:00:00", 14.55027, 345.716147, 10.343734, 346.752531, 12.754360, 348.6672, 0.64365128906803, 2.5920},
    {Planet::Saturn, "2017-06-15T01:00:00", 18.37881, 321.424419, 31.601068, 321.463452, 31.755143, 4.8464, 9.04271890845899, 0.1443},
    {Planet::Saturn, "2026-10-04T23:00:00", 19.70465, 240.245555, -9.104869, 240.329599, -9.445461, 3.2323, 8.43427525181747, 0.2908},
    {Planet::Uranus, "2015-10-12T00:00:00", 3.712604, 295.394126, 31.607712, 295.430980, 31.593648, 256.2521, 18.9843166947868, 0.0365},
    {Planet::Uranus, "2026-11-20T22:00:00", 3.820533, 346.647637, 74.687067, 346.210919, 74.455807, 287.7774, 18.4480139969144, 0.2752},
    {Planet::Neptune, "2015-09-01T00:00:00", 2.358584, 155.362027, -26.761414, 155.362938, -26.790070, 327.8954, 28.9533723553228, 0.0297},
    {Planet::Neptune, "2026-09-25T22:00:00", 2.364926, 193.294926, -19.507078, 193.283069, -19.555032, 316.2135, 28.8757250280491, 0.0490},
};

int main() {
    for (const Ref& r : kRefs) {
        double jd = 0;
        CHECK(parseTime(r.utc, jd));
        PhysicalEphemeris e = computeEphemeris(r.planet, jd);
        const PlanetInfo& info = planetInfo(r.planet);
        const int s = info.iauSystem;
        // Horizons gives apparent directions (with aberration), we use
        // astrometric ones; that and the truncated VSOP87 series account for
        // the few thousandths of a degree allowed here.
        CHECK_NEAR(wrap180(e.cm[s] - r.obsLon), 0.0, 0.01);
        CHECK_NEAR(e.subEarthLatGraphic, r.obsLat, 0.005);
        CHECK_NEAR(wrap180(e.subSolarLon[s] - r.sunLon), 0.0, 0.015);
        CHECK_NEAR(e.subSolarLatGraphic, r.sunLat, 0.005);
        CHECK_NEAR(wrap180(e.poleAngle - r.northPoleAngle), 0.0, 0.02);
        CHECK_NEAR(e.diameterArcsec, r.diameter, 2e-4);
        CHECK_NEAR(e.distanceAU, r.delta, 1e-4);
        CHECK_NEAR(e.phaseAngleDeg, r.phase, 0.02);
        CHECK(e.westLongitude == (r.planet != Planet::Uranus));
    }

    // Astrometric position against Horizons (ICRF RA/Dec).
    double jd = 0;
    parseTime("2022-09-26T21:30:00", jd);
    PhysicalEphemeris e = computeEphemeris(Planet::Jupiter, jd);
    CHECK_NEAR(e.raDeg, 3.73043, 2e-4);
    CHECK_NEAR(e.decDeg, -0.15080, 2e-4);
    parseTime("2026-01-10T22:00:00", jd);
    e = computeEphemeris(Planet::Jupiter, jd);
    CHECK_NEAR(e.raDeg, 111.31832, 2e-4);
    CHECK_NEAR(e.decDeg, 22.25534, 2e-4);

    // Jupiter Systems I and II. Worked example 43.a in Meeus, "Astronomical
    // Algorithms" (1992 December 16, 0h UT) gives D_E = -2.48 deg and, for the
    // geometric (not phase-corrected) disc, central meridians near 267.6 deg
    // (System I) and 72.3 deg (System II). Published definitions of these two
    // systems agree only to about 0.05 deg, hence the tolerance.
    parseTime("1992-12-16T00:00:00", jd);
    e = computeEphemeris(Planet::Jupiter, jd);
    CHECK_NEAR(e.de, -2.48, 0.01);
    CHECK_NEAR(e.cm[0], 267.63, 0.1);
    CHECK_NEAR(e.cm[1], 72.31, 0.1);
    CHECK_NEAR(e.poleAngle, 24.80, 0.01);

    // The systems drift apart at the difference of their rates: over 10 hours
    // System I gains 7.63 deg/day * 10/24 on System II.
    PhysicalEphemeris e2 = computeEphemeris(Planet::Jupiter, jd + 10.0 / 24.0);
    double gain = wrap180((e2.cm[0] - e.cm[0]) - (e2.cm[1] - e.cm[1]));
    CHECK_NEAR(gain, (877.900 - 870.270) * 10.0 / 24.0, 1e-3);
    // After one System II rotation (360 / 870.27 days = 9h 55m 40.7s) the CM
    // is back where it was, except for the small change in the direction
    // from which the Earth sees Jupiter during those ten hours.
    PhysicalEphemeris e3 = computeEphemeris(Planet::Jupiter, jd + 360.0 / 870.270);
    CHECK_NEAR(wrap180(e3.cm[1] - e.cm[1]), 0.0, 0.05);

    // Name lookups.
    Planet p;
    CHECK(planetFromName("Jupiter", p) && p == Planet::Jupiter);
    CHECK(planetFromName("sat", p) && p == Planet::Saturn);
    CHECK(!planetFromName("pluto", p));
    CHECK(systemIndex(planetInfo(Planet::Jupiter), "ii") == 1);
    CHECK(systemIndex(planetInfo(Planet::Jupiter), "3") == 2);
    CHECK(systemIndex(planetInfo(Planet::Saturn), "II") == -1);

    return testing::finish("test_ephem");
}
