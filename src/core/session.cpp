// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "imageio.h"
#include "json.h"
#include "timeutil.h"

namespace fixmyplanet {

namespace {

std::filesystem::path toPath(const std::string& utf8) {
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

std::string fromPath(const std::filesystem::path& p) {
#if defined(__cpp_lib_char8_t)
    std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
#else
    return p.generic_u8string();
#endif
}

}  // namespace

std::string Session::toJson() const {
    Json root = Json::object();
    root.set("format", "fixmyplanet-session");
    root.set("version", 1);
    root.set("planet", planetInfo(planet).name);
    root.set("system", system);
    root.set("custom_rate_deg_per_day", std::isfinite(customRate) ? Json(customRate) : Json());
    root.set("reference_time", referenceJd > 0 ? Json(formatTime(referenceJd, 1)) : Json());
    root.set("limb_darkening", limbDarkening >= 0 ? Json(limbDarkening) : Json("auto"));
    root.set("north_up", northUp);
    root.set("refine_alignment", refineAlignment);
    root.set("normalize_brightness", normalizeBrightness);
    Json arr = Json::array();
    for (const SessionImage& im : images) {
        Json o = Json::object();
        o.set("file", im.path);
        o.set("time", im.jdUtc > 0 ? Json(formatTime(im.jdUtc, 1)) : Json());
        o.set("channel", im.channel);
        o.set("measured", im.measured);
        o.set("cx", im.geometry.cx);
        o.set("cy", im.geometry.cy);
        o.set("radius", im.geometry.radius);
        o.set("north_angle", im.geometry.northAngle);
        o.set("mirror", im.geometry.mirror);
        o.set("weight", im.weight);
        arr.push(o);
    }
    root.set("images", arr);
    return root.dump(2);
}

bool Session::fromJson(const std::string& text, Session& out, std::string& error) {
    Json root;
    if (!Json::parse(text, root, error)) {
        error = "not valid JSON: " + error;
        return false;
    }
    if (!root.isObject() || root.get("format").string() != "fixmyplanet-session") {
        error = "not a fixmyplanet session file";
        return false;
    }
    if (root.get("version").number(1) > 1) {
        error = "this session file was written by a newer version";
        return false;
    }
    Session s;
    if (!planetFromName(root.get("planet").string("Jupiter"), s.planet)) {
        error = "unknown planet '" + root.get("planet").string() + "'";
        return false;
    }
    s.system = root.get("system").string();
    if (root.get("custom_rate_deg_per_day").isNumber()) s.customRate = root.get("custom_rate_deg_per_day").number();
    if (root.get("reference_time").isString() && !parseTime(root.get("reference_time").string(), s.referenceJd)) {
        error = "bad reference_time";
        return false;
    }
    s.limbDarkening = root.get("limb_darkening").number(-1.0);
    if (s.limbDarkening > 1.0) s.limbDarkening = 1.0;
    if (std::isfinite(s.customRate) && std::fabs(s.customRate) > 1e5) {
        error = "custom_rate_deg_per_day is out of range";
        return false;
    }
    s.northUp = root.get("north_up").boolean(false);
    s.refineAlignment = root.get("refine_alignment").boolean(true);
    s.normalizeBrightness = root.get("normalize_brightness").boolean(true);
    const Json& arr = root.get("images");
    if (arr.isArray()) {
        for (const Json& o : arr.items()) {
            if (!o.isObject()) continue;
            SessionImage im;
            im.path = o.get("file").string();
            if (im.path.empty()) {
                error = "an image entry has no file name";
                return false;
            }
            if (o.get("time").isString() && !parseTime(o.get("time").string(), im.jdUtc)) {
                error = "bad time for " + im.path;
                return false;
            }
            im.channel = o.get("channel").string();
            im.measured = o.get("measured").boolean(false);
            im.geometry.cx = o.get("cx").number(0);
            im.geometry.cy = o.get("cy").number(0);
            im.geometry.radius = o.get("radius").number(0);
            im.geometry.northAngle = o.get("north_angle").number(0);
            im.geometry.mirror = o.get("mirror").boolean(false);
            im.weight = o.get("weight").number(1.0);
            const ImageGeometry& g = im.geometry;
            const bool finite = std::isfinite(g.cx) && std::isfinite(g.cy) && std::isfinite(g.radius) &&
                                std::isfinite(g.northAngle) && std::isfinite(im.weight);
            if (!finite || std::fabs(g.cx) > 1e6 || std::fabs(g.cy) > 1e6 || g.radius < 0 || g.radius > 1e5 ||
                std::fabs(g.northAngle) > 3600 || im.weight <= 0 || im.weight > 1e6 ||
                (im.measured && g.radius < 2.0)) {
                error = "the measurement stored for " + im.path + " is out of range";
                return false;
            }
            s.images.push_back(im);
        }
    }
    out = s;
    return true;
}

DerotateOptions Session::options() const {
    DerotateOptions o;
    o.planet = planet;
    o.system = system.empty() ? -1 : systemIndex(planetInfo(planet), system);
    o.customRate = customRate;
    o.referenceJd = referenceJd;
    o.limbDarkening = limbDarkening;
    o.northUp = northUp;
    o.refineAlignment = refineAlignment;
    o.normalizeBrightness = normalizeBrightness;
    return o;
}

bool loadSession(const std::string& path, Session& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) return false;
    return Session::fromJson(std::string(bytes.begin(), bytes.end()), out, error);
}

bool saveSession(const std::string& path, const Session& session, std::string& error) {
    std::string text = session.toJson();
    return writeFile(path, std::vector<uint8_t>(text.begin(), text.end()), error);
}

std::string resolvePath(const std::string& sessionFile, const std::string& imagePath) {
    std::filesystem::path p = toPath(imagePath);
    if (p.is_absolute() || sessionFile.empty()) return imagePath;
    return fromPath(toPath(sessionFile).parent_path() / p);
}

std::string relativeToSession(const std::string& sessionFile, const std::string& imagePath) {
    std::error_code ec;
    std::filesystem::path base = std::filesystem::absolute(toPath(sessionFile), ec).parent_path();
    if (ec) return imagePath;
    std::filesystem::path img = std::filesystem::absolute(toPath(imagePath), ec);
    if (ec) return imagePath;
    std::filesystem::path rel = img.lexically_normal().lexically_relative(base.lexically_normal());
    if (rel.empty()) return fromPath(img);
    std::string r = fromPath(rel);
    // Keep absolute paths for images outside the session folder: they survive
    // moving the session file better than a chain of "../".
    if (r.compare(0, 2, "..") == 0) return fromPath(img);
    return r;
}

std::string channelFromFileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    static const char* labels[] = {"CH4", "IR", "UV", "R", "G", "B", "L"};
    std::vector<std::string> tokens;
    std::string cur;
    for (char ch : name) {
        if (ch == '-' || ch == '_' || ch == '.' || ch == ' ') {
            if (!cur.empty()) tokens.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    for (const std::string& t : tokens)
        for (const char* l : labels)
            if (t == l) return l;
    return "";
}

}  // namespace fixmyplanet
