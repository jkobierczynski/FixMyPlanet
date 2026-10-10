# Changelog

## 0.1.0 (unreleased)

First version.

- Physical ephemeris for Mars, Jupiter, Saturn, Uranus and Neptune (central
  meridian in Systems I, II and III for Jupiter, tilt, position angle of the
  axis, diameter, phase), checked against JPL Horizons.
- Automatic measurement of the disc in an image: centre, radius, and the
  direction of the axis from Jupiter's belts or Saturn's rings.
- Derotation of any number of stacked images to one reference time, with
  fine alignment between images, brightness matching and limb darkening
  compensation. Saturn's rings are left untouched.
- Separate R, G and B images are derotated to a common time and combined.
- Detection of swapped north and south from the direction the features move.
- Reads TIFF, PNG, FITS, JPEG and BMP; writes 8/16-bit and floating point TIFF,
  8/16-bit PNG and FITS.
- Command-line tool `fixmyplanet` and desktop application `fixmyplanet-gui` (Qt 6).
- Synthetic planet generator for practice and for the test-suite.
