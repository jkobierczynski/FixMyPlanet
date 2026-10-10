// SPDX-License-Identifier: GPL-3.0-or-later
#include "measure.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

namespace fixmyplanet {

// ---------------------------------------------------------------- outline --

OutlineModel::OutlineModel(const BodyShape& shape, double deDeg, const Vec3& sunDirView, bool includeRings) {
    const int n = 1440;
    table_.resize(n);
    terminator_.assign(n, 0);
    Orientation orient(0.0, deDeg);
    const Vec3 sunBody = orient.viewToBody(normalized(sunDirView));
    const double cp = projectedPolarRadius(shape, deDeg);
    const double sinDe = std::fabs(std::sin(deDeg * kDegToRad));
    const bool rings = includeRings && shape.hasRings && sinDe > 0.02;

    auto lit = [&](double x, double y) {
        Vec3 body;
        double mu;
        if (!orient.surfacePoint(x, y, shape, body, mu)) return false;
        return dot(surfaceNormal(body, shape), sunBody) > 0.0;
    };

    for (int i = 0; i < n; ++i) {
        double psi = 2.0 * kPi * i / n;
        double cx = std::cos(psi), sy = std::sin(psi);
        double limb = 1.0 / std::sqrt(cx * cx + sy * sy / (cp * cp));
        double r = limb;
        double inside = limb * (1.0 - 1e-7);
        if (!lit(inside * cx, inside * sy)) {
            // The edge on this side is the terminator: bisect between the lit
            // centre and the unlit limb.
            double lo = 0.0, hi = inside;
            if (!lit(0.0, 0.0)) {
                r = 0.0;
            } else {
                for (int k = 0; k < 40; ++k) {
                    double mid = 0.5 * (lo + hi);
                    if (lit(mid * cx, mid * sy)) lo = mid;
                    else hi = mid;
                }
                r = 0.5 * (lo + hi);
            }
            // Only worth telling apart where it differs visibly from the limb
            // (phase angles above about six degrees).
            terminator_[i] = (limb - r) > 0.005 * limb ? 1 : 0;
        }
        if (rings) {
            double a = shape.ringOuter, b = shape.ringOuter * sinDe;
            double ring = 1.0 / std::sqrt(cx * cx / (a * a) + sy * sy / (b * b));
            if (ring > r) {
                r = ring;
                terminator_[i] = 0;
            }
        }
        table_[i] = r;
    }
}

double OutlineModel::radius(double psi) const {
    const int n = static_cast<int>(table_.size());
    double f = psi / (2.0 * kPi);
    f -= std::floor(f);
    double x = f * n;
    int i0 = static_cast<int>(x) % n;
    int i1 = (i0 + 1) % n;
    double t = x - std::floor(x);
    return table_[i0] * (1 - t) + table_[i1] * t;
}

bool OutlineModel::isTerminator(double psi) const {
    const int n = static_cast<int>(table_.size());
    double f = psi / (2.0 * kPi);
    f -= std::floor(f);
    return terminator_[static_cast<int>(f * n + 0.5) % n] != 0;
}

// ------------------------------------------------------------ limb points --

namespace {

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    size_t k = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

float bilinear(const Image& img, double x, double y) {
    float out;
    sample(img, std::min(std::max(x, 0.0), img.width() - 1.0), std::min(std::max(y, 0.0), img.height() - 1.0),
           Interpolation::Bilinear, &out);
    return out;
}

}  // namespace

LimbScan findLimbPoints(const Image& lum) {
    LimbScan scan;
    std::vector<std::pair<double, double>>& points = scan.points;
    std::string& message = scan.message;
    double& centroidX = scan.centroidX;
    double& centroidY = scan.centroidY;
    double& roughRadius = scan.roughRadius;
    const int w = lum.width(), h = lum.height();
    if (w < 16 || h < 16) {
        message = "image is too small";
        return scan;
    }
    Image sm = gaussianBlur(lum, 1.5);

    // Background from the image border, object level from the bright end.
    std::vector<double> border;
    int bw = std::max(2, std::min(w, h) / 40);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (x < bw || y < bw || x >= w - bw || y >= h - bw) border.push_back(sm.at(x, y));
    const double bg = median(border);
    const double peak = percentile(sm, 0, 99.5, 3);
    if (!(peak - bg > 1e-4)) {
        message = "no object found above the background";
        return scan;
    }
    const double thr = bg + 0.2 * (peak - bg);

    // Largest connected region above the threshold.
    std::vector<int> label(static_cast<size_t>(w) * h, 0);
    int best = 0, current = 0;
    size_t bestArea = 0;
    std::vector<int> stack;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (label[idx] != 0 || sm.at(x, y) <= thr) continue;
            ++current;
            size_t area = 0;
            stack.clear();
            stack.push_back(static_cast<int>(idx));
            label[idx] = current;
            while (!stack.empty()) {
                int p = stack.back();
                stack.pop_back();
                ++area;
                int px = p % w, py = p / w;
                static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    int nx = px + dx[k], ny = py + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    size_t ni = static_cast<size_t>(ny) * w + nx;
                    if (label[ni] != 0 || sm.at(nx, ny) <= thr) continue;
                    label[ni] = current;
                    stack.push_back(static_cast<int>(ni));
                }
            }
            if (area > bestArea) {
                bestArea = area;
                best = current;
            }
        }
    if (bestArea < 200) {
        message = "the object is too small to measure";
        return scan;
    }

    double sx = 0, sy = 0;
    size_t touching = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (label[static_cast<size_t>(y) * w + x] == best) {
                sx += x;
                sy += y;
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) ++touching;
            }
    centroidX = sx / bestArea;
    centroidY = sy / bestArea;
    roughRadius = std::sqrt(bestArea / kPi);

    // Principal axes of the silhouette (second moments, y pointing up).
    {
        double cxx = 0, cyy = 0, cxy = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (label[static_cast<size_t>(y) * w + x] == best) {
                    double dx = x - centroidX, dy = -(y - centroidY);
                    cxx += dx * dx;
                    cyy += dy * dy;
                    cxy += dx * dy;
                }
        double theta = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);  // long axis, from +x counter-clockwise
        double common = 0.5 * (cxx + cyy), diff = std::sqrt(0.25 * (cxx - cyy) * (cxx - cyy) + cxy * cxy);
        double l1 = common + diff, l2 = common - diff;
        scan.elongation = l2 > 0 ? std::sqrt(l1 / l2) : 1.0;
        // A pole lies 90 deg from the long axis; measured from "up" that is the same number.
        scan.axisAngle = wrap360(theta * kRadToDeg);
        if (scan.axisAngle >= 180.0) scan.axisAngle -= 180.0;
    }
    if (touching > 0) message = "the planet touches the edge of the image; the measurement may be off";

    // Walk inwards along rays until the object is reached, then look for the
    // steepest brightness drop near that point.
    const int rays = 360;
    const double window = std::max(4.0, 0.06 * roughRadius);
    const double maxR = std::sqrt(static_cast<double>(w) * w + static_cast<double>(h) * h);
    for (int i = 0; i < rays; ++i) {
        double a = 2.0 * kPi * i / rays;
        double ca = std::cos(a), sa = std::sin(a);
        // Largest radius still inside the image along this ray.
        double rEdge = maxR;
        if (ca > 1e-9) rEdge = std::min(rEdge, (w - 1 - centroidX) / ca);
        if (ca < -1e-9) rEdge = std::min(rEdge, (0 - centroidX) / ca);
        if (sa > 1e-9) rEdge = std::min(rEdge, (h - 1 - centroidY) / sa);
        if (sa < -1e-9) rEdge = std::min(rEdge, (0 - centroidY) / sa);
        double r1 = -1;
        for (double r = rEdge; r > 0; r -= 0.5) {
            int px = static_cast<int>(std::lround(centroidX + r * ca));
            int py = static_cast<int>(std::lround(centroidY + r * sa));
            if (px < 0 || py < 0 || px >= w || py >= h) continue;
            if (label[static_cast<size_t>(py) * w + px] == best) {
                r1 = r;
                break;
            }
        }
        if (r1 < 0) continue;
        if (r1 > rEdge - 1.5) continue;  // the object runs off the image on this side
        const double step = 0.25;
        double bestG = 0, bestR = -1;
        double g0 = 0, g1 = 0, g2 = 0;  // gradients around the best sample for the parabola
        double prevG = 0;
        bool havePrev = false;
        double lo = std::max(1.0, r1 - window), hi = std::min(rEdge - 1.0, r1 + window);
        for (double r = lo; r <= hi; r += step) {
            double inner = bilinear(sm, centroidX + (r - 1.0) * ca, centroidY + (r - 1.0) * sa);
            double outer = bilinear(sm, centroidX + (r + 1.0) * ca, centroidY + (r + 1.0) * sa);
            double g = inner - outer;  // positive where brightness falls outwards
            if (g > bestG) {
                bestG = g;
                bestR = r;
                g1 = g;
                g0 = havePrev ? prevG : g;
                double inner2 = bilinear(sm, centroidX + (r + step - 1.0) * ca, centroidY + (r + step - 1.0) * sa);
                double outer2 = bilinear(sm, centroidX + (r + step + 1.0) * ca, centroidY + (r + step + 1.0) * sa);
                g2 = inner2 - outer2;
            }
            prevG = g;
            havePrev = true;
        }
        if (bestR < 0 || bestG < 0.05 * (peak - bg)) continue;
        double denom = g0 - 2 * g1 + g2;
        double shift = std::fabs(denom) > 1e-12 ? 0.5 * (g0 - g2) / denom : 0.0;
        shift = std::min(1.0, std::max(-1.0, shift));
        double r = bestR + shift * step;
        points.emplace_back(centroidX + r * ca, centroidY + r * sa);
    }
    if (points.size() < 40) message = "too few limb points found";
    return scan;
}

// ------------------------------------------------------------- fitting ----

namespace {

// Solve the small symmetric system A x = b in place (Gaussian elimination with pivoting).
bool solveSmall(std::vector<std::vector<double>> a, std::vector<double> b, std::vector<double>& x) {
    const int n = static_cast<int>(b.size());
    for (int i = 0; i < n; ++i) {
        int piv = i;
        for (int r = i + 1; r < n; ++r)
            if (std::fabs(a[r][i]) > std::fabs(a[piv][i])) piv = r;
        if (std::fabs(a[piv][i]) < 1e-14) return false;
        std::swap(a[i], a[piv]);
        std::swap(b[i], b[piv]);
        for (int r = i + 1; r < n; ++r) {
            double f = a[r][i] / a[i][i];
            for (int c = i; c < n; ++c) a[r][c] -= f * a[i][c];
            b[r] -= f * b[i];
        }
    }
    x.assign(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int c = i + 1; c < n; ++c) s -= a[i][c] * x[c];
        x[i] = s / a[i][i];
    }
    return true;
}

struct FitSetup {
    const std::vector<std::pair<double, double>>* points;
    const OutlineModel* model;
    bool mirror;
    bool fitRadius;
    bool fitAngle;
};

// Signed distance (pixels) of each limb point from the model outline, and
// optionally how much each point should count.
void residuals(const FitSetup& s, const ImageGeometry& g, std::vector<double>& out, std::vector<double>* prior = nullptr) {
    out.resize(s.points->size());
    if (prior) prior->resize(s.points->size());
    for (size_t k = 0; k < s.points->size(); ++k) {
        double x, y;
        g.pixelToView((*s.points)[k].first, (*s.points)[k].second, x, y);
        double rho = std::sqrt(x * x + y * y);
        double psi = std::atan2(y, x);
        out[k] = (rho - s.model->radius(psi)) * g.radius;
        // The terminator fades out gradually, so its measured position is
        // biased inwards; let the sharp limb decide and the terminator only assist.
        if (prior) (*prior)[k] = s.model->isTerminator(psi) ? 0.1 : 1.0;
    }
}

// Robust Levenberg-Marquardt fit of centre, radius and (optionally) north angle.
double fitOutline(const FitSetup& s, ImageGeometry& g, int* inliers) {
    std::vector<int> idx = {0, 1};  // cx, cy
    if (s.fitRadius) idx.push_back(2);
    if (s.fitAngle) idx.push_back(3);
    const int n = static_cast<int>(idx.size());
    auto getP = [](const ImageGeometry& gg, int i) { return i == 0 ? gg.cx : i == 1 ? gg.cy : i == 2 ? gg.radius : gg.northAngle; };
    auto setP = [](ImageGeometry& gg, int i, double v) {
        if (i == 0) gg.cx = v;
        else if (i == 1) gg.cy = v;
        else if (i == 2) gg.radius = v;
        else gg.northAngle = v;
    };
    const double steps[4] = {0.05, 0.05, 0.05, 0.05};

    std::vector<double> res, res2, wgt, prior, prior2;
    auto robustCost = [&](const std::vector<double>& r, const std::vector<double>& pri, double scale,
                          std::vector<double>* weights) {
        double cost = 0;
        if (weights) weights->resize(r.size());
        const double k = 2.0 * scale;
        for (size_t i = 0; i < r.size(); ++i) {
            double a = std::fabs(r[i]);
            double wv = a <= k ? 1.0 : k / a;  // Huber
            if (a > 6.0 * scale) wv = 0.0;     // far outliers (moons, shadows) are ignored
            wv *= pri[i];
            if (weights) (*weights)[i] = wv;
            cost += wv * r[i] * r[i];
        }
        return cost;
    };

    double lambda = 1e-3;
    double scale = 1.0;
    for (int iter = 0; iter < 40; ++iter) {
        residuals(s, g, res, &prior);
        std::vector<double> absr;
        for (size_t i = 0; i < res.size(); ++i)
            if (prior[i] >= 1.0) absr.push_back(std::fabs(res[i]));
        if (absr.size() < 20) {
            absr.clear();
            for (double r : res) absr.push_back(std::fabs(r));
        }
        scale = std::max(0.15, 1.4826 * median(absr));
        double cost = robustCost(res, prior, scale, &wgt);

        // Numeric Jacobian.
        std::vector<std::vector<double>> jac(n, std::vector<double>(res.size()));
        for (int j = 0; j < n; ++j) {
            ImageGeometry gp = g;
            setP(gp, idx[j], getP(g, idx[j]) + steps[idx[j]]);
            residuals(s, gp, res2);
            for (size_t i = 0; i < res.size(); ++i) jac[j][i] = (res2[i] - res[i]) / steps[idx[j]];
        }
        std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
        std::vector<double> b(n, 0.0);
        for (int p = 0; p < n; ++p) {
            for (int q = p; q < n; ++q) {
                double sum = 0;
                for (size_t i = 0; i < res.size(); ++i) sum += wgt[i] * jac[p][i] * jac[q][i];
                a[p][q] = a[q][p] = sum;
            }
            double sum = 0;
            for (size_t i = 0; i < res.size(); ++i) sum += wgt[i] * jac[p][i] * res[i];
            b[p] = -sum;
        }
        bool improved = false;
        for (int attempt = 0; attempt < 8 && !improved; ++attempt) {
            std::vector<std::vector<double>> al = a;
            for (int p = 0; p < n; ++p) al[p][p] *= 1.0 + lambda;
            std::vector<double> dx;
            if (!solveSmall(al, b, dx)) {
                lambda *= 10;
                continue;
            }
            ImageGeometry gt = g;
            for (int j = 0; j < n; ++j) setP(gt, idx[j], getP(g, idx[j]) + dx[j]);
            if (gt.radius < 2.0) {
                lambda *= 10;
                continue;
            }
            residuals(s, gt, res2, &prior2);
            double c2 = robustCost(res2, prior2, scale, nullptr);
            if (c2 < cost) {
                double change = 0;
                for (int j = 0; j < n; ++j) change = std::max(change, std::fabs(dx[j]));
                g = gt;
                lambda = std::max(1e-7, lambda * 0.3);
                improved = true;
                if (change < 1e-4) iter = 1000;
            } else {
                lambda *= 10;
            }
        }
        if (!improved) break;
    }

    residuals(s, g, res, &prior);
    double sum = 0;
    int cnt = 0;
    for (size_t i = 0; i < res.size(); ++i)
        if (std::fabs(res[i]) <= 6.0 * scale && prior[i] >= 1.0) {
            sum += res[i] * res[i];
            ++cnt;
        }
    if (inliers) *inliers = cnt;
    return cnt > 0 ? std::sqrt(sum / cnt) : 0.0;
}

// How strongly the disc is striped parallel to the assumed equator: the
// variance of the row-averaged brightness after removing its smooth trend.
double bandingScore(const Image& sm, const ImageGeometry& g, double polarRadius) {
    const double yMax = 0.72 * polarRadius, xMax = 0.55;
    const double step = 1.0 / std::max(8.0, g.radius);
    std::vector<double> rows;
    for (double y = -yMax; y <= yMax; y += step) {
        double acc = 0;
        int n = 0;
        for (double x = -xMax; x <= xMax; x += 2 * step) {
            double u, v;
            g.viewToPixel(x, y, u, v);
            if (u < 0 || v < 0 || u > sm.width() - 1 || v > sm.height() - 1) continue;
            acc += bilinear(sm, u, v);
            ++n;
        }
        rows.push_back(n > 0 ? acc / n : 0.0);
    }
    const int n = static_cast<int>(rows.size());
    if (n < 8) return 0.0;
    // Smooth trend: wide box filter applied twice.
    int half = std::max(2, n / 6);
    std::vector<double> trend = rows;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<double> t2(n);
        for (int i = 0; i < n; ++i) {
            double acc = 0;
            int cnt = 0;
            for (int k = -half; k <= half; ++k) {
                int j = std::min(n - 1, std::max(0, i + k));
                acc += trend[j];
                ++cnt;
            }
            t2[i] = acc / cnt;
        }
        trend.swap(t2);
    }
    double var = 0;
    for (int i = half / 2; i < n - half / 2; ++i) {
        double d = rows[i] - trend[i];
        var += d * d;
    }
    return var / n;
}

}  // namespace

MeasureResult measureDisc(const Image& image, const PhysicalEphemeris& eph, const MeasureOptions& opt) {
    MeasureResult result;
    if (image.empty()) {
        result.message = "empty image";
        return result;
    }
    const PlanetInfo& info = planetInfo(eph.planet);
    const BodyShape shape = bodyShape(eph.planet);
    Image lum = image.luminance();

    LimbScan scan = findLimbPoints(lum);
    const double cx = scan.centroidX, cy = scan.centroidY;
    std::string msg = scan.message;
    const std::vector<std::pair<double, double>>& pts = scan.points;
    if (pts.size() < 40) {
        result.message = msg.empty() ? "could not find the limb" : msg;
        return result;
    }

    const bool ringsOpen = info.hasRings && std::fabs(eph.de) >= 3.0;
    // Nearly edge-on rings are a thin line whose ends are found unreliably;
    // the globe alone is used then and the ring ends drop out as outliers.
    OutlineModel model(shape, eph.de, eph.sunDirView, ringsOpen);

    ImageGeometry g;
    g.cx = cx;
    g.cy = cy;
    g.northAngle = opt.northAngleHint;
    g.mirror = opt.mirror;
    const bool fixedRadius = opt.pixelScaleArcsec > 0;
    if (fixedRadius) {
        g.radius = 0.5 * eph.diameterArcsec / opt.pixelScaleArcsec;
    } else {
        // Scale so that the outline matches the limb points on average.
        std::vector<double> ratios;
        ImageGeometry unit = g;
        unit.radius = 1.0;
        for (const auto& p : pts) {
            double x, y;
            unit.pixelToView(p.first, p.second, x, y);
            double m = model.radius(std::atan2(y, x));
            if (m > 1e-6) ratios.push_back(std::sqrt(x * x + y * y) / m);
        }
        g.radius = std::max(4.0, median(ratios));
    }

    // The radius is always fitted, even when it is known: the limb is found
    // slightly inside the true edge, and with the radius held fixed that
    // offset would push the centre around. A known radius is put back at the end.
    FitSetup setup{&pts, &model, opt.mirror, true, false};
    int inliers = 0;
    double rms = fitOutline(setup, g, &inliers);

    if (opt.fitOrientation) {
        if (info.hasRings && scan.elongation > 1.05) {
            // The rings (or, when they are edge-on, the flattened globe) make
            // the silhouette long in the direction of the equator. Of the two
            // poles take the one nearer to the hint.
            double a = scan.axisAngle;
            if (std::fabs(wrap180(a - opt.northAngleHint)) > 90.0) a += 180.0;
            g.northAngle = a;
            rms = fitOutline(setup, g, &inliers);
            if (ringsOpen) {
                // With open rings the outline itself pins the angle down further.
                ImageGeometry gt = g;
                FitSetup s2 = setup;
                s2.fitAngle = true;
                int in2 = 0;
                double r2 = fitOutline(s2, gt, &in2);
                if (std::fabs(wrap180(gt.northAngle - a)) < 5.0 && in2 >= inliers * 9 / 10) {
                    g = gt;
                    rms = r2;
                    inliers = in2;
                }
            }
            result.orientationFitted = true;
            result.orientationContrast = scan.elongation;
        } else if (eph.planet == Planet::Jupiter) {
            Image sm = gaussianBlur(lum, 1.5);
            const double cp = projectedPolarRadius(shape, eph.de);
            for (int round = 0; round < 2; ++round) {
                std::vector<double> scores, angles;
                double bestScore = -1, bestAngle = g.northAngle;
                const double centre = round == 0 ? opt.northAngleHint : g.northAngle;
                const double range = round == 0 ? opt.orientationRange : 3.0;
                const double step = round == 0 ? 1.0 : 0.1;
                for (double a = centre - range; a <= centre + range + 1e-9; a += step) {
                    ImageGeometry gt = g;
                    gt.northAngle = a;
                    double sc = bandingScore(sm, gt, cp);
                    scores.push_back(sc);
                    angles.push_back(a);
                    if (sc > bestScore) {
                        bestScore = sc;
                        bestAngle = a;
                    }
                }
                // Parabolic refinement around the best sample.
                size_t bi = std::max_element(scores.begin(), scores.end()) - scores.begin();
                if (bi > 0 && bi + 1 < scores.size()) {
                    double d = scores[bi - 1] - 2 * scores[bi] + scores[bi + 1];
                    if (std::fabs(d) > 1e-18) bestAngle += step * 0.5 * (scores[bi - 1] - scores[bi + 1]) / d;
                }
                if (round == 0) {
                    double med = median(scores);
                    result.orientationContrast = med > 0 ? bestScore / med : 0.0;
                }
                g.northAngle = bestAngle;
                // The outline is not round, so refit the centre for the new angle.
                rms = fitOutline(setup, g, &inliers);
            }
            result.orientationFitted = result.orientationContrast >= 2.0;
            if (!result.orientationFitted) {
                g.northAngle = opt.northAngleHint;
                rms = fitOutline(setup, g, &inliers);
                if (msg.empty()) msg = "no clear belts found; the north angle was left at the hint";
            }
        }
    }

    if (fixedRadius) g.radius = 0.5 * eph.diameterArcsec / opt.pixelScaleArcsec;
    g.northAngle = wrap180(g.northAngle);
    result.ok = true;
    result.geometry = g;
    result.residualRms = rms;
    result.edgePoints = inliers;
    result.message = msg;
    return result;
}

RegulariseReport regulariseMeasurements(std::vector<ImageGeometry>& g, const std::vector<double>& jd,
                                        AngleMode angleMode, bool commonRadius) {
    RegulariseReport rep;
    const size_t n = g.size();
    if (n == 0 || jd.size() != n) return rep;
    auto med = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        size_t k = v.size();
        return k % 2 ? v[k / 2] : 0.5 * (v[k / 2 - 1] + v[k / 2]);
    };
    if (commonRadius && n > 1) {
        std::vector<double> r;
        for (const ImageGeometry& x : g) r.push_back(x.radius);
        rep.radius = med(r);
        rep.radiusChanged = true;
        for (ImageGeometry& x : g) x.radius = rep.radius;
    }
    if (angleMode != AngleMode::Individual && n > 1) {
        size_t first = 0;
        for (size_t i = 1; i < n; ++i)
            if (jd[i] < jd[first]) first = i;
        // Work with angles relative to the first image so that the +-180 wrap does not matter.
        const double a0 = g[first].northAngle;
        std::vector<double> rel(n), minutes(n);
        for (size_t i = 0; i < n; ++i) {
            rel[i] = wrap180(g[i].northAngle - a0);
            minutes[i] = (jd[i] - jd[first]) * 1440.0;
        }
        double slope = 0.0;
        if (angleMode == AngleMode::Trend && n >= 4) {
            // Theil-Sen: median of the slopes between all pairs of images.
            std::vector<double> slopes;
            for (size_t i = 0; i < n; ++i)
                for (size_t j = i + 1; j < n; ++j)
                    if (std::fabs(minutes[j] - minutes[i]) > 1e-6)
                        slopes.push_back((rel[j] - rel[i]) / (minutes[j] - minutes[i]));
            if (!slopes.empty()) slope = med(slopes);
        }
        std::vector<double> offsets(n);
        for (size_t i = 0; i < n; ++i) offsets[i] = rel[i] - slope * minutes[i];
        const double intercept = med(offsets);
        for (size_t i = 0; i < n; ++i) g[i].northAngle = wrap180(a0 + intercept + slope * minutes[i]);
        rep.angleChanged = true;
        rep.angleAtFirst = wrap180(a0 + intercept);
        rep.driftDegPerMinute = slope;
    }
    return rep;
}

}  // namespace fixmyplanet
