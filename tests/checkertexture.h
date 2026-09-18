/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

/*
 * The one checker-texture writer in this tree. Lifted from
 * tests/texture_test.cpp, whose own quadrant derivation still documents the
 * checker's layout and the fixture camera every caller renders it through.
 * tests/texcoords_test.cpp needs the identical bytes texture_test.cpp
 * already proved, so both include this rather than keeping their own copy.
 */

#pragma once

#include <string>

#include <tiffio.h>

// Writes a 2x2 RGB TIFF, row 0 first -- ORIENTATION_TOPLEFT (set below)
// then makes that row the image's top row on read-back, matching
// gmanoutputtiff.cpp's own write order. A non-empty swrap also sets
// TIFFTAG_PIXAR_WRAPMODES to "<swrap>,<twrap>" -- the tag RiMakeTexture
// writes and gman::Texture reads back; the default leaves a plain, untagged
// checker.
inline bool writeCheckerTexture(const std::string& path, const std::string& swrap = "", const std::string& twrap = "") {
  TIFF* tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)2);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)2);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  if (!swrap.empty()) {
    const std::string wrapModes = swrap + "," + twrap;
    TIFFSetField(tif, TIFFTAG_PIXAR_WRAPMODES, wrapModes.c_str());
  }

  // Row 0 (top): red, green. Row 1 (bottom): blue, white.
  const unsigned char row0[6] = {255, 0, 0, 0, 255, 0};
  const unsigned char row1[6] = {0, 0, 255, 255, 255, 255};
  bool ok = TIFFWriteScanline(tif, (void*)row0, 0, 0) >= 0 && TIFFWriteScanline(tif, (void*)row1, 1, 0) >= 0;
  TIFFClose(tif);
  return ok;
}
