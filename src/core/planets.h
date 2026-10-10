// SPDX-License-Identifier: GPL-3.0-or-later
// Planet constants: shape, rotation models and rings.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"

namespace fixmyplanet {

enum class Planet { Mars = 0, Jupiter, Saturn, Uranus, Neptune };

// A longitude system: W = w0 + rate * d (degrees, d in days from J2000 TDB),
// the angle from the node of the body equator on the ICRF equator to the
// prime meridian, measured along the body equator.
struct RotationSystem {
    std::string name;   // "I", "II", "III"
    double w0 = 0;      // degrees
    double rate = 0;    // degrees per day (negative for retrograde rotation)
    std::string note;   // what the system is tied to
};

struct PlanetInfo {
    Planet id;
    std::string name;
    double equatorialRadiusKm;
    double polarRadiusKm;
    std::vector<RotationSystem> systems;
    int defaultSystem;        // index into systems
    int iauSystem;            // index of the system the IAU model defines
    bool hasRings;
    double ringInnerKm;       // inner edge of the C ring
    double ringOuterKm;       // outer edge of the A ring
    // Extra ring radii drawn in overlays (Cassini division etc.).
    std::vector<double> ringMarksKm;

    double polarRatio() const { return polarRadiusKm / equatorialRadiusKm; }
};

const PlanetInfo& planetInfo(Planet p);
const std::vector<Planet>& allPlanets();

// Case-insensitive lookup by name; returns false if unknown.
bool planetFromName(const std::string& name, Planet& out);

// Index of the named rotation system ("I", "II", "III", "1".."3"), or -1.
int systemIndex(const PlanetInfo& info, const std::string& name);

// Shape of the planet (and its rings) in units of the equatorial radius.
BodyShape bodyShape(Planet p);

// IAU pole direction in the ICRF (degrees) at T Julian centuries TDB from J2000.
void iauPole(Planet p, double T, double& ra0, double& dec0);

// Prime meridian angle W (degrees, not reduced) of the given system at
// d days TDB from J2000. Includes the periodic terms of the IAU model where
// the model has any; these are common to all systems of a planet.
double primeMeridian(Planet p, int system, double d);

}  // namespace fixmyplanet
