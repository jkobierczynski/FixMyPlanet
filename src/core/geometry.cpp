// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry.h"

#include <cmath>

namespace fixmyplanet {

void ImageGeometry::viewToPixel(double x, double y, double& u, double& v) const {
    double c = std::cos(northAngle * kDegToRad), s = std::sin(northAngle * kDegToRad);
    double xm = mirror ? -x : x;
    u = cx + radius * (xm * c - y * s);
    v = cy - radius * (xm * s + y * c);
}

void ImageGeometry::pixelToView(double u, double v, double& x, double& y) const {
    double c = std::cos(northAngle * kDegToRad), s = std::sin(northAngle * kDegToRad);
    double xp = (u - cx) / radius;
    double yp = -(v - cy) / radius;
    double xm = xp * c + yp * s;
    y = -xp * s + yp * c;
    x = mirror ? -xm : xm;
}

Orientation::Orientation(double lonEastDeg, double deDeg) : lonEast_(lonEastDeg), de_(deDeg) {
    double lon = lonEastDeg * kDegToRad, de = deDeg * kDegToRad;
    dir_ = {std::cos(de) * std::cos(lon), std::cos(de) * std::sin(lon), std::sin(de)};
    Vec3 zv = dir_;
    Vec3 pole{0, 0, 1};
    Vec3 yv = pole - zv * dot(pole, zv);
    if (norm(yv) < 1e-12) {
        // Looking straight down the axis: any direction will do for "up".
        yv = {-std::cos(lon), -std::sin(lon), 0};
        if (de < 0) yv = yv * -1.0;
    }
    yv = normalized(yv);
    Vec3 xv = cross(yv, zv);
    bodyToView_.m[0][0] = xv.x; bodyToView_.m[0][1] = xv.y; bodyToView_.m[0][2] = xv.z;
    bodyToView_.m[1][0] = yv.x; bodyToView_.m[1][1] = yv.y; bodyToView_.m[1][2] = yv.z;
    bodyToView_.m[2][0] = zv.x; bodyToView_.m[2][1] = zv.y; bodyToView_.m[2][2] = zv.z;
    viewToBody_ = transposed(bodyToView_);
}

Orientation Orientation::fromCentralMeridian(double cmDeg, double deDeg, bool westLongitude) {
    return Orientation(westLongitude ? -cmDeg : cmDeg, deDeg);
}

Vec3 surfaceNormal(const Vec3& b, const BodyShape& shape) {
    double c2 = shape.polarRatio * shape.polarRatio;
    return normalized(Vec3{b.x, b.y, b.z / c2});
}

bool Orientation::surfacePoint(double x, double y, const BodyShape& shape, Vec3& body, double& mu) const {
    const double inv = 1.0 / (shape.polarRatio * shape.polarRatio);
    Vec3 q = viewToBody_ * Vec3{x, y, 0};
    const Vec3& d = dir_;
    double a = d.x * d.x + d.y * d.y + d.z * d.z * inv;
    double b = q.x * d.x + q.y * d.y + q.z * d.z * inv;
    double c = q.x * q.x + q.y * q.y + q.z * q.z * inv - 1.0;
    double disc = b * b - a * c;
    if (disc < 0) return false;
    double z = (-b + std::sqrt(disc)) / a;
    body = q + d * z;
    mu = dot(surfaceNormal(body, shape), d);
    return true;
}

double Orientation::emissionCos(const Vec3& body, const BodyShape& shape) const {
    return dot(surfaceNormal(body, shape), dir_);
}

bool Orientation::ringPoint(double x, double y, const BodyShape& shape, double& zView) const {
    if (!shape.hasRings || std::fabs(dir_.z) < 1e-6) return false;
    Vec3 q = viewToBody_ * Vec3{x, y, 0};
    double z = -q.z / dir_.z;
    Vec3 p = q + dir_ * z;
    double rho = std::sqrt(p.x * p.x + p.y * p.y);
    if (rho < shape.ringInner || rho > shape.ringOuter) return false;
    zView = z;
    return true;
}

void bodyToLonLat(const Vec3& b, const BodyShape& shape, double& lonEastDeg, double& latGraphicDeg) {
    double c2 = shape.polarRatio * shape.polarRatio;
    lonEastDeg = std::atan2(b.y, b.x) * kRadToDeg;
    latGraphicDeg = std::atan2(b.z / c2, std::sqrt(b.x * b.x + b.y * b.y)) * kRadToDeg;
}

Vec3 lonLatToBody(double lonEastDeg, double latGraphicDeg, const BodyShape& shape) {
    double c = shape.polarRatio, c2 = c * c;
    double lat = latGraphicDeg * kDegToRad, lon = lonEastDeg * kDegToRad;
    // Planetocentric latitude of the same surface point.
    double latC = std::atan2(c2 * std::sin(lat), std::cos(lat));
    double cl = std::cos(latC), sl = std::sin(latC);
    double r = 1.0 / std::sqrt(cl * cl + sl * sl / c2);
    return {r * cl * std::cos(lon), r * cl * std::sin(lon), r * sl};
}

double projectedPolarRadius(const BodyShape& shape, double deDeg) {
    double s = std::sin(deDeg * kDegToRad), c = std::cos(deDeg * kDegToRad);
    return std::sqrt(s * s + shape.polarRatio * shape.polarRatio * c * c);
}

}  // namespace fixmyplanet
