// SPDX-License-Identifier: GPL-3.0-or-later
#include "planets.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "math3.h"

namespace fixmyplanet {

// Sources for the constants below:
//  * Shape and IAU rotation models: Archinal et al., "Report of the IAU
//    Working Group on Cartographic Coordinates and Rotational Elements: 2015",
//    Celest. Mech. Dyn. Astr. 130:22 (2018), and the 2009 report it updates.
//  * Jupiter Systems I and II: Explanatory Supplement to the Astronomical
//    Almanac; the rates are the conventional 877.900 and 870.270 deg/day.
//  * Saturn ring radii: NASA Saturn ring fact sheet values.
// The values are checked against JPL Horizons in tests/test_ephem.cpp.

static std::vector<PlanetInfo> buildTable() {
    std::vector<PlanetInfo> t;

    PlanetInfo mars;
    mars.id = Planet::Mars;
    mars.name = "Mars";
    mars.equatorialRadiusKm = 3396.19;
    mars.polarRadiusKm = 3376.20;
    mars.systems = {{"III", 176.049863, 350.891982443297, "solid surface (IAU)"}};
    mars.defaultSystem = 0;
    mars.iauSystem = 0;
    mars.hasRings = false;
    mars.ringInnerKm = mars.ringOuterKm = 0;
    t.push_back(mars);

    PlanetInfo jup;
    jup.id = Planet::Jupiter;
    jup.name = "Jupiter";
    jup.equatorialRadiusKm = 71492.0;
    jup.polarRadiusKm = 66854.0;
    jup.systems = {
        {"I", 67.1, 877.900, "equatorial zone, within about 10 deg of the equator"},
        {"II", 43.3, 870.270, "higher latitudes, including the Great Red Spot"},
        {"III", 284.95, 870.5360000, "magnetic field / radio emission (IAU)"},
    };
    jup.defaultSystem = 1;
    jup.iauSystem = 2;
    jup.hasRings = false;
    jup.ringInnerKm = jup.ringOuterKm = 0;
    t.push_back(jup);

    PlanetInfo sat;
    sat.id = Planet::Saturn;
    sat.name = "Saturn";
    sat.equatorialRadiusKm = 60268.0;
    sat.polarRadiusKm = 54364.0;
    sat.systems = {{"III", 38.90, 810.7939024, "radio emission (IAU)"}};
    sat.defaultSystem = 0;
    sat.iauSystem = 0;
    sat.hasRings = true;
    sat.ringInnerKm = 74658.0;
    sat.ringOuterKm = 136775.0;
    sat.ringMarksKm = {92000.0, 117580.0, 122170.0};
    t.push_back(sat);

    PlanetInfo ura;
    ura.id = Planet::Uranus;
    ura.name = "Uranus";
    ura.equatorialRadiusKm = 25559.0;
    ura.polarRadiusKm = 24973.0;
    ura.systems = {{"III", 203.81, -501.1600928, "magnetic field (IAU)"}};
    ura.defaultSystem = 0;
    ura.iauSystem = 0;
    ura.hasRings = false;
    ura.ringInnerKm = ura.ringOuterKm = 0;
    t.push_back(ura);

    PlanetInfo nep;
    nep.id = Planet::Neptune;
    nep.name = "Neptune";
    nep.equatorialRadiusKm = 24764.0;
    nep.polarRadiusKm = 24341.0;
    nep.systems = {{"III", 249.978, 541.1397757, "atmospheric features (IAU 2015)"}};
    nep.defaultSystem = 0;
    nep.iauSystem = 0;
    nep.hasRings = false;
    nep.ringInnerKm = nep.ringOuterKm = 0;
    t.push_back(nep);

    return t;
}

static const std::vector<PlanetInfo>& table() {
    static const std::vector<PlanetInfo> t = buildTable();
    return t;
}

const PlanetInfo& planetInfo(Planet p) { return table()[static_cast<int>(p)]; }

const std::vector<Planet>& allPlanets() {
    static const std::vector<Planet> v = {Planet::Mars, Planet::Jupiter, Planet::Saturn, Planet::Uranus,
                                          Planet::Neptune};
    return v;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool planetFromName(const std::string& name, Planet& out) {
    std::string n = lower(name);
    for (const PlanetInfo& info : table()) {
        if (lower(info.name) == n || (n.size() >= 3 && lower(info.name).compare(0, n.size(), n) == 0)) {
            out = info.id;
            return true;
        }
    }
    return false;
}

int systemIndex(const PlanetInfo& info, const std::string& nameIn) {
    std::string n = lower(nameIn);
    if (n == "1") n = "i";
    if (n == "2") n = "ii";
    if (n == "3") n = "iii";
    for (size_t i = 0; i < info.systems.size(); ++i)
        if (lower(info.systems[i].name) == n) return static_cast<int>(i);
    return -1;
}

BodyShape bodyShape(Planet p) {
    const PlanetInfo& info = planetInfo(p);
    BodyShape s;
    s.polarRatio = info.polarRatio();
    s.hasRings = info.hasRings;
    if (info.hasRings) {
        s.ringInner = info.ringInnerKm / info.equatorialRadiusKm;
        s.ringOuter = info.ringOuterKm / info.equatorialRadiusKm;
    }
    return s;
}

// Neptune's pole and prime meridian depend on this angle (degrees).
static double neptuneN(double T) { return 357.85 + 52.316 * T; }
// Long-period argument of the Mars model (degrees). The 2015 model has several
// further terms with amplitudes below 0.0003 deg which are not included.
static double marsPoleArgRa(double T) { return 79.398797 + 0.5042615 * T; }
static double marsPoleArgDec(double T) { return 166.325722 + 0.5042615 * T; }
static double marsArgW(double T) { return 95.391654 + 0.5042615 * T; }

void iauPole(Planet p, double T, double& ra0, double& dec0) {
    switch (p) {
        case Planet::Mars:
            ra0 = 317.269202 - 0.10927547 * T + 0.419057 * std::sin(marsPoleArgRa(T) * kDegToRad);
            dec0 = 54.432516 - 0.05827105 * T + 1.591274 * std::cos(marsPoleArgDec(T) * kDegToRad);
            break;
        case Planet::Jupiter:
            // The periodic terms of the IAU model (below 0.003 deg) are omitted.
            ra0 = 268.056595 - 0.006499 * T;
            dec0 = 64.495303 + 0.002413 * T;
            break;
        case Planet::Saturn:
            ra0 = 40.589 - 0.036 * T;
            dec0 = 83.537 - 0.004 * T;
            break;
        case Planet::Uranus:
            ra0 = 257.311;
            dec0 = -15.175;
            break;
        case Planet::Neptune: {
            double n = neptuneN(T) * kDegToRad;
            ra0 = 299.36 + 0.70 * std::sin(n);
            dec0 = 43.46 - 0.51 * std::cos(n);
            break;
        }
    }
}

double primeMeridian(Planet p, int system, double d) {
    const PlanetInfo& info = planetInfo(p);
    if (system < 0 || system >= static_cast<int>(info.systems.size())) system = info.iauSystem;
    const RotationSystem& s = info.systems[system];
    double w = s.w0 + s.rate * d;
    double T = d / 36525.0;
    if (p == Planet::Neptune) w -= 0.48 * std::sin(neptuneN(T) * kDegToRad);
    if (p == Planet::Mars) w += 0.584542 * std::sin(marsArgW(T) * kDegToRad);
    return w;
}

}  // namespace fixmyplanet
