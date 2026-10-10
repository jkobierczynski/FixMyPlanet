# How FixMyPlanet works

This note describes the calculations in `src/core` and how each part was
checked. File names refer to that directory.

## 1. Where the planet is and how it is turned (`ephem`, `planets`)

For a UTC time the program computes, for an observer at the centre of the
Earth:

- the heliocentric positions of the Earth and the planet from **VSOP87A**
  (the "small" truncation, rotated from the ecliptic to the equatorial J2000
  frame), with the planet taken at the moment its light left (light-time
  iteration). Terrestrial Time is UTC + 32.184 s + leap seconds (the table in
  `timeutil.cpp` ends with the leap second of 1 January 2017, the latest one
  known when this was written);
- the direction of the planet's north pole and the angle W of its prime
  meridian from the **IAU rotation models** (Archinal et al. 2018; for
  Jupiter and Saturn the linear terms, for Mars and Neptune also the main
  periodic term);
- from these, the body-frame direction of the Earth and of the Sun.

That gives the quantities an observer works with:

| Quantity | Meaning |
|----------|---------|
| central meridian (CM) | planetographic longitude of the centre of the disc |
| D_E | planetocentric declination of the Earth: the tilt of the axis towards us |
| P | position angle of the north pole, from celestial north (of date) through east |
| diameter, phase angle, distance | as usual |

Planetographic longitude increases to the west for all planets here except
Uranus, whose IAU north pole makes its rotation retrograde.

Jupiter has three longitude systems, which differ only in rotation rate and
zero point:

| System | Rate (°/day) | W at J2000 (°) | Follows |
|--------|--------------|----------------|---------|
| I | 877.900 | 67.1 | equatorial zone |
| II | 870.270 | 43.3 | the rest of the cloud deck, Great Red Spot |
| III | 870.536 | 284.95 | magnetic field (the IAU system) |

**Checks.** `tests/test_ephem.cpp` compares 13 dates between 1992 and 2030
with JPL Horizons (DE441). Largest differences found:

| Quantity | Largest difference |
|----------|--------------------|
| central meridian, System III / IAU | 0.005° |
| sub-Earth latitude | 0.002° |
| sub-solar longitude, latitude | 0.008°, 0.002° |
| position angle of the pole | 0.012° (Uranus), otherwise 0.005° |
| diameter | 0.00001″ |
| distance | 6 × 10⁻⁵ AU (Neptune), 2 × 10⁻⁶ AU for Jupiter |

Horizons prints apparent directions (with aberration) and FixMyPlanet astrometric
ones, which accounts for most of the longitude difference.

Systems I and II are not in Horizons. They are checked against the worked
example in Meeus, *Astronomical Algorithms* (1992 December 16, 0h UT), to
0.05°. Published definitions of these two systems agree with each other only
to about that much: the Explanatory Supplement gives the zero points to 0.1°.

## 2. From the planet to the pixels (`geometry`)

Three frames are used.

- **Body frame**: z along the rotation axis, x through the prime meridian of
  the chosen longitude system. The planet is the spheroid
  x² + y² + z²/c² = 1 with c = polar / equatorial radius.
- **View frame**: z towards the observer, y along the projected north pole,
  x = y × z. With north up, x points right, which is the direction in which
  the rotation carries the features. The projection is orthographic; the
  planets are far enough away for perspective not to matter.
- **Image**: pixel (u, v), u to the right and v down. One image is described
  by five numbers: the centre (cx, cy), the equatorial radius in pixels, the
  north angle ν (direction of the north pole, counter-clockwise from up) and
  a mirror flag:

  ```
  x' = (mirror ? -x : x)
  u  = cx + r (x' cos ν - y sin ν)
  v  = cy - r (x' sin ν + y cos ν)
  ```

The body-to-view rotation follows from the central meridian and D_E. A line
of sight through view point (x, y) meets the spheroid where a quadratic in z
has its larger root; the outward normal there gives μ, the cosine of the
angle between the surface and the line of sight (1 at the centre of the disc,
0 at the limb).

For Saturn the rings lie in the plane z_body = 0 between 1.239 and 2.269
equatorial radii.

## 3. Finding the planet in an image (`measure`)

1. The image is smoothed slightly (σ = 1.5 px). Background is the median of
   the image border; everything above 20 % of the way to the bright end is
   object, and the largest connected piece is the planet (moons drop out).
2. From the centroid 360 rays go outwards. On each, the **limb point** is
   where the brightness falls most steeply near the edge of the object,
   refined to a fraction of a pixel.
3. The **model outline** is the projected ellipse of the spheroid for the
   D_E of that time, cut by the terminator on the side away from the Sun, and
   extended by the outer edge of the A ring for Saturn (when the rings are
   open more than 3°).
4. Centre and radius (and for Saturn with open rings the north angle) follow
   from a robust least-squares fit of the outline to the limb points
   (Levenberg-Marquardt, Huber weights, points more than six scale units off
   ignored). Points on the terminator count for a tenth, because that edge is
   soft.
5. **North angle.**
   - Jupiter: the angle at which the disc is most strongly striped parallel
     to the assumed equator, searched within 60° of the given rough value.
   - Saturn: the long axis of the silhouette (rings, or the flattened globe
     when they are edge-on), refined by the outline fit when the rings are
     open.
   - Mars, Uranus, Neptune: not measured; the given value is kept.
6. Across a session the radius is replaced by the median of all images, and
   the north angle by a robust straight line against time (constant for an
   equatorial mount, drifting for an alt-azimuth one).
7. **North or south?** The outline is the same either way. The earliest and
   latest image are derotated to the time between them, once as measured and
   once with the north angle turned by 180°, and the surface detail (after
   removing the average along each parallel, which takes out the belts) is
   correlated. If the turned version matches clearly better, all north
   angles are turned.

   A mirror-reversed image with the opposite pole shows the same motion, so
   the mirror setting cannot be found this way and has to be given.

**Checks** (`tests/test_measure.cpp`), on rendered planets with blur of 1 to
2 px and noise of 0.5 to 1 %:

| Case | Centre error (px) | Radius error (px) | Angle error (°) |
|------|-------------------|-------------------|-----------------|
| Jupiter at opposition, radius 180 | 0.05 | −1.0 | 0.35 |
| Jupiter, south up, mirrored, radius 210 | 0.05 | −0.9 | 0.37 |
| Jupiter, phase 9°, radius 150 | 0.8 | −1.3 | 0.14 |
| Saturn, rings open, radius 105 | 0.01 | 0.0 | 0.00 |
| Saturn, rings nearly edge-on, phase 5°, radius 118 | 0.45 | −1.1 | 0.16 |
| Mars, phase 38°, radius 160 | 0.9 | −1.0 | not measured |
| Uranus, radius 40 | 0.07 | −0.9 | not measured |
| Neptune, radius 22 | 0.01 | −0.8 | not measured |

Two systematic effects are visible:

- **The radius comes out about one pixel small.** The steepest point of a
  blurred, limb-darkened edge lies about half a blur-width inside the true
  limb. Saturn with open rings is exempt because the ring edge is sharp.
- **Away from opposition the centre is pulled towards the Sun**, by up to
  about 0.5 % of the radius, because the terminator is found further inside
  than the limb.

The synthetic Jupiter has wavy belts, which is where its 0.35° angle error
comes from.

Both effects could be removed by predicting where the detector will find the
edge of a blurred model disc, instead of comparing with the geometric edge.
That is not done yet; see DEVELOPMENT.md.

## 4. Derotation (`derotate`)

The **reference time** is the mean of the image times unless given. The
output has the pixel grid of the image nearest to that time (or a centred,
north-up grid on request).

For every output pixel and every input image:

1. pixel → view frame → surface point on the spheroid as it was turned at
   the reference time;
2. the same surface point → view frame as the planet was turned when the
   input image was taken → pixel in the input image, sampled with bicubic
   interpolation (bilinear and Lanczos-3 are available);
3. a **weight** `m · smoothstep(0.15, 0.40, m)`, where m is the smaller of
   the cosines of the viewing and the illumination angle at that point in
   the input image. A point seen at a grazing angle, or in poor light, counts
   little; within the outermost 1 % of the radius, where the blurred limb
   contaminates the data, not at all.

The output pixel is the weighted mean over the images.

Further steps:

- **Limb darkening.** A feature is darker when it is nearer the limb.
  Brightness is modelled as `(1 − k) + k·μ₀` (μ₀ = cosine of the solar
  incidence angle) and each sample is rescaled from its own μ₀ to that at the
  reference time. k is measured on the images by default, from a band along
  the equator so that belts are not mistaken for limb darkening.
- **Brightness matching.** Each image is scaled so that the mean of its inner
  disc equals that of the image nearest the reference time (per filter).
- **Fine alignment.** Each image is derotated, and its surface detail
  compared with that of the reference image inside the inner disc
  (brightness divided by its local mean, so that shading drops out). One
  Gauss-Newton step gives the remaining shift; the image centre is corrected
  and the step repeated, coarse to fine, until it is below 0.01 px. A second
  pass compares each image with the combination of all the others.
- **Where the derotated data runs out** (the extreme limb), the result goes
  over smoothly into the plain average of the images, which are aligned but
  not derotated. The same plain average is used for everything outside the
  globe, and for Saturn's rings where they pass in front of it.
- **Filters.** Images are grouped by filter label and each group is combined
  on its own, all to the same reference time. Groups R, G and B become the
  channels of a colour image; anything else is written as one image per
  filter.

**Checks** (`tests/test_derotate.cpp`), on a rendered Jupiter of radius 140
px, seven images three minutes apart. Errors are RMS differences from the
planet rendered at the reference time, as a fraction of full brightness.

| Test | Result |
|------|--------|
| exact measurements, no noise: inner disc (ρ < 0.85) | 0.00024 |
| the same, limb zone (0.85 < ρ < 1.05) | 0.0040 |
| for comparison: an image 9 minutes early, not derotated | 0.033 |
| centres wrong by up to 2.5 px: error left after fine alignment | 0.024 px |
| noise 3 % per image: single image → seven derotated | 0.0225 → 0.0073 |
| R, G, B taken 2.5 min apart: derotated / stacked as they are | 0.0006 / 0.0117 |
| radius 1 px small and centre 0.7 px off in every image | 0.00023 |
| Saturn, five images over 20 minutes: globe / whole picture with rings | 0.0002 / 0.0014 |
| north and south swapped: detail correlation as given / turned | 0.99 / 0.26 |

These numbers say that the geometry and the bookkeeping are right. They say
nothing yet about real images, where the limiting factors will be the
measurement of sharpened, noisy discs and features that drift.

## 5. Files (`imageio`, `tiff`, `fits`, `session`)

- TIFF is read by the program's own reader (strips or tiles; no compression,
  LZW, Deflate or PackBits; horizontal predictor; 8/16/32-bit integers and
  32/64-bit floats; chunky or planar; either byte order) and written
  uncompressed. PNG goes through LodePNG, JPEG and BMP through stb_image.
  FITS is read and written directly; rows are taken bottom-up unless the
  header says `ROWORDER = 'TOP-DOWN'`.
- Integer samples are scaled to 0..1 on reading. Floating-point FITS data
  above 1 is taken to be in 16-bit units; floating-point TIFF data is used as
  it is.
- The readers are tested against files written by tifffile, Pillow and
  astropy (`tests/make_fixtures.py`), and against truncated and corrupted
  versions of those files.
- A session is a JSON file; numbers are read and written independently of
  the system's decimal separator.
