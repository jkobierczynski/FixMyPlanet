# Third-party code

| Directory | What | Licence | Source |
|-----------|------|---------|--------|
| `lodepng/` | PNG reading and writing, zlib inflate | zlib licence | https://github.com/lvandeve/lodepng (commit e584a84) |
| `stb/` | `stb_image.h`, used for JPEG and BMP reading | MIT or public domain | https://github.com/nothings/stb (commit 2c980bb) |
| `vsop87/` | VSOP87A planetary theory, "small" truncation | public domain | https://github.com/gmiller123456/vsop87-multilang (commit 56354a6) |

The files are unmodified copies. VSOP87 itself is the work of P. Bretagnon and
G. Francou, Bureau des Longitudes, Paris (Astronomy & Astrophysics 202, 309,
1988).

The "small" truncation of VSOP87A differs from the "large" one by at most
1.4 arcseconds in the geocentric direction of any planet between 1950 and 2050
(0.1 arcsecond for Jupiter), which is far below what the physical ephemeris
needs.
