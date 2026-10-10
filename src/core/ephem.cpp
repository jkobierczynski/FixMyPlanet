// SPDX-License-Identifier: GPL-3.0-or-later
#include "ephem.h"

#include <cmath>

#include "timeutil.h"
#include "vsop87a_small.h"

namespace fixmyplanet {

namespace {

constexpr double kLightSpeedAUPerDay = 173.1446326847;
constexpr double kAUKm = 149597870.7;

// VSOP87A gives rectangular coordinates in the dynamical ecliptic and equinox
// of J2000. This rotation (from the VSOP87 documentation) takes them to the
// equatorial FK5 J2000 frame, which is the ICRF to within 0.03 arcsec.
Vec3 eclipticToEquatorial(const double v[3]) {
    return {
        v[0] + 0.000000440360 * v[1] - 0.000000190919 * v[2],
        -0.000000479966 * v[0] + 0.917482137087 * v[1] - 0.397776982902 * v[2],
        0.397776982902 * v[1] + 0.917482137087 * v[2],
    };
}

// ICRF -> body-fixed rotation for pole (ra0, dec0) and prime meridian W, all in degrees.
Mat3 icrfToBody(double ra0, double dec0, double w) {
    return rotZ(-w * kDegToRad) * rotX(-(90.0 - dec0) * kDegToRad) * rotZ(-(ra0 + 90.0) * kDegToRad);
}

// Direction of the celestial pole of date expressed in J2000 coordinates
// (IAU 1976 precession; nutation is ignored, it changes the position angle by
// less than 0.01 deg).
Vec3 poleOfDate(double T) {
    const double arcsec = kDegToRad / 3600.0;
    double zeta = (2306.2181 * T + 0.30188 * T * T + 0.017998 * T * T * T) * arcsec;
    double theta = (2004.3109 * T - 0.42665 * T * T - 0.041833 * T * T * T) * arcsec;
    return {std::sin(theta) * std::cos(zeta), -std::sin(theta) * std::sin(zeta), std::cos(theta)};
}

double positionAngle(const Vec3& earthToPlanet, const Vec3& celestialNorth, const Vec3& bodyPole) {
    Vec3 e = normalized(earthToPlanet);
    Vec3 east = normalized(cross(celestialNorth, e));
    Vec3 north = cross(e, east);
    return wrap360(std::atan2(dot(bodyPole, east), dot(bodyPole, north)) * kRadToDeg);
}

}  // namespace

Vec3 heliocentricEarth(double jdTT) {
    double t = (jdTT - kJ2000) / 365250.0;
    double v[3];
    vsop87a_small::getEarth(t, v);
    return eclipticToEquatorial(v);
}

Vec3 heliocentricPosition(Planet planet, double jdTT) {
    double t = (jdTT - kJ2000) / 365250.0;
    double v[3] = {0, 0, 0};
    switch (planet) {
        case Planet::Mars: vsop87a_small::getMars(t, v); break;
        case Planet::Jupiter: vsop87a_small::getJupiter(t, v); break;
        case Planet::Saturn: vsop87a_small::getSaturn(t, v); break;
        case Planet::Uranus: vsop87a_small::getUranus(t, v); break;
        case Planet::Neptune: vsop87a_small::getNeptune(t, v); break;
    }
    return eclipticToEquatorial(v);
}

PhysicalEphemeris computeEphemeris(Planet planet, double jdUtc) {
    const PlanetInfo& info = planetInfo(planet);
    PhysicalEphemeris r;
    r.planet = planet;
    r.jdUtc = jdUtc;
    r.jdTT = utcToTT(jdUtc);

    // Light-time iteration: the planet is seen where it was when the light left.
    Vec3 earth = heliocentricEarth(r.jdTT);
    double tau = 0.0;
    Vec3 pl, geo;
    for (int i = 0; i < 4; ++i) {
        pl = heliocentricPosition(planet, r.jdTT - tau);
        geo = pl - earth;
        tau = norm(geo) / kLightSpeedAUPerDay;
    }
    pl = heliocentricPosition(planet, r.jdTT - tau);
    geo = pl - earth;

    r.distanceAU = norm(geo);
    r.sunDistanceAU = norm(pl);
    r.lightTimeMinutes = tau * 1440.0;
    r.raDeg = wrap360(std::atan2(geo.y, geo.x) * kRadToDeg);
    r.decDeg = std::asin(geo.z / r.distanceAU) * kRadToDeg;
    r.diameterArcsec = 2.0 * std::asin(info.equatorialRadiusKm / (r.distanceAU * kAUKm)) * kRadToDeg * 3600.0;

    Vec3 toEarth = normalized(geo * -1.0);  // planet -> Earth
    Vec3 toSun = normalized(pl * -1.0);     // planet -> Sun
    r.phaseAngleDeg = std::acos(std::fmax(-1.0, std::fmin(1.0, dot(toEarth, toSun)))) * kRadToDeg;

    // Orientation of the body when the light left it.
    double d = r.jdTT - tau - kJ2000;
    double T = d / 36525.0;
    double ra0, dec0;
    iauPole(planet, T, ra0, dec0);

    const double ratio = info.polarRatio();
    const double ratio2 = ratio * ratio;
    r.cm.resize(info.systems.size());
    r.subSolarLon.resize(info.systems.size());
    r.westLongitude = info.systems[info.iauSystem].rate > 0;

    for (size_t s = 0; s < info.systems.size(); ++s) {
        Mat3 m = icrfToBody(ra0, dec0, primeMeridian(planet, static_cast<int>(s), d));
        Vec3 eb = m * toEarth;
        Vec3 sb = m * toSun;
        double lonE = std::atan2(eb.y, eb.x) * kRadToDeg;
        double lonS = std::atan2(sb.y, sb.x) * kRadToDeg;
        r.cm[s] = wrap360(r.westLongitude ? -lonE : lonE);
        r.subSolarLon[s] = wrap360(r.westLongitude ? -lonS : lonS);
        if (static_cast<int>(s) == info.iauSystem) {
            double de = std::asin(std::fmax(-1.0, std::fmin(1.0, eb.z)));
            double ds = std::asin(std::fmax(-1.0, std::fmin(1.0, sb.z)));
            r.de = de * kRadToDeg;
            r.ds = ds * kRadToDeg;
            r.subEarthLatGraphic = std::atan(std::tan(de) / ratio2) * kRadToDeg;
            r.subSolarLatGraphic = std::atan(std::tan(ds) / ratio2) * kRadToDeg;

            // View frame: z towards the observer, y along the projected pole.
            Vec3 zv = eb;
            Vec3 pole{0, 0, 1};
            Vec3 yv = normalized(pole - zv * dot(pole, zv));
            Vec3 xv = cross(yv, zv);
            r.sunDirView = {dot(sb, xv), dot(sb, yv), dot(sb, zv)};
        }
    }

    Vec3 bodyPole{std::cos(dec0 * kDegToRad) * std::cos(ra0 * kDegToRad),
                  std::cos(dec0 * kDegToRad) * std::sin(ra0 * kDegToRad), std::sin(dec0 * kDegToRad)};
    r.poleAngleJ2000 = positionAngle(geo, Vec3{0, 0, 1}, bodyPole);
    r.poleAngle = positionAngle(geo, poleOfDate((r.jdTT - kJ2000) / 36525.0), bodyPole);
    return r;
}

}  // namespace fixmyplanet
