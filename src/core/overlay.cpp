// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay.h"

#include <algorithm>
#include <cmath>

namespace fixmyplanet {

namespace {

// Append visible runs of a body-frame curve as separate polylines.
template <typename Fn>
void addCurve(std::vector<Curve>& out, CurveKind kind, int samples, const Orientation& o, const BodyShape& shape,
              bool hideBehindGlobe, Fn pointAt) {
    Curve cur{kind, {}};
    for (int i = 0; i <= samples; ++i) {
        Vec3 b = pointAt(static_cast<double>(i) / samples);
        Vec3 v = o.bodyToView(b);
        bool visible;
        if (hideBehindGlobe) {
            Vec3 s;
            double mu;
            visible = !(o.surfacePoint(v.x, v.y, shape, s, mu) && o.bodyToView(s).z > v.z);
        } else {
            visible = o.emissionCos(b, shape) > 0.0;
        }
        if (visible) {
            cur.points.emplace_back(v.x, v.y);
        } else if (!cur.points.empty()) {
            if (cur.points.size() > 1) out.push_back(cur);
            cur.points.clear();
        }
    }
    if (cur.points.size() > 1) out.push_back(cur);
}

}  // namespace

std::vector<Curve> overlayCurves(const BodyShape& shape, double deDeg, const Vec3& sunDirView, bool latitudeGrid,
                                 const std::vector<double>& ringMarks) {
    std::vector<Curve> out;
    Orientation o(0.0, deDeg);
    const double c = shape.polarRatio;
    const double cp = projectedPolarRadius(shape, deDeg);

    // Limb: the projected ellipse.
    Curve limb{CurveKind::Limb, {}};
    for (int i = 0; i <= 360; ++i) {
        double a = i * kDegToRad;
        limb.points.emplace_back(std::cos(a), cp * std::sin(a));
    }
    out.push_back(limb);

    // Terminator: surface points whose normal is perpendicular to the Sun.
    Vec3 sun = o.viewToBody(normalized(sunDirView));
    if (dot(sun, o.observerDir()) < std::cos(1.5 * kDegToRad)) {
        Vec3 e1 = normalized(cross(sun, std::fabs(sun.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
        Vec3 e2 = cross(sun, e1);
        addCurve(out, CurveKind::Terminator, 360, o, shape, false, [&](double t) {
            double a = 2 * kPi * t;
            Vec3 n = e1 * std::cos(a) + e2 * std::sin(a);
            double s = std::sqrt(n.x * n.x + n.y * n.y + c * c * n.z * n.z);
            return Vec3{n.x / s, n.y / s, c * c * n.z / s};
        });
    }

    auto latitudeCircle = [&](CurveKind kind, double latDeg) {
        addCurve(out, kind, 360, o, shape, false, [&](double t) { return lonLatToBody(360.0 * t, latDeg, shape); });
    };
    latitudeCircle(CurveKind::Equator, 0.0);
    if (latitudeGrid)
        for (double lat : {-60.0, -30.0, 30.0, 60.0}) latitudeCircle(CurveKind::Latitude, lat);

    addCurve(out, CurveKind::CentralMeridian, 180, o, shape, false,
             [&](double t) { return lonLatToBody(0.0, -90.0 + 180.0 * t, shape); });

    if (shape.hasRings && std::fabs(deDeg) > 0.3) {
        std::vector<double> radii = {shape.ringInner, shape.ringOuter};
        radii.insert(radii.end(), ringMarks.begin(), ringMarks.end());
        for (double rho : radii)
            addCurve(out, CurveKind::Ring, 720, o, shape, true, [&](double t) {
                double a = 2 * kPi * t;
                return Vec3{rho * std::cos(a), rho * std::sin(a), 0.0};
            });
    }

    // North marker.
    Curve pole{CurveKind::NorthPole, {}};
    Vec3 north{0, 0, c};
    if (o.emissionCos(north, shape) > 0) {
        Vec3 v = o.bodyToView(north);
        pole.points.emplace_back(v.x, v.y);
    } else {
        pole.points.emplace_back(0.0, cp);
    }
    out.push_back(pole);
    return out;
}

namespace {

void plot(Image& img, int x, int y, const float col[3]) {
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return;
    for (int k = 0; k < 3; ++k) img.at(x, y, k) = col[k];
}

void line(Image& img, double x0, double y0, double x1, double y1, const float col[3]) {
    double dx = x1 - x0, dy = y1 - y0;
    int steps = static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))) + 1;
    for (int i = 0; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        plot(img, static_cast<int>(std::lround(x0 + dx * t)), static_cast<int>(std::lround(y0 + dy * t)), col);
    }
}

}  // namespace

Image drawOverlay(const Image& image, const ImageGeometry& g, const std::vector<Curve>& curves) {
    Image out(image.width(), image.height(), 3);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            for (int k = 0; k < 3; ++k) out.at(x, y, k) = image.at(x, y, image.channels() >= 3 ? k : 0);
    for (const Curve& c : curves) {
        float col[3] = {0.2f, 1.0f, 0.3f};
        if (c.kind == CurveKind::Equator) { col[0] = 1.0f; col[1] = 0.85f; col[2] = 0.1f; }
        if (c.kind == CurveKind::CentralMeridian) { col[0] = 0.3f; col[1] = 0.7f; col[2] = 1.0f; }
        if (c.kind == CurveKind::Latitude) { col[0] = 0.6f; col[1] = 0.6f; col[2] = 0.6f; }
        if (c.kind == CurveKind::Ring) { col[0] = 1.0f; col[1] = 0.4f; col[2] = 0.9f; }
        if (c.kind == CurveKind::Terminator) { col[0] = 1.0f; col[1] = 0.3f; col[2] = 0.2f; }
        if (c.kind == CurveKind::NorthPole) {
            double u, v;
            g.viewToPixel(c.points[0].first, c.points[0].second, u, v);
            const float red[3] = {1.0f, 0.2f, 0.2f};
            line(out, u - 5, v, u + 5, v, red);
            line(out, u, v - 5, u, v + 5, red);
            continue;
        }
        for (size_t i = 1; i < c.points.size(); ++i) {
            double u0, v0, u1, v1;
            g.viewToPixel(c.points[i - 1].first, c.points[i - 1].second, u0, v0);
            g.viewToPixel(c.points[i].first, c.points[i].second, u1, v1);
            line(out, u0, v0, u1, v1, col);
        }
    }
    return out;
}

}  // namespace fixmyplanet
