// SPDX-License-Identifier: GPL-3.0-or-later
// Derotation: bring images taken at different times to one reference time by
// moving every surface point along with the planet's rotation, then combine.
#pragma once

#include <limits>
#include <string>
#include <vector>

#include "ephem.h"
#include "geometry.h"
#include "image.h"

namespace fixmyplanet {

// One measured input image.
struct Frame {
    Image image;             // 1 or 3 channels
    double jdUtc = 0;        // mid-capture time
    ImageGeometry geometry;  // where the planet is in the image
    std::string channel;     // filter label: "" (plain), "L", "R", "G", "B", ...
    double weight = 1.0;     // relative quality
    std::string name;        // for messages
};

struct DerotateOptions {
    Planet planet = Planet::Jupiter;
    int system = -1;                 // rotation system index; -1 = the planet's default
    // If finite, use this rotation rate (degrees/day, sidereal, in the IAU
    // frame) instead of the system's rate. Positive = prograde.
    double customRate = std::numeric_limits<double>::quiet_NaN();
    double referenceJd = 0;          // UTC; 0 = weighted mean of the frame times

    Interpolation interpolation = Interpolation::Bicubic;
    // A frame contributes to a surface point with a weight that ramps from 0
    // to full between these cosines of the emission angle in that frame.
    double limbMuLow = 0.15;
    double limbMuHigh = 0.40;
    // Limb darkening compensation: brightness is modelled as (1 - k) + k * mu
    // (mu = cosine of the solar incidence angle) and each sample is rescaled
    // from its own mu to the mu at the reference time. 0 switches the
    // compensation off; a negative value means "measure k on the images".
    double limbDarkening = -1.0;
    bool normalizeBrightness = true; // match the mean disc brightness of the frames
    bool refineAlignment = true;     // fine-tune the frame centres against each other
    bool alignAcrossChannels = true; // also align different filters to each other

    // Output geometry: by default the reference frame's own geometry and size.
    bool northUp = false;            // centre the disc, north up, not mirrored
    int outputWidth = 0, outputHeight = 0;  // 0 = size of the reference frame

    int threads = 0;
};

struct FrameReport {
    double rotationDeg = 0;          // rotation applied (central meridian difference to the reference)
    double minutesFromReference = 0;
    double shiftX = 0, shiftY = 0;   // centre correction found by the alignment, pixels
    double brightnessScale = 1.0;
    ImageGeometry geometry;          // geometry actually used
};

struct ChannelOutput {
    std::string channel;
    Image image;                     // combined image of this channel group
    int frames = 0;
};

struct DerotateResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;

    double referenceJd = 0;
    PhysicalEphemeris referenceEphemeris;
    ImageGeometry geometry;          // geometry of the output images
    int referenceFrame = 0;          // index of the frame closest to the reference time
    int systemUsed = 0;
    double limbDarkeningUsed = 0;    // coefficient applied (measured or given)

    std::vector<ChannelOutput> channels;  // one combined image per filter group
    // The assembled picture: the single group if there is only one, R+G+B as
    // colour if those three groups exist; empty otherwise.
    Image image;
    std::vector<FrameReport> frames;
};

// Orientation of the planet and direction of the Sun at one instant, both in
// the body frame of the chosen longitude system.
struct Pose {
    Orientation orientation;
    Vec3 sunBody{1, 0, 0};
};

// Gives the pose of the planet at a UTC time for the chosen rotation system or rate.
class RotationModel {
public:
    // system = -1 selects the planet's default system. A finite customRate
    // (degrees/day, same sign convention as the IAU prime meridian rate)
    // replaces the system's rate; longitudes then agree with the IAU system
    // at referenceJd.
    RotationModel(Planet planet, int system, double customRate, double referenceJd);
    Pose at(double jdUtc) const;
    // Central meridian (planetographic) in the model's longitude system.
    double centralMeridian(double jdUtc) const;
    int system() const { return system_; }
    Planet planet() const { return planet_; }

private:
    double offset(double jdUtc) const;
    Planet planet_;
    int system_;
    double customRate_;
    double referenceJd_;
};

// Warp one frame to the output geometry at the reference pose.
// `warped` receives the derotated image, `weight` the per-pixel weight
// (0 where the frame has nothing reliable to say), and `plain`/`plainValid`
// (optional) the frame merely shifted, scaled and turned without derotation.
void derotateFrame(const Frame& frame, const Pose& framePose, const Pose& refPose,
                   const ImageGeometry& outGeometry, int outWidth, int outHeight, const BodyShape& shape,
                   const DerotateOptions& options, Image& warped, Image& weight, Image* plain, Image* plainValid);

DerotateResult derotate(const std::vector<Frame>& frames, const DerotateOptions& options);

// The outline of a planet does not tell north from south, but the motion of
// the surface between two images does. This derotates the two images of one
// filter that are furthest apart in time, once with the north angles as given
// and once with them turned by 180 degrees, and compares how well the surface
// detail then agrees.
// Returns +1 if the angles as given are clearly right, -1 if the turned ones
// are, and 0 if it cannot tell (one image, too little rotation, no detail).
// Note that a mirror-reversed image with the opposite pole moves the same
// way, so this cannot detect a wrong mirror setting on its own.
int checkRotationSense(const std::vector<Frame>& frames, const DerotateOptions& options,
                       double* correlationGiven = nullptr, double* correlationFlipped = nullptr);

}  // namespace fixmyplanet
