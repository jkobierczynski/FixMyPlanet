// SPDX-License-Identifier: GPL-3.0-or-later
#include "derotate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

#include "parallel.h"
#include "timeutil.h"

namespace fixmyplanet {

namespace {

inline double smoothstep(double lo, double hi, double x) {
    if (hi <= lo) return x >= hi ? 1.0 : 0.0;
    double t = (x - lo) / (hi - lo);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3 - 2 * t);
}

std::string upper(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    s = s.substr(a);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

Vec3 direction(double lonEastDeg, double latDeg) {
    double lon = lonEastDeg * kDegToRad, lat = latDeg * kDegToRad;
    return {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
}

}  // namespace

// ----------------------------------------------------------- RotationModel --

RotationModel::RotationModel(Planet planet, int system, double customRate, double referenceJd)
    : planet_(planet), system_(system), customRate_(customRate), referenceJd_(referenceJd) {
    const PlanetInfo& info = planetInfo(planet);
    if (system_ < 0 || system_ >= static_cast<int>(info.systems.size())) system_ = info.defaultSystem;
    if (std::isfinite(customRate_)) system_ = info.iauSystem;
}

double RotationModel::offset(double jdUtc) const {
    if (!std::isfinite(customRate_)) return 0.0;
    const PlanetInfo& info = planetInfo(planet_);
    const double iauRate = info.systems[info.iauSystem].rate;
    const double dW = (customRate_ - iauRate) * (jdUtc - referenceJd_);
    return iauRate > 0 ? dW : -dW;
}

double RotationModel::centralMeridian(double jdUtc) const {
    PhysicalEphemeris e = computeEphemeris(planet_, jdUtc);
    return wrap360(e.cm[system_] + offset(jdUtc));
}

Pose RotationModel::at(double jdUtc) const {
    PhysicalEphemeris e = computeEphemeris(planet_, jdUtc);
    const double off = offset(jdUtc);
    Pose p;
    p.orientation = Orientation::fromCentralMeridian(e.cm[system_] + off, e.de, e.westLongitude);
    const double sunLon = e.subSolarLon[system_] + off;
    p.sunBody = direction(e.westLongitude ? -sunLon : sunLon, e.ds);
    return p;
}

// ------------------------------------------------------------ derotateFrame --

void derotateFrame(const Frame& frame, const Pose& framePose, const Pose& refPose, const ImageGeometry& outGeometry,
                   int outWidth, int outHeight, const BodyShape& shape, const DerotateOptions& options, Image& warped,
                   Image& weight, Image* plain, Image* plainValid) {
    const int c = frame.image.channels();
    warped = Image(outWidth, outHeight, c);
    weight = Image(outWidth, outHeight, 1);
    if (plain) *plain = Image(outWidth, outHeight, c);
    if (plainValid) *plainValid = Image(outWidth, outHeight, 1);

    const Orientation& ro = refPose.orientation;
    const Orientation& fo = framePose.orientation;
    const double k = options.limbDarkening;
    const double muLow = options.limbMuLow, muHigh = std::max(options.limbMuHigh, options.limbMuLow + 1e-3);

    parallelRows(outHeight, options.threads, [&](int y0, int y1) {
        std::vector<float> buf(c);
        for (int v = y0; v < y1; ++v)
            for (int u = 0; u < outWidth; ++u) {
                double x, y;
                outGeometry.pixelToView(u, v, x, y);

                if (plain) {
                    double pu, pv;
                    frame.geometry.viewToPixel(x, y, pu, pv);
                    bool inside = sample(frame.image, pu, pv, options.interpolation, buf.data());
                    for (int ch = 0; ch < c; ++ch) plain->at(u, v, ch) = buf[ch];
                    if (plainValid) plainValid->at(u, v) = inside ? 1.0f : 0.0f;
                }

                Vec3 body;
                double muRef;
                if (!ro.surfacePoint(x, y, shape, body, muRef)) continue;
                double zRing;
                if (shape.hasRings) {
                    // Rings in front of the globe do not take part in the rotation.
                    if (ro.ringPoint(x, y, shape, zRing) && zRing > ro.bodyToView(body).z) continue;
                }
                const Vec3 normal = surfaceNormal(body, shape);
                const double muFrame = dot(normal, fo.observerDir());
                const double sunFrame = dot(normal, framePose.sunBody);
                const double m = std::min(muFrame, sunFrame);
                if (m <= muLow) continue;
                const Vec3 vf = fo.bodyToView(body);
                if (shape.hasRings && fo.ringPoint(vf.x, vf.y, shape, zRing) && zRing > vf.z) continue;
                double su, sv;
                frame.geometry.viewToPixel(vf.x, vf.y, su, sv);
                if (!sample(frame.image, su, sv, options.interpolation, buf.data())) continue;

                double gain = 1.0;
                if (k > 0) {
                    const double sunRef = std::max(0.0, dot(normal, refPose.sunBody));
                    const double a = (1.0 - k) + k * sunRef, b = (1.0 - k) + k * std::max(0.0, sunFrame);
                    gain = b > 1e-6 ? a / b : 1.0;
                    gain = std::min(2.0, std::max(0.5, gain));
                }
                for (int ch = 0; ch < c; ++ch) warped.at(u, v, ch) = static_cast<float>(buf[ch] * gain);
                weight.at(u, v) = static_cast<float>(frame.weight * smoothstep(muLow, muHigh, m) * m);
            }
    });
}

// ---------------------------------------------------------------- alignment --

namespace {

// High-pass filter two warped images inside the area where both are valid, so
// that they can be compared: the shading of the disc is divided out, leaving
// the surface detail. `core` is 1 well inside the common area.
bool prepareForComparison(const Image& a, const Image& wa, const Image& target, const Image& wt, double hpSigma,
                          double lpSigma, Image& ha, Image& ht, Image& core) {
    const int w = a.width(), h = a.height();
    const size_t n = static_cast<size_t>(w) * h;
    float maxW = 0;
    for (size_t i = 0; i < n; ++i) maxW = std::max(maxW, std::min(wa.data()[i], wt.data()[i]));
    if (maxW <= 0) return false;

    Image mask(w, h, 1);
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        // Keep to the inner disc: near the limb the shading is steep and
        // does not rotate with the features, which would bias the result.
        bool in = std::min(wa.data()[i], wt.data()[i]) > 0.45f * maxW;
        mask.data()[i] = in ? 1.0f : 0.0f;
        count += in;
    }
    if (count < 400) return false;

    Image maskBlur = gaussianBlur(mask, hpSigma);
    auto highPass = [&](const Image& img) {
        Image num(w, h, 1);
        for (size_t i = 0; i < n; ++i) num.data()[i] = img.data()[i] * mask.data()[i];
        Image nb = gaussianBlur(num, hpSigma);
        Image out(w, h, 1);
        for (size_t i = 0; i < n; ++i) {
            float d = maskBlur.data()[i];
            // Shading is multiplicative, so divide by the local mean instead of subtracting it.
            const float localMean = d > 1e-4f ? nb.data()[i] / d : 0.0f;
            out.data()[i] = mask.data()[i] > 0 && localMean > 1e-6f ? img.data()[i] / localMean - 1.0f : 0.0f;
        }
        return gaussianBlur(out, lpSigma);
    };
    ha = highPass(a);
    ht = highPass(target);
    // Stay away from the border of the valid area, where the filters see zeros.
    core = gaussianBlur(mask, std::max(2.0, 2.0 * lpSigma));
    return true;
}

// Estimate the translation t for which a(x + t) matches target(x), using one
// Gauss-Newton step on high-pass filtered images inside the common valid area.
bool estimateShift(const Image& a, const Image& wa, const Image& target, const Image& wt, double hpSigma,
                   double lpSigma, double& tx, double& ty) {
    Image ha, ht, core;
    if (!prepareForComparison(a, wa, target, wt, hpSigma, lpSigma, ha, ht, core)) return false;
    const int w = a.width(), h = a.height();
    double sxx = 0, sxy = 0, syy = 0, bx = 0, by = 0;
    size_t used = 0;
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
            if (core.at(x, y) < 0.97f) continue;
            double gx = 0.5 * (ha.at(x + 1, y) - ha.at(x - 1, y));
            double gy = 0.5 * (ha.at(x, y + 1) - ha.at(x, y - 1));
            double r = ht.at(x, y) - ha.at(x, y);
            sxx += gx * gx;
            sxy += gx * gy;
            syy += gy * gy;
            bx += gx * r;
            by += gy * r;
            ++used;
        }
    if (used < 200) return false;
    double det = sxx * syy - sxy * sxy;
    if (!(det > 1e-12 * (sxx + syy) * (sxx + syy)) || det <= 0) return false;
    tx = (syy * bx - sxy * by) / det;
    ty = (sxx * by - sxy * bx) / det;
    return std::isfinite(tx) && std::isfinite(ty);
}

// Correlation (-1..1) of the surface detail of two warped images; false if
// they do not overlap enough. Belts run along the direction of rotation and
// look the same wherever the planet has turned to, so the average along each
// line parallel to the equator is taken out first: what remains is the detail
// that actually moves.
bool detailCorrelation(const Image& a, const Image& wa, const Image& b, const Image& wb, const ImageGeometry& g,
                       double hpSigma, double& corr) {
    Image ha, hb, core;
    if (!prepareForComparison(a, wa, b, wb, hpSigma, 1.5, ha, hb, core)) return false;
    const int w = a.width(), h = a.height();
    const int bins = 2 * static_cast<int>(g.radius) + 5;
    std::vector<double> sumA(bins, 0.0), sumB(bins, 0.0);
    std::vector<int> cnt(bins, 0);
    std::vector<int> binOf(static_cast<size_t>(w) * h, -1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (core.at(x, y) < 0.97f) continue;
            double vx, vy;
            g.pixelToView(x, y, vx, vy);
            int bin = static_cast<int>(std::lround(vy * g.radius)) + bins / 2;
            if (bin < 0 || bin >= bins) continue;
            binOf[static_cast<size_t>(y) * w + x] = bin;
            sumA[bin] += ha.at(x, y);
            sumB[bin] += hb.at(x, y);
            ++cnt[bin];
        }
    double saa = 0, sbb = 0, sab = 0;
    size_t used = 0;
    for (size_t i = 0; i < binOf.size(); ++i) {
        int bin = binOf[i];
        if (bin < 0 || cnt[bin] < 8) continue;
        double x = ha.data()[i] - sumA[bin] / cnt[bin], y = hb.data()[i] - sumB[bin] / cnt[bin];
        saa += x * x;
        sbb += y * y;
        sab += x * y;
        ++used;
    }
    if (used < 200 || saa <= 0 || sbb <= 0) return false;
    corr = sab / std::sqrt(saa * sbb);
    return true;
}

// Mean brightness of the inner part of the disc, using the frame's own geometry.
double innerDiscMean(const Image& lum, const ImageGeometry& g, double polarRadius) {
    double acc = 0;
    size_t n = 0;
    const double step = 2.0 / std::max(8.0, g.radius);
    for (double y = -0.6 * polarRadius; y <= 0.6 * polarRadius; y += step)
        for (double x = -0.6; x <= 0.6; x += step) {
            if (x * x + (y / polarRadius) * (y / polarRadius) > 0.36) continue;
            double u, v;
            g.viewToPixel(x, y, u, v);
            float val;
            if (sample(lum, u, v, Interpolation::Bilinear, &val)) {
                acc += val;
                ++n;
            }
        }
    return n > 0 ? acc / n : 0.0;
}

// Estimate the limb darkening coefficient k of the model I = A * ((1 - k) + k * mu)
// from one image, where mu is the cosine of the solar incidence angle. Only a
// band along the equator is used, so that belts (which follow latitude) do
// not masquerade as limb darkening.
double fitLimbDarkening(const Image& lum, const ImageGeometry& g, const Pose& pose, const BodyShape& shape) {
    const int bins = 14;
    std::vector<std::vector<float>> values(bins);
    const double step = 1.0 / std::max(8.0, g.radius);
    for (double y = -0.25; y <= 0.25; y += step)
        for (double x = -1.0; x <= 1.0; x += step) {
            Vec3 body;
            double mu;
            if (!pose.orientation.surfacePoint(x, y, shape, body, mu)) continue;
            double sun = dot(surfaceNormal(body, shape), pose.sunBody);
            double m = std::min(mu, sun);
            if (m < 0.3 || m > 1.0) continue;
            double u, v;
            g.viewToPixel(x, y, u, v);
            float val;
            if (!sample(lum, u, v, Interpolation::Bilinear, &val)) continue;
            int b = std::min(bins - 1, static_cast<int>((m - 0.3) / 0.7 * bins));
            values[b].push_back(val);
        }
    // Median brightness per bin, then a straight line through the bins.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int b = 0; b < bins; ++b) {
        if (values[b].size() < 20) continue;
        std::nth_element(values[b].begin(), values[b].begin() + values[b].size() / 2, values[b].end());
        double med = values[b][values[b].size() / 2];
        double m = 0.3 + 0.7 * (b + 0.5) / bins;
        sx += m;
        sy += med;
        sxx += m * m;
        sxy += m * med;
        ++n;
    }
    if (n < 5) return 0.0;
    double den = n * sxx - sx * sx;
    if (std::fabs(den) < 1e-12) return 0.0;
    double slope = (n * sxy - sx * sy) / den;
    double intercept = (sy - slope * sx) / n;
    double atOne = intercept + slope;
    if (!(atOne > 1e-6)) return 0.0;
    return std::min(1.0, std::max(0.0, slope / atOne));
}

}  // namespace

// ----------------------------------------------------------- rotation sense --

int checkRotationSense(const std::vector<Frame>& frames, const DerotateOptions& options, double* correlationGiven,
                       double* correlationFlipped) {
    if (correlationGiven) *correlationGiven = 0;
    if (correlationFlipped) *correlationFlipped = 0;
    // The two frames of one filter that are furthest apart in time.
    int ia = -1, ib = -1;
    double span = 0;
    for (size_t i = 0; i < frames.size(); ++i)
        for (size_t j = i + 1; j < frames.size(); ++j) {
            if (upper(frames[i].channel) != upper(frames[j].channel)) continue;
            if (frames[i].image.empty() || frames[j].image.empty()) continue;
            if (!(frames[i].jdUtc > 0 && frames[j].jdUtc > 0 && frames[i].geometry.radius > 2 && frames[j].geometry.radius > 2)) continue;
            double d = std::fabs(frames[i].jdUtc - frames[j].jdUtc);
            if (d > span) {
                span = d;
                ia = static_cast<int>(i);
                ib = static_cast<int>(j);
            }
        }
    if (ia < 0) return 0;

    const double mid = 0.5 * (frames[ia].jdUtc + frames[ib].jdUtc);
    RotationModel model(options.planet, options.system, options.customRate, mid);
    const Pose pa = model.at(frames[ia].jdUtc), pb = model.at(frames[ib].jdUtc), pm = model.at(mid);
    // The features must have moved by a few pixels for the test to mean anything.
    const double rotation = std::fabs(wrap180(pa.orientation.lonEast() - pb.orientation.lonEast())) * kDegToRad;
    if (rotation * frames[ia].geometry.radius < 3.0 || rotation > 1.2) return 0;

    const BodyShape shape = bodyShape(options.planet);
    DerotateOptions o = options;
    o.limbDarkening = 0.0;
    o.interpolation = Interpolation::Bilinear;
    Frame fa = frames[ia], fb = frames[ib];
    fa.image = fa.image.luminance();
    fb.image = fb.image.luminance();
    fa.weight = fb.weight = 1.0;
    const int w = fa.image.width(), h = fa.image.height();
    const double hpSigma = std::max(4.0, fa.geometry.radius / 8.0);

    double corr[2] = {0, 0};
    bool have[2] = {false, false};
    for (int flip = 0; flip < 2; ++flip) {
        Frame a = fa, b = fb;
        if (flip) {
            a.geometry.northAngle = wrap180(a.geometry.northAngle + 180.0);
            b.geometry.northAngle = wrap180(b.geometry.northAngle + 180.0);
        }
        Image wa, ma, wb, mb;
        derotateFrame(a, pa, pm, a.geometry, w, h, shape, o, wa, ma, nullptr, nullptr);
        derotateFrame(b, pb, pm, a.geometry, w, h, shape, o, wb, mb, nullptr, nullptr);
        have[flip] = detailCorrelation(wa, ma, wb, mb, a.geometry, hpSigma, corr[flip]);
    }
    if (correlationGiven) *correlationGiven = corr[0];
    if (correlationFlipped) *correlationFlipped = corr[1];
    if (!have[0] || !have[1]) return 0;
    // Decide only when one way is clearly better and actually matches.
    if (corr[0] > corr[1] + 0.15 && corr[0] > 0.2) return 1;
    if (corr[1] > corr[0] + 0.15 && corr[1] > 0.2) return -1;
    return 0;
}

// ----------------------------------------------------------------- derotate --

DerotateResult derotate(const std::vector<Frame>& framesIn, const DerotateOptions& optionsIn) {
    DerotateOptions options = optionsIn;
    DerotateResult res;
    if (framesIn.empty()) {
        res.error = "no images";
        return res;
    }
    for (const Frame& f : framesIn) {
        if (f.image.empty()) {
            res.error = "image '" + f.name + "' is empty";
            return res;
        }
        if (!(f.geometry.radius > 2.0)) {
            res.error = "image '" + f.name + "' has not been measured";
            return res;
        }
        if (!(f.geometry.radius < 1e5 && std::fabs(f.geometry.cx) < 1e6 && std::fabs(f.geometry.cy) < 1e6 &&
              std::isfinite(f.geometry.northAngle))) {
            res.error = "the measurement of image '" + f.name + "' is out of range";
            return res;
        }
        if (!(f.jdUtc > 2300000.0)) {
            res.error = "image '" + f.name + "' has no capture time";
            return res;
        }
    }
    std::vector<Frame> frames = framesIn;
    const size_t nf = frames.size();
    for (Frame& f : frames) {
        f.channel = upper(f.channel);
        if (!(f.weight > 0)) f.weight = 1.0;
    }

    // Reference time.
    double refJd = options.referenceJd;
    if (!(refJd > 0)) {
        double sw = 0, st = 0;
        const double base = frames[0].jdUtc;
        for (const Frame& f : frames) {
            sw += f.weight;
            st += f.weight * (f.jdUtc - base);
        }
        refJd = base + st / sw;
    }
    res.referenceJd = refJd;
    res.referenceEphemeris = computeEphemeris(options.planet, refJd);

    const BodyShape shape = bodyShape(options.planet);
    RotationModel model(options.planet, options.system, options.customRate, refJd);
    res.systemUsed = model.system();
    const Pose refPose = model.at(refJd);
    std::vector<Pose> poses(nf);
    for (size_t i = 0; i < nf; ++i) poses[i] = model.at(frames[i].jdUtc);

    int refIndex = 0;
    for (size_t i = 1; i < nf; ++i)
        if (std::fabs(frames[i].jdUtc - refJd) < std::fabs(frames[refIndex].jdUtc - refJd)) refIndex = static_cast<int>(i);
    res.referenceFrame = refIndex;

    // Output geometry.
    const Frame& rf = frames[refIndex];
    int outW = options.outputWidth > 0 ? options.outputWidth : rf.image.width();
    int outH = options.outputHeight > 0 ? options.outputHeight : rf.image.height();
    ImageGeometry outG = rf.geometry;
    if (options.northUp) {
        outG.cx = 0.5 * (outW - 1);
        outG.cy = 0.5 * (outH - 1);
        outG.northAngle = 0;
        outG.mirror = false;
    } else {
        outG.cx += 0.5 * (outW - rf.image.width());
        outG.cy += 0.5 * (outH - rf.image.height());
    }
    res.geometry = outG;

    res.frames.resize(nf);
    const bool west = res.referenceEphemeris.westLongitude;
    for (size_t i = 0; i < nf; ++i) {
        FrameReport& fr = res.frames[i];
        fr.minutesFromReference = (frames[i].jdUtc - refJd) * 1440.0;
        double dLonEast = wrap180(poses[i].orientation.lonEast() - refPose.orientation.lonEast());
        fr.rotationDeg = west ? -dLonEast : dLonEast;
        if (std::fabs(fr.rotationDeg) > 30.0)
            res.warnings.push_back("'" + frames[i].name + "' is " + std::to_string(static_cast<int>(std::fabs(fr.rotationDeg))) +
                                   " deg of rotation away from the reference time; it will only cover part of the disc");
    }

    // Luminance copies drive the brightness matching and the alignment.
    std::vector<Frame> lum(nf);
    for (size_t i = 0; i < nf; ++i) {
        lum[i] = frames[i];
        lum[i].image = frames[i].image.luminance();
    }

    // Channel groups, each with its own reference frame.
    std::map<std::string, std::vector<int>> groups;
    for (size_t i = 0; i < nf; ++i) groups[frames[i].channel].push_back(static_cast<int>(i));
    std::vector<int> groupRef(nf, 0);
    for (auto& g : groups) {
        int best = g.second[0];
        for (int i : g.second)
            if (std::fabs(frames[i].jdUtc - refJd) < std::fabs(frames[best].jdUtc - refJd)) best = i;
        for (int i : g.second) groupRef[i] = best;
    }

    // Brightness matching within each group.
    std::vector<double> scale(nf, 1.0);
    if (options.normalizeBrightness) {
        const double cp = projectedPolarRadius(shape, res.referenceEphemeris.de);
        std::vector<double> mean(nf);
        for (size_t i = 0; i < nf; ++i) mean[i] = innerDiscMean(lum[i].image, lum[i].geometry, cp);
        for (size_t i = 0; i < nf; ++i) {
            double m0 = mean[groupRef[i]];
            if (mean[i] > 1e-6 && m0 > 1e-6) scale[i] = m0 / mean[i];
            if (scale[i] < 0.5 || scale[i] > 2.0)
                res.warnings.push_back("'" + frames[i].name + "' differs a lot in brightness from its reference frame");
        }
    }
    for (size_t i = 0; i < nf; ++i) res.frames[i].brightnessScale = scale[i];

    // Catch the classic mistake: north and south (or the mirror setting) swapped.
    if (nf > 1 && checkRotationSense(frames, options) < 0)
        res.warnings.push_back("the features move the opposite way to the planet's rotation: north and south are probably "
                               "swapped (turn the north angle by 180 deg), or the mirror setting is wrong");

    // Limb darkening: measure it on the images unless a value was given.
    if (options.limbDarkening < 0) {
        std::vector<double> ks;
        for (size_t i = 0; i < nf; ++i) ks.push_back(fitLimbDarkening(lum[i].image, lum[i].geometry, poses[i], shape));
        std::nth_element(ks.begin(), ks.begin() + ks.size() / 2, ks.end());
        options.limbDarkening = ks[ks.size() / 2];
    }
    options.limbDarkening = std::min(1.0, std::max(0.0, options.limbDarkening));
    res.limbDarkeningUsed = options.limbDarkening;

    // ---- alignment refinement ------------------------------------------
    if (options.refineAlignment && nf > 1) {
        const DerotateOptions& ao = options;
        const double hpSigma = std::max(4.0, outG.radius / 8.0);

        auto warpLum = [&](int i, Image& img, Image& wgt) {
            derotateFrame(lum[i], poses[i], refPose, outG, outW, outH, shape, ao, img, wgt, nullptr, nullptr);
        };
        // Move the centre of frame i so that its warped image shifts by -t.
        auto applyShift = [&](int i, double tx, double ty) {
            double x0, y0, x1, y1;
            outG.pixelToView(outG.cx, outG.cy, x0, y0);
            outG.pixelToView(outG.cx + tx, outG.cy + ty, x1, y1);
            double u0, v0, u1, v1;
            lum[i].geometry.viewToPixel(0, 0, u0, v0);
            lum[i].geometry.viewToPixel(x1 - x0, y1 - y0, u1, v1);
            lum[i].geometry.cx += u1 - u0;
            lum[i].geometry.cy += v1 - v0;
        };
        auto alignTo = [&](int i, const Image& target, const Image& targetW) {
            Image img, wgt;
            const ImageGeometry before = lum[i].geometry;
            // Coarse steps first (tolerant of a few pixels of error), then fine ones.
            const double lp[] = {3.0, 3.0, 1.5, 1.0, 1.0, 1.0};
            for (double sigma : lp) {
                warpLum(i, img, wgt);
                double tx = 0, ty = 0;
                if (!estimateShift(img, wgt, target, targetW, hpSigma, sigma, tx, ty)) {
                    lum[i].geometry = before;  // do not leave a half-finished correction behind
                    return false;
                }
                double len = std::sqrt(tx * tx + ty * ty);
                if (len > 4.0) {
                    tx *= 4.0 / len;
                    ty *= 4.0 / len;
                }
                applyShift(i, tx, ty);
                if (len < 0.01 && sigma <= 1.0) break;
            }
            return true;
        };

        std::vector<bool> failed(nf, false);
        // Pass 1: every frame against the reference frame (or its group's reference).
        std::vector<Image> refImg(nf), refWgt(nf);
        auto ensureRef = [&](int r) {
            if (refImg[r].empty()) warpLum(r, refImg[r], refWgt[r]);
        };
        // Group reference frames first, so that the others see their final position.
        if (options.alignAcrossChannels) {
            ensureRef(refIndex);
            for (auto& g : groups) {
                int gr = groupRef[g.second[0]];
                if (gr == refIndex) continue;
                if (!alignTo(gr, refImg[refIndex], refWgt[refIndex])) failed[gr] = true;
            }
        }
        for (size_t i = 0; i < nf; ++i) {
            int gr = groupRef[i];
            if (static_cast<int>(i) == gr) continue;
            ensureRef(gr);
            if (!alignTo(static_cast<int>(i), refImg[gr], refWgt[gr])) failed[i] = true;
        }

        // Pass 2: every frame against the combination of all the others,
        // which is less noisy than a single frame.
        if (nf > 2) {
            std::vector<Image> wi(nf), ww(nf);
            for (size_t i = 0; i < nf; ++i) warpLum(static_cast<int>(i), wi[i], ww[i]);
            const size_t npx = static_cast<size_t>(outW) * outH;
            for (size_t i = 0; i < nf; ++i) {
                if (static_cast<int>(i) == refIndex || failed[i]) continue;
                if (!options.alignAcrossChannels && static_cast<int>(i) == groupRef[i]) continue;
                Image sum(outW, outH, 1), sumW(outW, outH, 1);
                size_t others = 0;
                for (size_t j = 0; j < nf; ++j) {
                    if (j == i || failed[j]) continue;
                    if (!options.alignAcrossChannels && frames[j].channel != frames[i].channel) continue;
                    ++others;
                    const float s = static_cast<float>(scale[j]);
                    for (size_t p = 0; p < npx; ++p) {
                        sum.data()[p] += ww[j].data()[p] * wi[j].data()[p] * s;
                        sumW.data()[p] += ww[j].data()[p];
                    }
                }
                if (others < 2) continue;
                for (size_t p = 0; p < npx; ++p)
                    if (sumW.data()[p] > 0) sum.data()[p] /= sumW.data()[p];
                alignTo(static_cast<int>(i), sum, sumW);  // on failure the pass-1 position stays
            }
        }

        for (size_t i = 0; i < nf; ++i) {
            if (failed[i]) {
                res.warnings.push_back("could not fine-align '" + frames[i].name + "'; its measured position is used as is");
                lum[i].geometry = frames[i].geometry;
            }
            res.frames[i].shiftX = lum[i].geometry.cx - frames[i].geometry.cx;
            res.frames[i].shiftY = lum[i].geometry.cy - frames[i].geometry.cy;
            frames[i].geometry = lum[i].geometry;
        }
    }
    for (size_t i = 0; i < nf; ++i) res.frames[i].geometry = frames[i].geometry;

    // ---- combination ------------------------------------------------------
    const size_t npx = static_cast<size_t>(outW) * outH;
    for (auto& g : groups) {
        const int c = frames[g.second[0]].image.channels();
        for (int i : g.second)
            if (frames[i].image.channels() != c) {
                res.error = "images of the same filter must all be mono or all be colour";
                return res;
            }
        Image sumV(outW, outH, c), sumW(outW, outH, 1), plainSum(outW, outH, c), plainW(outW, outH, 1);
        double sumQ = 0;
        Image warped, weight, plain, plainValid;
        for (int i : g.second) {
            derotateFrame(frames[i], poses[i], refPose, outG, outW, outH, shape, options, warped, weight, &plain, &plainValid);
            const float s = static_cast<float>(scale[i]);
            const float q = static_cast<float>(frames[i].weight);
            sumQ += q;
            for (size_t p = 0; p < npx; ++p) {
                const float wv = weight.data()[p];
                const float pv = plainValid.data()[p] * q;
                sumW.data()[p] += wv;
                plainW.data()[p] += pv;
                for (int ch = 0; ch < c; ++ch) {
                    sumV.data()[p * c + ch] += wv * s * warped.data()[p * c + ch];
                    plainSum.data()[p * c + ch] += pv * s * plain.data()[p * c + ch];
                }
            }
        }
        ChannelOutput co;
        co.channel = g.first;
        co.frames = static_cast<int>(g.second.size());
        co.image = Image(outW, outH, c);
        for (size_t p = 0; p < npx; ++p) {
            const double wsum = sumW.data()[p];
            // Hand over to the plain average where the derotated data runs out (the extreme limb).
            const double blend = smoothstep(0.0, 0.04, wsum / sumQ);
            for (int ch = 0; ch < c; ++ch) {
                double d = wsum > 0 ? sumV.data()[p * c + ch] / wsum : 0.0;
                double pl = plainW.data()[p] > 0 ? plainSum.data()[p * c + ch] / plainW.data()[p] : 0.0;
                co.image.data()[p * c + ch] = static_cast<float>(blend * d + (1.0 - blend) * pl);
            }
        }
        res.channels.push_back(std::move(co));
    }

    // ---- assemble ---------------------------------------------------------
    if (res.channels.size() == 1) {
        res.image = res.channels[0].image;
    } else {
        const ChannelOutput* r = nullptr;
        const ChannelOutput* gch = nullptr;
        const ChannelOutput* b = nullptr;
        for (const ChannelOutput& co : res.channels) {
            if (co.channel == "R") r = &co;
            else if (co.channel == "G") gch = &co;
            else if (co.channel == "B") b = &co;
        }
        if (r && gch && b) {
            Image rr = r->image.luminance(), gg = gch->image.luminance(), bb = b->image.luminance();
            res.image = Image(outW, outH, 3);
            for (size_t p = 0; p < npx; ++p) {
                res.image.data()[p * 3 + 0] = rr.data()[p];
                res.image.data()[p * 3 + 1] = gg.data()[p];
                res.image.data()[p * 3 + 2] = bb.data()[p];
            }
        }
    }
    res.ok = true;
    return res;
}

}  // namespace fixmyplanet
