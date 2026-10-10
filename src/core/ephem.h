// SPDX-License-Identifier: GPL-3.0-or-later
// Physical ephemeris of a planet as seen from the centre of the Earth.
#pragma once

#include <vector>

#include "math3.h"
#include "planets.h"

namespace fixmyplanet {

struct PhysicalEphemeris {
    Planet planet = Planet::Jupiter;
    double jdUtc = 0;
    double jdTT = 0;

    double distanceAU = 0;        // Earth - planet, light-time corrected
    double sunDistanceAU = 0;     // Sun - planet
    double lightTimeMinutes = 0;
    double raDeg = 0, decDeg = 0; // astrometric, J2000
    double diameterArcsec = 0;    // equatorial
    double phaseAngleDeg = 0;     // Sun - planet - Earth

    // Central meridian: planetographic longitude of the sub-Earth point, one
    // value per rotation system of the planet (same order as PlanetInfo).
    std::vector<double> cm;
    // Planetocentric declination of the Earth, "D_E". This is the tilt used
    // for the disc geometry.
    double de = 0;
    // Planetographic latitude of the disc centre (what JPL Horizons prints).
    double subEarthLatGraphic = 0;

    // Same for the Sun.
    std::vector<double> subSolarLon;
    double ds = 0;
    double subSolarLatGraphic = 0;

    // Position angle of the planet's north pole, from celestial north through
    // east. `poleAngle` refers to the equator of date, `poleAngleJ2000` to
    // the J2000 equator.
    double poleAngle = 0;
    double poleAngleJ2000 = 0;

    // Unit vector from the planet towards the Sun in the view frame:
    // x to the right, y along the projected north pole, z towards the observer.
    Vec3 sunDirView;

    // True when planetographic longitude increases to the west (prograde
    // rotation); false for Uranus.
    bool westLongitude = true;
};

PhysicalEphemeris computeEphemeris(Planet planet, double jdUtc);

// Heliocentric position (AU, equatorial J2000) at a TT Julian date.
Vec3 heliocentricPosition(Planet planet, double jdTT);
Vec3 heliocentricEarth(double jdTT);

}  // namespace fixmyplanet
