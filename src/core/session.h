// SPDX-License-Identifier: GPL-3.0-or-later
// A session: the list of images with their times and measurements, plus the
// derotation settings. Stored as JSON; image paths are kept relative to the
// session file when possible.
#pragma once

#include <limits>
#include <string>
#include <vector>

#include "derotate.h"
#include "geometry.h"
#include "planets.h"

namespace fixmyplanet {

struct SessionImage {
    std::string path;        // as stored (relative to the session file, or absolute)
    double jdUtc = 0;        // 0 = unknown
    std::string channel;     // filter label
    ImageGeometry geometry;
    bool measured = false;
    double weight = 1.0;
};

struct Session {
    Planet planet = Planet::Jupiter;
    std::string system;      // "" = planet default
    double customRate = std::numeric_limits<double>::quiet_NaN();
    double referenceJd = 0;  // 0 = automatic
    double limbDarkening = -1.0;  // negative = measure it on the images
    bool northUp = false;
    bool refineAlignment = true;
    bool normalizeBrightness = true;
    std::vector<SessionImage> images;

    std::string toJson() const;
    static bool fromJson(const std::string& text, Session& out, std::string& error);

    // Fill a DerotateOptions from the session settings.
    DerotateOptions options() const;
};

bool loadSession(const std::string& path, Session& out, std::string& error);
bool saveSession(const std::string& path, const Session& session, std::string& error);

// Resolve an image path stored in a session against the session file's folder.
std::string resolvePath(const std::string& sessionFile, const std::string& imagePath);
// Make `imagePath` relative to the session file's folder when it lies inside it.
std::string relativeToSession(const std::string& sessionFile, const std::string& imagePath);

// Guess a filter label from a file name: a token R, G, B, L, IR, UV or CH4
// delimited by '-', '_', '.' or the ends of the name. Empty if none is found.
std::string channelFromFileName(const std::string& path);

}  // namespace fixmyplanet
