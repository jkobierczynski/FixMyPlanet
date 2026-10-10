// SPDX-License-Identifier: GPL-3.0-or-later
// fixmyplanet - command-line planetary derotation.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "derotate.h"
#include "ephem.h"
#include "imageio.h"
#include "measure.h"
#include "overlay.h"
#include "session.h"
#include "strutil.h"
#include "synth.h"
#include "timeutil.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using namespace fixmyplanet;

namespace {

const char* kUsage = R"(fixmyplanet - planetary derotation

Usage:
  fixmyplanet ephem   --planet NAME [--time UTC]...
  fixmyplanet measure --planet NAME -o SESSION.json [options] IMAGE...
  fixmyplanet run     SESSION.json -o OUTPUT [options]
  fixmyplanet auto    --planet NAME -o OUTPUT [measure and run options] IMAGE...
  fixmyplanet overlay SESSION.json --out-dir DIR
  fixmyplanet synth   --planet NAME --out-dir DIR [options]
  fixmyplanet version | help

Planets: mars, jupiter, saturn, uranus, neptune.
Times are UTC, written as 2026-01-10T22:03:30 (the middle of the capture).

measure options:
  --time UTC           capture time of the next image on the command line;
                       repeat once per image. Without it the time is read
                       from the file name or the FITS DATE-OBS header.
  --start UTC --interval SECONDS
                       give the images evenly spaced times instead
  --channel LABEL      filter of the next image (R, G, B, L, ...); repeat once
                       per image. Without it the label is read from the name.
  --north-angle DEG    rough direction of the planet's north pole, degrees
                       counter-clockwise from up (default 0; 180 = south up)
  --mirror             the images are mirror-reversed
  --no-orientation     keep the north angle exactly as given
  --same-angle         give every image the median north angle (fixed camera
                       on an equatorial mount)
  --individual-angle   keep each image's own fitted north angle (default: a
                       straight line through the angles against time, which
                       also follows the field rotation of an alt-az mount)
  --individual-radius  keep each image's own fitted radius (default: all
                       images get the median radius)
  --pixel-scale ARCSEC image scale in arcsec/pixel; the radius is then taken
                       from the ephemeris instead of being fitted

run options:
  -o FILE              output image (.tif, .png or .fits)
  --system NAME        longitude system: I, II or III (Jupiter), III (others)
  --rate DEG_PER_DAY   use this rotation rate instead of a named system
  --ref-time UTC       derotate to this time (default: mean of the images)
  --limb-darkening K   limb darkening to compensate, 0 (off) to 1, or 'auto'
                       to measure it on the images (default)
  --north-up           output with the disc centred and north up
  --no-align           do not fine-tune the image positions
  --no-normalize       do not match the brightness of the images
  --interp NAME        bilinear, bicubic (default) or lanczos
  --depth N            8, 16 (default) or 32 (floating point; TIFF and FITS)
  --save-frames DIR    also write each derotated image on its own
  --threads N          worker threads (default: all cores)

synth options:
  --start UTC          time of the first image (default 2026-01-10T22:00:00)
  --count N            number of images (default 9)
  --interval SECONDS   spacing (default 120)
  --size PIXELS        image width and height (default 640)
  --radius PIXELS      equatorial radius (default 200)
  --north-angle DEG    orientation of the planet in the images (default 0)
  --mirror             mirror-reversed images
  --psf SIGMA          blur, pixels (default 1.2)
  --noise SIGMA        noise, 0..1 (default 0.01)
  --jitter PIXELS      random offset of the disc from image to image (default 3)
  --colour             colour images (default mono)
  --filters RGB        mono images cycling through the R, G and B filters
  --truth              also write the noise-free image at the middle time
  --seed N             random seed (default 1)
)";

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> opts;
    // Options in the order given, with the number of positionals seen before each.
    std::vector<std::pair<std::string, std::string>> ordered;

    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = opts.find(k);
        return it == opts.end() || it->second.empty() ? def : it->second.back();
    }
    std::vector<std::string> all(const std::string& k) const {
        auto it = opts.find(k);
        return it == opts.end() ? std::vector<std::string>() : it->second;
    }
};

struct Fail {
    std::string message;
};

std::vector<std::string> expandWildcard(const std::string& arg);  // defined near main()

bool isFlag(const std::string& name) {
    static const char* flags[] = {"--mirror", "--no-orientation", "--same-angle", "--individual-angle", "--individual-radius", "--north-up",
                                  "--no-align", "--no-normalize", "--colour", "--color", "--truth"};
    for (const char* f : flags)
        if (name == f) return true;
    return false;
}

Args parseArgs(const std::vector<std::string>& argv, size_t first) {
    Args a;
    for (size_t i = first; i < argv.size(); ++i) {
        const std::string& s = argv[i];
        if (s == "--") {
            for (size_t j = i + 1; j < argv.size(); ++j) a.positional.push_back(argv[j]);
            break;
        }
        // A lone "-" or a negative number is not an option.
        bool looksLikeOption = s.size() > 1 && s[0] == '-' && !(std::isdigit(static_cast<unsigned char>(s[1])) || s[1] == '.');
        if (!looksLikeOption) {
            a.positional.push_back(s);
            continue;
        }
        std::string name = s == "-o" ? "--output" : s == "-p" ? "--planet" : s;
        if (name == "--color") name = "--colour";
        if (isFlag(name)) {
            a.opts[name].push_back("1");
            continue;
        }
        if (i + 1 >= argv.size()) throw Fail{"option " + s + " needs a value"};
        a.opts[name].push_back(argv[++i]);
        a.ordered.emplace_back(name, argv[i]);
    }
    return a;
}

double numberOpt(const Args& a, const std::string& key, double def) {
    if (!a.has(key)) return def;
    double v;
    if (!parseDouble(a.get(key), v)) throw Fail{"option " + key + ": '" + a.get(key) + "' is not a number"};
    return v;
}

double timeOpt(const Args& a, const std::string& key, double def) {
    if (!a.has(key)) return def;
    double jd;
    if (!parseTime(a.get(key), jd)) throw Fail{"option " + key + ": '" + a.get(key) + "' is not a UTC time like 2026-01-10T22:03:30"};
    return jd;
}

Planet planetOpt(const Args& a) {
    if (!a.has("--planet")) throw Fail{"please give the planet with --planet"};
    Planet p;
    if (!planetFromName(a.get("--planet"), p)) throw Fail{"unknown planet '" + a.get("--planet") + "'"};
    return p;
}

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void checkKnownOptions(const Args& a, std::initializer_list<const char*> known) {
    for (const auto& kv : a.opts) {
        bool ok = false;
        for (const char* k : known)
            if (kv.first == k) ok = true;
        if (!ok) throw Fail{"unknown option " + kv.first + " for this command (see 'fixmyplanet help')"};
    }
}

void printEphemeris(const PhysicalEphemeris& e) {
    const PlanetInfo& info = planetInfo(e.planet);
    std::printf("%s  %s\n", info.name.c_str(), formatTime(e.jdUtc, 0).c_str());
    for (size_t s = 0; s < info.systems.size(); ++s)
        std::printf("  CM System %-3s      %8.2f deg   (%s)\n", info.systems[s].name.c_str(), e.cm[s], info.systems[s].note.c_str());
    std::printf("  D_E (tilt)         %+8.2f deg   sub-Earth latitude, planetographic: %+.2f\n", e.de, e.subEarthLatGraphic);
    std::printf("  D_S                %+8.2f deg\n", e.ds);
    std::printf("  North pole angle   %8.2f deg   from celestial north through east\n", e.poleAngle);
    std::printf("  Diameter           %8.2f arcsec (equatorial)\n", e.diameterArcsec);
    std::printf("  Phase angle        %8.2f deg\n", e.phaseAngleDeg);
    std::printf("  Distance           %8.4f AU    light time %.1f min\n", e.distanceAU, e.lightTimeMinutes);
    std::printf("  RA, Dec (J2000)    %8.4f deg  %+.4f deg\n", e.raDeg, e.decDeg);
}

int cmdEphem(const Args& a) {
    checkKnownOptions(a, {"--planet", "--time"});
    Planet p = planetOpt(a);
    std::vector<std::string> times = a.all("--time");
    if (times.empty()) throw Fail{"please give at least one --time"};
    for (const std::string& t : times) {
        double jd;
        if (!parseTime(t, jd)) throw Fail{"'" + t + "' is not a UTC time like 2026-01-10T22:03:30"};
        printEphemeris(computeEphemeris(p, jd));
    }
    return 0;
}

// Measure every image, print one line per image and return the session.
Session measureImages(const Args& a, Planet planet) {
    if (a.positional.empty()) throw Fail{"no image files given"};
    const std::vector<std::string>& files = a.positional;
    std::vector<std::string> times = a.all("--time"), channels = a.all("--channel");
    if (!times.empty() && times.size() != files.size())
        throw Fail{"--time was given " + std::to_string(times.size()) + " times for " + std::to_string(files.size()) + " images"};
    if (!channels.empty() && channels.size() != files.size())
        throw Fail{"--channel was given " + std::to_string(channels.size()) + " times for " + std::to_string(files.size()) + " images"};
    const bool spaced = a.has("--start") || a.has("--interval");
    if (spaced && !(a.has("--start") && a.has("--interval"))) throw Fail{"--start and --interval go together"};
    const double start = timeOpt(a, "--start", 0), interval = numberOpt(a, "--interval", 0);

    MeasureOptions mo;
    mo.northAngleHint = numberOpt(a, "--north-angle", 0.0);
    mo.mirror = a.has("--mirror");
    mo.fitOrientation = !a.has("--no-orientation");
    mo.pixelScaleArcsec = numberOpt(a, "--pixel-scale", 0.0);

    Session s;
    s.planet = planet;
    std::vector<Frame> loaded;  // kept for the rotation-sense check below
    std::printf("%-36s %-21s %-3s %8s %8s %8s %8s %6s  %s\n", "image", "time (UTC)", "flt", "cx", "cy", "radius", "north", "rms", "");
    for (size_t i = 0; i < files.size(); ++i) {
        SessionImage im;
        im.path = files[i];
        Image img;
        ImageFileInfo info;
        std::string err;
        if (!loadImage(files[i], img, &info, err)) throw Fail{err};

        if (!times.empty()) {
            if (!parseTime(times[i], im.jdUtc)) throw Fail{"'" + times[i] + "' is not a UTC time like 2026-01-10T22:03:30"};
        } else if (spaced) {
            im.jdUtc = start + i * interval / 86400.0;
        } else if (!timeFromFileName(files[i], im.jdUtc) && !(parseTime(info.dateObs, im.jdUtc))) {
            throw Fail{"no capture time for " + files[i] + ": none in the file name; give --time or --start/--interval"};
        }
        im.channel = !channels.empty() ? channels[i] : channelFromFileName(files[i]);

        MeasureResult r = measureDisc(img, computeEphemeris(planet, im.jdUtc), mo);
        if (!r.ok) throw Fail{files[i] + ": " + r.message};
        im.geometry = r.geometry;
        im.measured = true;
        std::string note = r.message;
        if (r.orientationFitted) note = note.empty() ? "angle fitted" : "angle fitted; " + note;
        std::printf("%-36s %-21s %-3s %8.2f %8.2f %8.2f %8.2f %6.2f  %s\n", baseName(files[i]).substr(0, 36).c_str(),
                    formatTime(im.jdUtc, 0).c_str(), im.channel.c_str(), r.geometry.cx, r.geometry.cy, r.geometry.radius,
                    r.geometry.northAngle, r.residualRms, note.c_str());
        s.images.push_back(im);
        Frame f;
        f.image = img.luminance();
        f.jdUtc = im.jdUtc;
        f.channel = im.channel;
        loaded.push_back(std::move(f));
    }

    if (s.images.size() > 1) {
        std::vector<ImageGeometry> geo;
        std::vector<double> jd;
        for (const SessionImage& im : s.images) {
            geo.push_back(im.geometry);
            jd.push_back(im.jdUtc);
        }
        // The angle can only be smoothed where it was actually measured.
        const bool angleMeasured = mo.fitOrientation && (planet == Planet::Jupiter || planet == Planet::Saturn);
        AngleMode mode = AngleMode::Individual;
        if (angleMeasured) mode = a.has("--same-angle") ? AngleMode::Same : a.has("--individual-angle") ? AngleMode::Individual : AngleMode::Trend;
        RegulariseReport rep = regulariseMeasurements(geo, jd, mode, !a.has("--individual-radius") && mo.pixelScaleArcsec <= 0);
        for (size_t i = 0; i < geo.size(); ++i) s.images[i].geometry = geo[i];
        if (rep.radiusChanged) std::printf("radius set to the median of all images: %.2f px\n", rep.radius);
        if (rep.angleChanged) {
            if (mode == AngleMode::Trend && s.images.size() >= 4)
                std::printf("north angle smoothed over the session: %.2f deg at the first image, drifting %+.3f deg/min\n",
                            rep.angleAtFirst, rep.driftDegPerMinute);
            else
                std::printf("north angle set to the median of all images: %.2f deg\n", rep.angleAtFirst);
        }

        // The limb cannot tell north from south; the way the features move can.
        for (size_t i = 0; i < loaded.size(); ++i) loaded[i].geometry = s.images[i].geometry;
        DerotateOptions probe;
        probe.planet = planet;
        if (checkRotationSense(loaded, probe) < 0) {
            if (mo.fitOrientation) {
                for (SessionImage& im : s.images) im.geometry.northAngle = wrap180(im.geometry.northAngle + 180.0);
                std::printf("the features move the other way round: north and south were swapped, north angle is now %.2f deg\n",
                            s.images[0].geometry.northAngle);
            } else {
                std::printf("warning: the features move the opposite way to the planet's rotation; check --north-angle and --mirror\n");
            }
        }
    }
    return s;
}

void applyRunOptions(const Args& a, Session& s, DerotateOptions& o) {
    if (a.has("--system")) {
        if (systemIndex(planetInfo(s.planet), a.get("--system")) < 0)
            throw Fail{planetInfo(s.planet).name + " has no longitude system '" + a.get("--system") + "'"};
        s.system = a.get("--system");
    }
    if (a.has("--rate")) s.customRate = numberOpt(a, "--rate", 0);
    if (a.has("--ref-time")) s.referenceJd = timeOpt(a, "--ref-time", 0);
    if (a.has("--limb-darkening")) {
        if (a.get("--limb-darkening") == "auto") s.limbDarkening = -1.0;
        else {
            s.limbDarkening = numberOpt(a, "--limb-darkening", 0);
            if (s.limbDarkening < 0 || s.limbDarkening > 1) throw Fail{"--limb-darkening must be 'auto' or between 0 and 1"};
        }
    }
    if (a.has("--north-up")) s.northUp = true;
    if (a.has("--no-align")) s.refineAlignment = false;
    if (a.has("--no-normalize")) s.normalizeBrightness = false;
    o = s.options();
    std::string interp = a.get("--interp", "bicubic");
    if (interp == "bilinear") o.interpolation = Interpolation::Bilinear;
    else if (interp == "bicubic") o.interpolation = Interpolation::Bicubic;
    else if (interp == "lanczos") o.interpolation = Interpolation::Lanczos3;
    else throw Fail{"--interp must be bilinear, bicubic or lanczos"};
    o.threads = static_cast<int>(numberOpt(a, "--threads", 0));
}

std::string withSuffix(const std::string& path, const std::string& suffix) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path + suffix;
    return path.substr(0, dot) + suffix + path.substr(dot);
}

int runSession(const Args& a, Session& s, const std::string& sessionFile) {
    if (!a.has("--output")) throw Fail{"please give the output file with -o"};
    const std::string outPath = a.get("--output");
    DerotateOptions o;
    applyRunOptions(a, s, o);

    SaveOptions so;
    double depth = numberOpt(a, "--depth", 16);
    if (depth == 8) so.depth = SampleDepth::U8;
    else if (depth == 16) so.depth = SampleDepth::U16;
    else if (depth == 32) so.depth = SampleDepth::F32;
    else throw Fail{"--depth must be 8, 16 or 32"};

    if (s.images.empty()) throw Fail{"the session has no images"};
    std::vector<Frame> frames;
    for (const SessionImage& im : s.images) {
        if (!im.measured) throw Fail{im.path + " has not been measured yet"};
        if (!(im.jdUtc > 0)) throw Fail{im.path + " has no capture time"};
        Frame f;
        std::string err;
        std::string path = resolvePath(sessionFile, im.path);
        if (!loadImage(path, f.image, nullptr, err)) throw Fail{err};
        f.jdUtc = im.jdUtc;
        f.geometry = im.geometry;
        f.channel = im.channel;
        f.weight = im.weight;
        f.name = baseName(im.path);
        frames.push_back(std::move(f));
    }

    DerotateResult r = derotate(frames, o);
    if (!r.ok) throw Fail{r.error};

    const PlanetInfo& info = planetInfo(s.planet);
    const PhysicalEphemeris& e = r.referenceEphemeris;
    std::printf("\n%s, %zu image%s, derotated to %s\n", info.name.c_str(), frames.size(), frames.size() == 1 ? "" : "s",
                formatTime(r.referenceJd, 0).c_str());
    if (std::isfinite(o.customRate)) std::printf("rotation rate %.4f deg/day\n", o.customRate);
    else std::printf("longitude system %s\n", info.systems[r.systemUsed].name.c_str());
    std::string cmText;
    for (size_t k = 0; k < info.systems.size(); ++k) {
        if (k) cmText += "  ";
        cmText += "CM " + info.systems[k].name + " " + formatFixed(e.cm[k], 1);
    }
    std::printf("%s deg, D_E %+.2f deg\n", cmText.c_str(), e.de);
    std::printf("limb darkening coefficient %.2f%s\n\n", r.limbDarkeningUsed, s.limbDarkening < 0 ? " (measured)" : "");
    std::printf("%-36s %-3s %9s %9s %8s %8s %7s\n", "image", "flt", "minutes", "rotation", "shift x", "shift y", "bright");
    for (size_t i = 0; i < frames.size(); ++i) {
        const FrameReport& fr = r.frames[i];
        std::printf("%-36s %-3s %+9.2f %+9.2f %+8.2f %+8.2f %7.3f%s\n", frames[i].name.substr(0, 36).c_str(),
                    frames[i].channel.c_str(), fr.minutesFromReference, fr.rotationDeg, fr.shiftX, fr.shiftY,
                    fr.brightnessScale, static_cast<int>(i) == r.referenceFrame ? "  (reference)" : "");
    }
    for (const std::string& w : r.warnings) std::printf("warning: %s\n", w.c_str());

    so.description = "fixmyplanet " FIXMYPLANET_VERSION "; " + info.name + "; " + formatTime(r.referenceJd, 0) + "; " + cmText +
                     "; " + std::to_string(frames.size()) + " images";
    so.dateObs = formatTime(r.referenceJd, 1);
    std::string err;
    if (!r.image.empty()) {
        if (!saveImage(outPath, r.image, so, err)) throw Fail{err};
        std::printf("\nwrote %s\n", outPath.c_str());
    } else {
        std::printf("\nthe filters do not form an R, G, B set, so each one is written on its own:\n");
        for (const ChannelOutput& co : r.channels) {
            std::string p = withSuffix(outPath, "_" + (co.channel.empty() ? std::string("mono") : co.channel));
            if (!saveImage(p, co.image, so, err)) throw Fail{err};
            std::printf("wrote %s\n", p.c_str());
        }
    }

    if (a.has("--save-frames")) {
        const std::string dir = a.get("--save-frames");
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
        const BodyShape shape = bodyShape(s.planet);
        RotationModel model(o.planet, o.system, o.customRate, r.referenceJd);
        const Pose refPose = model.at(r.referenceJd);
        for (size_t i = 0; i < frames.size(); ++i) {
            Frame f = frames[i];
            f.geometry = r.frames[i].geometry;
            Image warped, weight;
            derotateFrame(f, model.at(f.jdUtc), refPose, r.geometry, r.channels[0].image.width(),
                          r.channels[0].image.height(), shape, o, warped, weight, nullptr, nullptr);
            std::string num = std::to_string(i + 1);
            if (num.size() < 3) num.insert(0, 3 - num.size(), '0');
            std::string p = dir + "/fixmyplanet_" + num + ".tif";
            if (!saveImage(p, warped, so, err)) throw Fail{err};
        }
        std::printf("wrote %zu derotated images to %s\n", frames.size(), dir.c_str());
    }
    return 0;
}

const std::initializer_list<const char*> kMeasureOpts = {"--planet", "--output", "--time", "--start", "--interval",
                                                         "--channel", "--north-angle", "--mirror", "--no-orientation",
                                                         "--same-angle", "--individual-angle", "--individual-radius",
                                                         "--pixel-scale"};

int cmdMeasure(const Args& a) {
    checkKnownOptions(a, kMeasureOpts);
    if (!a.has("--output")) throw Fail{"please give the session file to write with -o"};
    Planet p = planetOpt(a);
    Session s = measureImages(a, p);
    const std::string out = a.get("--output");
    for (SessionImage& im : s.images) im.path = relativeToSession(out, im.path);
    std::string err;
    if (!saveSession(out, s, err)) throw Fail{err};
    std::printf("wrote %s\n", out.c_str());
    return 0;
}

int cmdRun(const Args& a) {
    checkKnownOptions(a, {"--output", "--system", "--rate", "--ref-time", "--limb-darkening", "--north-up", "--no-align",
                          "--no-normalize", "--interp", "--depth", "--save-frames", "--threads"});
    if (a.positional.size() != 1) throw Fail{"please give one session file"};
    Session s;
    std::string err;
    if (!loadSession(a.positional[0], s, err)) throw Fail{a.positional[0] + ": " + err};
    return runSession(a, s, a.positional[0]);
}

int cmdAuto(const Args& a) {
    checkKnownOptions(a, {"--planet", "--output", "--time", "--start", "--interval", "--channel", "--north-angle",
                          "--mirror", "--no-orientation", "--same-angle", "--individual-angle", "--individual-radius",
                          "--pixel-scale", "--system", "--rate", "--ref-time", "--limb-darkening", "--north-up", "--no-align",
                          "--no-normalize", "--interp", "--depth", "--save-frames", "--threads"});
    Planet p = planetOpt(a);
    Session s = measureImages(a, p);
    return runSession(a, s, "");
}

int cmdOverlay(const Args& a) {
    checkKnownOptions(a, {"--out-dir"});
    if (a.positional.size() != 1) throw Fail{"please give one session file"};
    if (!a.has("--out-dir")) throw Fail{"please give --out-dir"};
    Session s;
    std::string err;
    if (!loadSession(a.positional[0], s, err)) throw Fail{a.positional[0] + ": " + err};
    const std::string dir = a.get("--out-dir");
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    const PlanetInfo& info = planetInfo(s.planet);
    const BodyShape shape = bodyShape(s.planet);
    std::vector<double> marks;
    for (double km : info.ringMarksKm) marks.push_back(km / info.equatorialRadiusKm);
    size_t n = 0;
    for (const SessionImage& im : s.images) {
        if (!im.measured || !(im.jdUtc > 0)) continue;
        Image img;
        if (!loadImage(resolvePath(a.positional[0], im.path), img, nullptr, err)) throw Fail{err};
        PhysicalEphemeris e = computeEphemeris(s.planet, im.jdUtc);
        Image out = drawOverlay(img, im.geometry, overlayCurves(shape, e.de, e.sunDirView, true, marks));
        SaveOptions so;
        so.depth = SampleDepth::U8;
        std::string name = baseName(im.path);
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        if (!saveImage(dir + "/" + name + "_overlay.png", out, so, err)) throw Fail{err};
        ++n;
    }
    std::printf("wrote %zu overlay images to %s\n", n, dir.c_str());
    return 0;
}

int cmdSynth(const Args& a) {
    checkKnownOptions(a, {"--planet", "--out-dir", "--start", "--count", "--interval", "--size", "--radius", "--north-angle",
                          "--mirror", "--psf", "--noise", "--jitter", "--colour", "--filters", "--truth", "--seed"});
    Planet planet = planetOpt(a);
    if (!a.has("--out-dir")) throw Fail{"please give --out-dir"};
    const std::string dir = a.get("--out-dir");
    double start;
    parseTime("2026-01-10T22:00:00", start);
    start = timeOpt(a, "--start", start);
    const int count = static_cast<int>(numberOpt(a, "--count", 9));
    const double interval = numberOpt(a, "--interval", 120);
    const int size = static_cast<int>(numberOpt(a, "--size", 640));
    const double radius = numberOpt(a, "--radius", 200);
    const double jitter = numberOpt(a, "--jitter", 3);
    const uint32_t seed = static_cast<uint32_t>(numberOpt(a, "--seed", 1));
    const std::string filters = a.get("--filters");
    const bool colour = a.has("--colour");
    if (count < 1 || count > 1000 || size < 64 || size > 8192 || radius < 10) throw Fail{"count, size or radius out of range"};
    if (!filters.empty() && filters != "RGB") throw Fail{"--filters only accepts RGB"};

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    const PlanetInfo& info = planetInfo(planet);
    const BodyShape shape = bodyShape(planet);
    const bool colourMap = colour || !filters.empty();
    SurfaceMap map = makeProceduralMap(planet, 2880, 1440, seed, colourMap);

    RenderOptions ro;
    ro.psfSigma = numberOpt(a, "--psf", 1.2);
    ro.noiseSigma = numberOpt(a, "--noise", 0.01);
    std::mt19937 rng(seed * 7919u + 13u);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    std::string lowerName = info.name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    RotationModel model(planet, -1, std::numeric_limits<double>::quiet_NaN(), start);
    auto render = [&](double jd, const ImageGeometry& g, const RenderOptions& opts) {
        PhysicalEphemeris e = computeEphemeris(planet, jd);
        return renderPlanet(map, shape, model.at(jd).orientation, g, size, size, e.sunDirView, opts);
    };
    auto stamp = [](double jd) {
        std::string t = formatTime(jd, 0);  // 2026-01-10T22:00:00Z
        t.pop_back();
        std::replace(t.begin(), t.end(), ':', '-');
        std::replace(t.begin(), t.end(), 'T', '_');
        return t;
    };

    SaveOptions so;
    std::string err;
    for (int i = 0; i < count; ++i) {
        double jd = start + i * interval / 86400.0;
        ImageGeometry g;
        g.cx = 0.5 * (size - 1) + jitter * uni(rng);
        g.cy = 0.5 * (size - 1) + jitter * uni(rng);
        g.radius = radius;
        g.northAngle = numberOpt(a, "--north-angle", 0);
        g.mirror = a.has("--mirror");
        ro.noiseSeed = seed * 1000u + static_cast<uint32_t>(i);
        Image img = render(jd, g, ro);
        std::string name = lowerName + "_" + stamp(jd);
        if (!filters.empty()) {
            int ch = i % 3;
            img = img.channel(ch);
            name += std::string("_") + "RGB"[ch];
        } else if (!colour) {
            img = img.luminance();
        }
        std::string path = dir + "/" + name + ".tif";
        if (!saveImage(path, img, so, err)) throw Fail{err};
        std::printf("%s  centre %.2f %.2f\n", path.c_str(), g.cx, g.cy);
    }
    if (a.has("--truth")) {
        double jd = start + 0.5 * (count - 1) * interval / 86400.0;
        ImageGeometry g;
        g.cx = g.cy = 0.5 * (size - 1);
        g.radius = radius;
        g.northAngle = numberOpt(a, "--north-angle", 0);
        g.mirror = a.has("--mirror");
        RenderOptions clean = ro;
        clean.noiseSigma = 0;
        Image img = render(jd, g, clean);
        if (!colourMap) img = img.luminance();
        std::string path = dir + "/truth_" + stamp(jd) + ".tif";
        if (!saveImage(path, img, so, err)) throw Fail{err};
        std::printf("%s  (noise-free, middle time)\n", path.c_str());
    }
    return 0;
}

int run(const std::vector<std::string>& argv) {
    if (argv.size() < 2 || argv[1] == "help" || argv[1] == "--help" || argv[1] == "-h") {
        std::fputs(kUsage, stdout);
        return argv.size() < 2 ? 2 : 0;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "version" || cmd == "--version") {
            std::printf("fixmyplanet %s\n", FIXMYPLANET_VERSION);
            return 0;
        }
        if (cmd == "expand") {
            // Not documented: prints how a wildcard argument would be expanded on
            // Windows. Lets the test-suite exercise that code on every platform.
            for (size_t i = 2; i < argv.size(); ++i)
                for (const std::string& e : expandWildcard(argv[i])) std::printf("%s\n", e.c_str());
            return 0;
        }
        Args a = parseArgs(argv, 2);
        if (cmd == "ephem") return cmdEphem(a);
        if (cmd == "measure") return cmdMeasure(a);
        if (cmd == "run") return cmdRun(a);
        if (cmd == "auto") return cmdAuto(a);
        if (cmd == "overlay") return cmdOverlay(a);
        if (cmd == "synth") return cmdSynth(a);
        std::fprintf(stderr, "fixmyplanet: unknown command '%s' (see 'fixmyplanet help')\n", cmd.c_str());
        return 2;
    } catch (const Fail& f) {
        std::fprintf(stderr, "fixmyplanet: %s\n", f.message.c_str());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fixmyplanet: %s\n", e.what());
        return 1;
    }
}

// Does `name` match `pattern`, where * stands for any run of characters and ?
// for one? ASCII letters compare without regard to case, as Windows does.
bool wildcardMatch(const std::string& pattern, const std::string& name) {
    size_t p = 0, n = 0, star = std::string::npos, mark = 0;
    auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || same(pattern[p], name[n]))) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// Unix shells expand "captures/*.tif" before the program starts; cmd.exe and
// PowerShell do not. Returns the matching files in name order, or the
// argument itself when it has no wildcard or matches nothing.
std::vector<std::string> expandWildcard(const std::string& arg) {
    if (arg.find_first_of("*?") == std::string::npos) return {arg};
    size_t slash = arg.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? "." : arg.substr(0, slash == 0 ? 1 : slash);
    const std::string pattern = slash == std::string::npos ? arg : arg.substr(slash + 1);
    const std::string prefix = slash == std::string::npos ? "" : arg.substr(0, slash + 1);
    std::vector<std::string> found;
    try {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) continue;
            const std::string name = it->path().filename().u8string();
            if (wildcardMatch(pattern, name)) found.push_back(prefix + name);
        }
    } catch (const std::exception&) {
        return {arg};
    }
    if (found.empty()) return {arg};
    std::sort(found.begin(), found.end());
    return found;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // file names are printed as UTF-8
    // Take the command line as UTF-16 so that non-ASCII file names survive.
    (void)argc;
    (void)argv;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    for (int i = 0; wargv && i < wargc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
        if (i == 0 || (!s.empty() && s[0] == '-')) {
            args.push_back(s);
        } else {
            for (const std::string& e : expandWildcard(s)) args.push_back(e);
        }
    }
    if (wargv) LocalFree(wargv);
#else
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return run(args);
}
