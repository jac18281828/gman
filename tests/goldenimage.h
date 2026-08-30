/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * The one golden-image comparison path in this tree. Lifted from
 * tests/lighting_test.cpp (Phase 3), which read a TIFF and compared it per
 * channel against tests/rib/lights_golden.tif -- the only golden mechanism
 * that existed before this. Every scene needing pixel-regression protection
 * calls checkGoldenImage() below instead of writing a second comparison.
 *
 * Format: TIFF, matching the existing golden and every output driver this
 * renderer already exercises by default -- picking PNG instead would mean
 * converting a passing golden for no behavioral gain.
 *
 * Tolerance: per-channel, not exact match (RtFloat rasterization differs
 * slightly between compilers and platforms), plus a small allowed fraction
 * of mismatched pixels (a faceted tessellation boundary can shift by a
 * pixel between platforms at identical float precision -- a
 * tessellation-resolution artifact, not a shading regression;
 * silhouette_test.cpp already accepts an 8% geometric tolerance for the
 * same reason). 24/255 per channel, under 1% of pixels: the values Phase 3
 * picked for tests/rib/lights.rib, reused here as the tree-wide default.
 *
 * Regenerating a golden is legitimate only when the change that moved the
 * pixels is itself an intended, reviewed behavior change (a shading,
 * clipping or projection fix) -- never to make a red test green without
 * understanding why it moved. Regenerate by rendering the scene with a
 * known-good gman and copying the output over the checked-in golden, then
 * inspect the diff image (see below) from the old golden one last time
 * before overwriting it, and say so in the commit message.
 */

#ifndef GMAN_TESTS_GOLDENIMAGE_H
#define GMAN_TESTS_GOLDENIMAGE_H

#include <tiffio.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "check.h"

struct GmanImage {
  bool ok = false;
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> raster;

  uint32_t at(uint32_t x, uint32_t y) const {
    return raster[y * width + x];
  }
};

inline GmanImage readGmanTIFF(const std::string &path) {
  GmanImage img;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &img.width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &img.height);
  img.raster.resize(img.width * img.height);
  img.ok = TIFFReadRGBAImageOriented(tif, img.width, img.height,
                                     img.raster.data(), ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);
  return img;
}

// Red where a channel exceeds channelTol, the actual image's own grey
// luminance elsewhere -- so a failure shows where two images differ, not
// only that they do.
inline void writeGoldenDiffTIFF(const std::string &path,
                                const GmanImage &actual,
                                const GmanImage &golden, int channelTol) {
  if (!actual.ok || !golden.ok || actual.width != golden.width ||
      actual.height != golden.height) {
    return;
  }
  TIFF *tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, actual.width);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, actual.height);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

  std::vector<unsigned char> row(actual.width * 3);
  for (uint32_t y = 0; y < actual.height; ++y) {
    for (uint32_t x = 0; x < actual.width; ++x) {
      uint32_t a = actual.at(x, y);
      uint32_t g = golden.at(x, y);
      bool differs =
          std::abs(int(TIFFGetR(a)) - int(TIFFGetR(g))) > channelTol ||
          std::abs(int(TIFFGetG(a)) - int(TIFFGetG(g))) > channelTol ||
          std::abs(int(TIFFGetB(a)) - int(TIFFGetB(g))) > channelTol;
      if (differs) {
        row[x * 3 + 0] = 255;
        row[x * 3 + 1] = 0;
        row[x * 3 + 2] = 0;
      } else {
        unsigned char grey = (unsigned char)TIFFGetR(a);
        row[x * 3 + 0] = row[x * 3 + 1] = row[x * 3 + 2] = grey;
      }
    }
    TIFFWriteScanline(tif, row.data(), y, 0);
  }
  TIFFClose(tif);
}

// Reads actualPath and goldenPath, asserts matching dimensions and a
// per-channel comparison within tolerance, and -- on failure -- writes
// diffPath (in the current working directory, already the CMake test's own
// build-tree run directory) showing which pixels differed.
inline void checkGoldenImage(const std::string &actualPath,
                             const std::string &goldenPath, int channelTol,
                             double maxFraction, const std::string &diffPath) {
  GmanImage actual = readGmanTIFF(actualPath);
  check(actual.ok, "golden image: TIFF read back (" + actualPath + ")");
  if (!actual.ok) {
    return;
  }

  GmanImage golden = readGmanTIFF(goldenPath);
  check(golden.ok, "golden image: TIFF read back (" + goldenPath + ")");
  if (!golden.ok) {
    return;
  }

  check(actual.width == golden.width && actual.height == golden.height,
        "golden image: dimensions match (" + goldenPath + ")");
  if (actual.width != golden.width || actual.height != golden.height) {
    return;
  }

  long mismatched = 0;
  const long total = (long)actual.width * (long)actual.height;
  for (uint32_t y = 0; y < actual.height; ++y) {
    for (uint32_t x = 0; x < actual.width; ++x) {
      uint32_t a = actual.at(x, y);
      uint32_t g = golden.at(x, y);
      if (std::abs(int(TIFFGetR(a)) - int(TIFFGetR(g))) > channelTol ||
          std::abs(int(TIFFGetG(a)) - int(TIFFGetG(g))) > channelTol ||
          std::abs(int(TIFFGetB(a)) - int(TIFFGetB(g))) > channelTol) {
        ++mismatched;
      }
    }
  }
  double fraction = (double)mismatched / (double)total;
  bool passed = fraction < maxFraction;
  check(passed, "golden image: fewer than " +
                    std::to_string((int)(maxFraction * 100)) +
                    "% of pixels differ from " + goldenPath + " by more than " +
                    std::to_string(channelTol) + "/255 per channel (" +
                    std::to_string(mismatched) + "/" + std::to_string(total) +
                    " differed)");
  if (!passed) {
    writeGoldenDiffTIFF(diffPath, actual, golden, channelTol);
  }
}

#endif
