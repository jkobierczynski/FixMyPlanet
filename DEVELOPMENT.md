# Development notes

Where the project stands, what was decided and why, and what is open.
The project was called "Derot" while the first version was written; nothing
was published under that name.

## State at 0.1.0

| Part | State |
|------|-------|
| Ephemeris (Mars to Neptune) | done; agrees with JPL Horizons to 0.01° |
| Disc geometry, rings | done |
| Automatic measurement | done; two known biases (below) |
| Derotation and combination | done |
| Fine alignment between images | done (translation only) |
| R/G/B derotation | done |
| North/south check from feature motion | done |
| Image files: TIFF, PNG, FITS, JPEG, BMP | done |
| Command-line tool | done |
| Desktop application | done; exercised off-screen only (see below) |
| Tests | 7 test programs and an end-to-end run of the command-line tool; clean under ASan/UBSan |
| CI and release workflows | written, **never run** |
| Real telescope data | **not tried yet** |
| Comparison with WinJUPOS | **not done yet** |

### What has and has not been verified

Built and tested here: Linux x86-64, GCC 13 and Clang 18, Qt 6.4.2; the test
suite also under AddressSanitizer and UndefinedBehaviorSanitizer (GCC).

Not verified:

- **Windows and macOS.** The core, the command-line tool and the tests
  cross-compile for Windows with MinGW-w64 and build and pass against libc++
  (the macOS standard library) on Linux, but nothing has been compiled with
  MSVC or Apple Clang, and the desktop application has not been built for
  either system. Expect a round of small fixes on the first CI run.
- **The GitHub workflows.** The action versions were looked up on
  2026-10-09 (checkout v7, upload-artifact v7, download-artifact v8,
  install-qt-action v4, action-gh-release v3) and the YAML parses, but the
  jobs have not run. The Linux AppImage step was run by hand once (with Qt
  6.4.2) and produced an AppImage; the Windows and macOS packaging steps
  (windeployqt, macdeployqt, ad-hoc signing) are the most likely to need
  adjusting.
- **The desktop application by hand.** It was started with Qt's off-screen
  platform, fed a synthetic session, and its window was checked from
  screenshots. Mouse and keyboard handling have not been tried by a person.
- **Qt versions other than 6.4.2.** The minimum of 6.2 in CMake is an
  estimate from the API used.

## Decisions

**Language and layout.** C++17. `src/core` is a static library without Qt, so
that it can be tested on its own and used from the command line; `src/cli`
and `src/gui` are thin layers on it.

**Licence: GPL-3.0-or-later.** Version 3 of the GNU General Public License
or, at the user's option, any later version. Qt is used under the LGPL-3.0 /
GPL-3.0, which fits. The vendored code is public domain, MIT or zlib
licensed.

**No external libraries besides Qt.** PNG through LodePNG, JPEG/BMP through
stb_image, TIFF and FITS through small readers of our own. This keeps the
three-platform build simple (no vcpkg). The TIFF reader covers what stacking
programs write; it does not read BigTIFF, JPEG-in-TIFF or palette images.

**Ephemeris: VSOP87A, "small" truncation.** Public domain, self-contained,
and within 1.4″ of the full theory for 1950 to 2050, where 20″ would still be
enough. A JPL kernel would add a large data file for no gain.

**Rotation models: IAU 2015.** For Mars the long-period term of the 2015
model is included (without it the central meridian is off by 0.6°); the
remaining periodic terms of Mars and Jupiter are below 0.003° and left out.
Checked against Horizons rather than trusted from memory.

**Jupiter Systems I and II** use the Explanatory Supplement values
(W = 67.1° + 877.900°/d and 43.3° + 870.270°/d). Meeus's formulas give a
difference between the two systems that is 0.05° larger. Which one WinJUPOS
uses is not known to me; see open questions.

**Geometry is orthographic** and the observer is at the centre of the Earth.
Perspective and parallax are below 0.01 px for any amateur image.

**Derotation works on surface points, not on a 2-D warp.** Each output pixel
is traced to the planet and back into each input image. Changes of tilt and
of the direction of the Sun during the session are then handled for free.

**Weights instead of a hard cut at the limb.** Samples count in proportion
to how squarely the surface was seen and lit, and not at all in the blurred
outermost 1 %. Where no image has usable data the plain average takes over.
This is what avoids the bright or dark rim that derotation is known for.

**Limb darkening is measured, not assumed.** A linear law fitted along the
equator of each image; the median over the images is used. On the synthetic
planets (brightness ∝ μ₀^0.6) it finds k = 0.65 to 0.74, and using it cuts
the limb-zone error by a factor of two to three compared with no
compensation.

**Fine alignment is translation only**, against the reference image and then
against the combination. The comparison uses brightness divided by its local
mean inside the inner disc; a first version that subtracted the local mean
over a larger area was biased by 0.2 px over eight minutes because the
shading of the disc does not rotate with the features.

**Radius and north angle are evened out over the session** (median radius,
robust line through the angles). Per-image noise in the north angle of 0.15°
is 0.5 px at the limb of a 200 px disc, and the alignment, which only
shifts, cannot correct a turn.

**North/south from motion.** Added after the first off-screen run of the
desktop application, where a south-up test set measured with the default
"north up" hint produced a smeared result. The automatic measurement now
corrects this, and `derotate()` warns about it when it is called with
measurements made by hand.

**Numbers in files do not follow the system locale.** A Belgian or German
locale writes decimals with a comma; session files, FITS headers and
command-line values are parsed and written with the classic locale
(`strutil.h`). `tests/test_json_session.cpp` switches locales to check this.

**Damaged files must not crash or allocate wildly.** Image sizes are checked
against the file size before allocating. Found by the test that feeds
truncated and bit-flipped fixtures to the readers (a corrupted size field
asked for gigabytes).

**Independent review before release.** A separate review pass read the code
for portability and bugs, cross-compiled the core for Windows with MinGW,
built it against libc++, ran it under ThreadSanitizer and fed the file
readers about half a million mutated files. Fixed as a result: an
out-of-bounds read in the report when the planet was changed during a
derotation, a crash when a folder was given instead of an image, a missing
exception barrier around the worker thread, no size check in the BMP/JPEG
path, NaN samples in floating-point TIFFs, `inf`/`nan` accepted as numbers
by libc++, unchecked measurements in session files, wrong p/f labels for
Uranus, and three packaging problems in the release workflow (no MSVC
runtime in the Windows zip, no wildcard expansion on Windows, no re-signing
after macdeployqt).

## Known limits

Measurement:

- Radius about one pixel too small (edge found half a blur-width inside the
  limb); centre pulled up to 0.5 % of the radius towards the Sun away from
  opposition. Both are properties of looking for the steepest brightness
  drop. Remedy: fit the position of the steepest drop of a blurred,
  limb-darkened *model* disc instead of the geometric outline. This needs an
  estimate of the blur and of the limb darkening first.
- The north angle of Mars, Uranus and Neptune is not measured.
- Sharpening halos at the limb (wavelets, deconvolution) have not been
  simulated. They may move the detected edge outwards instead of inwards.
- The mirror setting cannot be detected and must be given.

Derotation:

- One rotation rate for the whole disc. A rate that depends on latitude
  (Jupiter's zonal wind profile) would sharpen long sessions.
- The alignment corrects position only, not orientation or scale.
- Ring shadow on Saturn's globe, moons and moon shadows are not masked.
- Mars: no special treatment of the terminator beyond the weights; clouds
  and limb hazes do not rotate with the surface.

Files and interface:

- No BigTIFF, no SER/AVI, no 32-bit PNG.
- FITS row order is assumed bottom-up when the header does not say.
- No application icon in the Windows executable or the macOS bundle yet
  (only the window icon); the macOS build is not signed or notarised.
- The interface is in English only; strings are wrapped in `tr()`.

## Open questions

1. **How does it do on real captures?** First thing to try: six to ten
   one-minute captures of Jupiter or Saturn, stacked in FTYS, through
   `fixmyplanet auto`, with `fixmyplanet overlay` to judge the measurement.
2. **Comparison with WinJUPOS** on the same images: central meridian values
   (expected to agree to 0.1°, possibly 0.05° apart in Systems I/II) and the
   derotated result.
3. **Leap seconds.** The table ends in 2017. If IERS announces a new one,
   `leapSeconds()` needs the entry (one second is 0.01° on Jupiter).
4. **Default limb weights** (0.15 to 0.40) were chosen on synthetic data with
   1 to 2 px of blur; real data may want the lower bound higher.

## Next steps, in rough order of value

1. Run on real data; fix what breaks.
2. First CI run on Windows and macOS.
3. Blink or difference view in the desktop application for checking a
   measurement against the neighbouring image.
4. Model-based limb fit (removes the two measurement biases).
5. Latitude-dependent rotation for Jupiter.
6. Cylindrical and polar map output; longitude and latitude under the cursor.
7. Derotation of video frames before stacking (in FTYS or here).

## Working on the code

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
ctest --test-dir build --output-on-failure

# with sanitizers, without the GUI
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFIXMYPLANET_SANITIZE=ON -DFIXMYPLANET_BUILD_GUI=OFF
cmake --build build-asan --parallel && ctest --test-dir build-asan --output-on-failure

# the desktop application without a display, for a screenshot
QT_QPA_PLATFORM=offscreen build/src/gui/fixmyplanet-gui --measure --derotate --screenshot shot.png images/*.tif
```

- `tests/make_fixtures.py` regenerates the image files in `tests/data` with
  tifffile, Pillow and astropy.
- The Horizons reference values in `tests/test_ephem.cpp` came from the
  Horizons API, geocentric observer, quantities 13, 14, 15, 17, 20 and 24.
- `fixmyplanet synth` and `src/core/synth.cpp` make the test planets. The renderer
  uses the same geometry code as the derotation, so the synthetic tests
  cannot catch an error common to both; the Horizons comparison and the
  direction-of-rotation tests in `tests/test_geometry.cpp` guard that side.
