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
 * A ray renderer draws Patch and PatchMesh with the z-buffer's own coverage.
 * Each fixture named on the command line renders under the z-buffer and
 * under the renderer named, and the pixels with alpha above 0 in exactly one
 * image number at most a small fraction of the z-buffer's covered pixels. A
 * renderer that draws no patch fails every fixture. Under gmanraytracer, a
 * fixture lit by ambientlight alone with "Ka" 1 and "Kd" 0 also has one flat
 * colour per pixel, so every pixel opaque in both images agrees within a
 * small tolerance in each channel.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "rungman.h"

namespace {

constexpr double kMaxDifferingFraction = 0.02;
constexpr int kFlatColourTolerance = 2;
constexpr unsigned char kOpaque = 255;

std::set<std::string> const kFlatColourFixtures = {
    "patch_norender",    "patchmesh_bilinear",     "patchmesh_bilinear_periodic",
    "patchmesh_bicubic", "patchmesh_bspline_tube",
};

struct RawImage {
  bool ok = false;
  uint32_t width = 0;
  uint32_t height = 0;
  int samplesPerPixel = 0;
  std::vector<unsigned char> data;

  unsigned char sample(uint32_t x, uint32_t y, int channel) const {
    return data[((std::size_t)y * width + x) * (std::size_t)samplesPerPixel + (std::size_t)channel];
  }
  unsigned char alpha(uint32_t x, uint32_t y) const { return sample(x, y, 3); }
};

RawImage readRawTIFF(std::string const& path) {
  RawImage img;
  TIFF* tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  uint16_t samplesPerPixel = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);

  const tmsize_t scanlineSize = TIFFScanlineSize(tif);
  const std::size_t linebytes = (std::size_t)width * (std::size_t)samplesPerPixel;
  std::vector<unsigned char> scanline((std::size_t)scanlineSize);

  img.data.resize((std::size_t)height * linebytes);
  bool ok = samplesPerPixel >= 4;
  for (uint32_t y = 0; ok && y < height; ++y) {
    if (TIFFReadScanline(tif, scanline.data(), y) < 0) {
      ok = false;
      break;
    }
    std::copy(scanline.begin(), scanline.begin() + (std::ptrdiff_t)linebytes,
              img.data.begin() + (std::ptrdiff_t)((std::size_t)y * linebytes));
  }
  TIFFClose(tif);

  img.width = width;
  img.height = height;
  img.samplesPerPixel = samplesPerPixel;
  img.ok = ok;
  return img;
}

std::size_t countCovered(RawImage const& img) {
  std::size_t covered = 0;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      if (img.alpha(x, y) > 0) {
        ++covered;
      }
    }
  }
  return covered;
}

int render(std::string const& gman, std::string const& rib, std::string const& renderer) {
  return runGman(gman, {"-r", renderer, rib}).exitStatus;
}

bool renderTo(std::string const& gman, std::string const& rib, std::string const& renderer, std::string const& fixture,
              std::string const& output) {
  return render(gman, rib, renderer) == 0 && std::rename((fixture + ".tif").c_str(), output.c_str()) == 0;
}

std::size_t countFlatColourMismatches(RawImage const& a, RawImage const& b) {
  std::size_t mismatches = 0;
  for (uint32_t y = 0; y < a.height; ++y) {
    for (uint32_t x = 0; x < a.width; ++x) {
      if (a.alpha(x, y) != kOpaque || b.alpha(x, y) != kOpaque) {
        continue;
      }
      for (int channel = 0; channel < 3; ++channel) {
        if (std::abs(int(a.sample(x, y, channel)) - int(b.sample(x, y, channel))) > kFlatColourTolerance) {
          ++mismatches;
          break;
        }
      }
    }
  }
  return mismatches;
}

void checkFixture(std::string const& gman, std::string const& ribDir, std::string const& renderer,
                  std::string const& fixture) {
  const std::string rib = ribDir + "/" + fixture + ".rib";
  const std::string zbufferTif = fixture + "_zbuffer.tif";
  const std::string rayTif = fixture + "_" + renderer + ".tif";

  check(renderTo(gman, rib, "gmanzbuffer", fixture, zbufferTif), fixture + ": renders under gmanzbuffer");
  check(renderTo(gman, rib, renderer, fixture, rayTif), fixture + ": renders under " + renderer);

  RawImage const zImg = readRawTIFF(zbufferTif);
  RawImage const rImg = readRawTIFF(rayTif);
  check(zImg.ok && rImg.ok, fixture + ": both images read back");
  if (!zImg.ok || !rImg.ok) {
    return;
  }
  check(zImg.width == rImg.width && zImg.height == rImg.height, fixture + ": both images share one size");
  if (zImg.width != rImg.width || zImg.height != rImg.height) {
    return;
  }

  std::size_t const zCovered = countCovered(zImg);
  std::size_t differing = 0;
  for (uint32_t y = 0; y < zImg.height; ++y) {
    for (uint32_t x = 0; x < zImg.width; ++x) {
      if ((zImg.alpha(x, y) > 0) != (rImg.alpha(x, y) > 0)) {
        ++differing;
      }
    }
  }

  check(zCovered > 0, fixture + ": the z-buffer covers at least one pixel");
  double percent = 0.0;
  if (zCovered > 0) {
    percent = 100.0 * (double)differing / (double)zCovered;
  }
  std::printf("%s: z-buffer covered=%zu differing=%zu (%.2f%%)\n", fixture.c_str(), zCovered, differing, percent);
  check(differing <= (std::size_t)(kMaxDifferingFraction * (double)zCovered),
        fixture + ": " + renderer + "'s coverage matches the z-buffer's within " +
            std::to_string(kMaxDifferingFraction * 100.0) + "% of its own covered count");

  if (renderer != "gmanraytracer" || kFlatColourFixtures.count(fixture) == 0) {
    return;
  }
  std::size_t const mismatches = countFlatColourMismatches(zImg, rImg);
  std::printf("%s: %zu opaque pixels differ in colour\n", fixture.c_str(), mismatches);
  check(mismatches == 0, fixture + ": every pixel opaque in both images agrees within " +
                             std::to_string(kFlatColourTolerance) + " in each 8-bit channel");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir> <renderer> <fixture>...\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  const std::string renderer = argv[3];

  for (int i = 4; i < argc; ++i) {
    checkFixture(gman, ribDir, renderer, argv[i]);
  }

  return checkSummary("every patch fixture covers the z-buffer's pixels");
}
