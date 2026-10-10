#!/usr/bin/env python3
"""Regenerate the image files in tests/data.

They are written with independent libraries (tifffile, Pillow, astropy) so that
the readers in src/core are checked against someone else's writer. Every pixel
follows a formula, which tests/test_imageio.cpp repeats:

    8-bit : v = (x*7 + y*13 + c*29) % 251
    16-bit: v = (x*701 + y*1303 + c*2909) % 65521
    float : v = ((x*7 + y*13 + c*29) % 251) / 250.0

Needs: pip install numpy tifffile imagecodecs pillow astropy
"""
import os
import numpy as np
import tifffile
from PIL import Image
from astropy.io import fits

W, H = 37, 23
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
os.makedirs(out, exist_ok=True)


def pattern(channels, kind):
    y, x, c = np.mgrid[0:H, 0:W, 0:channels]
    if kind == "u8":
        return ((x * 7 + y * 13 + c * 29) % 251).astype(np.uint8)
    if kind == "u16":
        return ((x * 701 + y * 1303 + c * 2909) % 65521).astype(np.uint16)
    return (((x * 7 + y * 13 + c * 29) % 251) / 250.0).astype(np.float32)


def squeeze(a):
    return a[:, :, 0] if a.shape[2] == 1 else a


g8, g16, c8, c16 = pattern(1, "u8"), pattern(1, "u16"), pattern(3, "u8"), pattern(3, "u16")
f32 = pattern(1, "f32")

tifffile.imwrite(f"{out}/gray16_none.tif", squeeze(g16), photometric="minisblack")
tifffile.imwrite(f"{out}/gray16_lzw_pred.tif", squeeze(g16), photometric="minisblack", compression="lzw", predictor=True)
tifffile.imwrite(f"{out}/rgb16_deflate_pred.tif", c16, photometric="rgb", compression="zlib", predictor=True)
tifffile.imwrite(f"{out}/rgb8_lzw.tif", c8, photometric="rgb", compression="lzw")
tifffile.imwrite(f"{out}/rgb16_planar_be.tif", np.moveaxis(c16, 2, 0), photometric="rgb", planarconfig="separate", byteorder=">")
tifffile.imwrite(f"{out}/gray16_tiled_lzw.tif", squeeze(g16), photometric="minisblack", tile=(16, 16), compression="lzw")
tifffile.imwrite(f"{out}/gray8_packbits.tif", squeeze(g8), photometric="minisblack", compression="packbits")
tifffile.imwrite(f"{out}/gray16_strips.tif", squeeze(g16), photometric="minisblack", rowsperstrip=5, compression="lzw", predictor=True)
tifffile.imwrite(f"{out}/gray_f32.tif", squeeze(f32), photometric="minisblack")
tifffile.imwrite(f"{out}/rgba8.tif", np.concatenate([c8, np.full((H, W, 1), 255, np.uint8)], axis=2), photometric="rgb", extrasamples=["unassalpha"])

Image.fromarray(squeeze(g8)).save(f"{out}/gray8.png")
Image.fromarray(c8).save(f"{out}/rgb8.png")
Image.fromarray(squeeze(g16)).save(f"{out}/gray16.png")
Image.fromarray(c8).save(f"{out}/rgb8.bmp")

# FITS: first row at the bottom (the standard), so flip for writing.
hdu = fits.PrimaryHDU(np.flipud(squeeze(g16)))
hdu.header["DATE-OBS"] = "2026-01-10T22:03:30.5"
hdu.writeto(f"{out}/gray16.fits", overwrite=True)
hdu = fits.PrimaryHDU(np.flipud(squeeze(f32)))
hdu.writeto(f"{out}/gray_f32.fits", overwrite=True)
cube = np.moveaxis(c16, 2, 0)[:, ::-1, :]
fits.PrimaryHDU(cube).writeto(f"{out}/rgb16.fits", overwrite=True)
hdu = fits.PrimaryHDU(squeeze(g16))
hdu.header["ROWORDER"] = "TOP-DOWN"
hdu.writeto(f"{out}/gray16_topdown.fits", overwrite=True)
print("wrote", len(os.listdir(out)), "files to", out)
