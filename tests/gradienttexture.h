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
 * A texture whose sampled colour is affine in (s, t): red rises linearly
 * with column, green rises linearly with row, blue is constant --
 * regardless of which axis convention gman's own texture lookup treats as
 * s versus t. tests/polygonstparity_test.cpp needs this instead of
 * tests/checkertexture.h's checker: interpolating an affine function of
 * position commutes with evaluating it at the interpolated position, so
 * "blend the coordinate then shade" (ray tracer) and "shade then blend
 * the colour" (z-buffer) agree exactly. A checker's per-texel
 * discontinuity does not commute, so the two orders could disagree even
 * when both renderers are correct.
 */

#pragma once

#include <string>
#include <vector>

#include <tiffio.h>

// 64x64: fine enough that nearest-texel quantization (255/63 ~ 4 per step)
// stays well under GOLDEN_CHANNEL_TOL (tests/goldenimage.h), so a parity
// check does not fail on texel rounding rather than a real mismatch.
inline bool writeGradientTexture(const std::string& path) {
  const uint32_t size = 64;

  TIFF* tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, size);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, size);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);

  bool ok = true;
  std::vector<unsigned char> row(size * 3);
  for (uint32_t y = 0; y < size && ok; ++y) {
    const unsigned char green = (unsigned char)(255.0 * y / (size - 1));
    for (uint32_t x = 0; x < size; ++x) {
      const unsigned char red = (unsigned char)(255.0 * x / (size - 1));
      row[x * 3 + 0] = red;
      row[x * 3 + 1] = green;
      row[x * 3 + 2] = 128;
    }
    ok = TIFFWriteScanline(tif, row.data(), y, 0) >= 0;
  }
  TIFFClose(tif);
  return ok;
}
