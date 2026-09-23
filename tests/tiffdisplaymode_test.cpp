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
 * gman::OutputTIFF::save against its DisplayMode contract: "rgb" writes
 * three samples and no ExtraSamples tag; "rgba" keeps four and tags the
 * fourth EXTRASAMPLE_ASSOCALPHA. Reads the produced files back with
 * libtiff directly (tests/ is exempt from AGENTS.md's one-includer rule
 * for tiffio.h).
 */

#include "check.h"
#include "gmanoutputtiff.h"
#include "ri.h"

extern "C" {
#include <tiffio.h>
}

namespace {

void checkRGBFile(const char* path) {
  gman::OutputTIFF output(path, 2, 2);
  output.save(GMANOutput::RGB, 1.0, 1.0);

  TIFF* tif = TIFFOpen(path, "r");
  check(tif != nullptr, "rgb: file opens");
  if (tif == nullptr) {
    return;
  }

  uint16_t samplesPerPixel = 0;
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  check(samplesPerPixel == 3, "rgb: SamplesPerPixel is 3");

  uint16_t photometric = 0;
  TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
  check(photometric == PHOTOMETRIC_RGB, "rgb: Photometric is RGB");

  uint16_t extraCount = 0;
  uint16_t* extraTypes = nullptr;
  const int hasExtra = TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes);
  check(!hasExtra, "rgb: no ExtraSamples tag");

  TIFFClose(tif);
}

void checkRGBAFile(const char* path) {
  gman::OutputTIFF output(path, 2, 2);
  output.save(GMANOutput::RGBA, 1.0, 1.0);

  TIFF* tif = TIFFOpen(path, "r");
  check(tif != nullptr, "rgba: file opens");
  if (tif == nullptr) {
    return;
  }

  uint16_t samplesPerPixel = 0;
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  check(samplesPerPixel == 4, "rgba: SamplesPerPixel is 4");

  uint16_t photometric = 0;
  TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
  check(photometric == PHOTOMETRIC_RGB, "rgba: Photometric is RGB");

  uint16_t extraCount = 0;
  uint16_t* extraTypes = nullptr;
  const int hasExtra = TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes);
  check(hasExtra != 0, "rgba: ExtraSamples tag is present");
  check(extraCount == 1, "rgba: ExtraSamples names exactly one sample");
  check(hasExtra != 0 && extraCount == 1 && extraTypes[0] == EXTRASAMPLE_ASSOCALPHA,
        "rgba: the one ExtraSamples value is EXTRASAMPLE_ASSOCALPHA");

  TIFFClose(tif);
}

} // namespace

int main() {
  checkRGBFile("rgb.tif");
  checkRGBAFile("rgba.tif");

  return checkSummary("tiffdisplaymode holds");
}
