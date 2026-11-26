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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "gmancolor.h"
#include "gmanmatrix4.h"
#include "gmanshading.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr int kBlockHalfWidth = 2;
constexpr uint32_t kRow = 100;
constexpr uint32_t kCutColumn = 87;
constexpr uint32_t kOpaqueColumn = 113;
constexpr uint32_t kShadowColumn = 87;

constexpr double kBleedCeiling = 8.0;
constexpr double kHueFloor = 64.0;

constexpr double kShadowTolerance = 6.0;
constexpr double kShadowMargin = 60.0;

bool writeOpacityMap(std::string const& path, unsigned char left, unsigned char right) {
  TIFF* tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)2);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)2);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  unsigned char row[2] = {left, right};
  bool const ok = TIFFWriteScanline(tif, row, 0, 0) >= 0 && TIFFWriteScanline(tif, row, 1, 0) >= 0;
  TIFFClose(tif);
  return ok;
}

struct Mean {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
};

Mean blockMean(GmanImage const& image, uint32_t centreX, uint32_t centreY) {
  Mean mean;
  int count = 0;
  for (int dy = -kBlockHalfWidth; dy <= kBlockHalfWidth; ++dy) {
    for (int dx = -kBlockHalfWidth; dx <= kBlockHalfWidth; ++dx) {
      uint32_t const px = image.at(centreX + static_cast<uint32_t>(dx), centreY + static_cast<uint32_t>(dy));
      mean.r += TIFFGetR(px);
      mean.g += TIFFGetG(px);
      mean.b += TIFFGetB(px);
      ++count;
    }
  }
  mean.r /= count;
  mean.g /= count;
  mean.b /= count;
  return mean;
}

std::string describe(Mean const& mean) {
  return "(" + std::to_string(mean.r) + ", " + std::to_string(mean.g) + ", " + std::to_string(mean.b) + ")";
}

GmanImage render(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                 std::string const& name) {
  std::remove((name + ".tif").c_str());
  int const status = runGman(gman, {"-r", renderer, ribDir + "/" + name + ".rib"}).exitStatus;
  check(status == 0, name + ": renders");
  GmanImage image = readGmanTIFF(name + ".tif");
  check(image.ok, name + ": TIFF read back");
  return image;
}

void checkRedCard(Mean const& mean, std::string const& what) {
  check(mean.r >= kHueFloor && mean.g <= kBleedCeiling, what + ": the card's red dominates, mean " + describe(mean));
}

void checkGreenBackdrop(Mean const& mean, std::string const& what) {
  check(mean.g >= kHueFloor && mean.r <= kBleedCeiling,
        what + ": the backdrop's green shows, red under " + std::to_string(kBleedCeiling) + ", mean " + describe(mean));
}

void testCutout(std::string const& gman, std::string const& renderer, std::string const& ribDir) {
  GmanImage const cutout = render(gman, renderer, ribDir, "textureopacity_cutout");
  if (cutout.ok) {
    checkGreenBackdrop(blockMean(cutout, kCutColumn, kRow), "cutout, map 0 half");
    checkRedCard(blockMean(cutout, kOpaqueColumn, kRow), "cutout, map 1 half");
  }
}

void testNoMap(std::string const& gman, std::string const& renderer, std::string const& ribDir) {
  GmanImage const nomap = render(gman, renderer, ribDir, "textureopacity_nomap");
  if (nomap.ok) {
    checkRedCard(blockMean(nomap, kCutColumn, kRow), "no map, left half");
    checkRedCard(blockMean(nomap, kOpaqueColumn, kRow), "no map, right half");
  }
}

void testShadow(std::string const& gman, std::string const& renderer, std::string const& ribDir) {
  GmanImage const cleared = render(gman, renderer, ribDir, "textureopacity_shadow");
  GmanImage const opaque = render(gman, renderer, ribDir, "textureopacity_shadow_nomap");
  GmanImage const bare = render(gman, renderer, ribDir, "textureopacity_shadow_nocard");
  if (!cleared.ok || !opaque.ok || !bare.ok) {
    return;
  }
  Mean const clearedMean = blockMean(cleared, kShadowColumn, kRow);
  Mean const opaqueMean = blockMean(opaque, kShadowColumn, kRow);
  Mean const bareMean = blockMean(bare, kShadowColumn, kRow);
  check(bareMean.g >= kHueFloor + kShadowMargin, "shadow: the unshadowed receiver is lit, mean " + describe(bareMean));
  check(std::abs(clearedMean.g - bareMean.g) <= kShadowTolerance,
        "shadow: a card whose map is 0 casts none, mean " + describe(clearedMean) + " against the receiver alone " +
            describe(bareMean) + ", tolerance " + std::to_string(kShadowTolerance));
  check(opaqueMean.g <= bareMean.g - kShadowMargin,
        "shadow: a card with no map shadows the receiver, mean " + describe(opaqueMean) +
            " against the receiver alone " + describe(bareMean) + ", margin " + std::to_string(kShadowMargin));
}

void testSeam() {
  gman::Appearance appearance;
  appearance.Os = GMANColor((RtFloat)0.25, (RtFloat)0.5, (RtFloat)0.75);
  GMANColor const got = gman::opacity(appearance, gman::SurfacePoint(), GMANMatrix4());
  check(got.getRed() == appearance.Os.getRed() && got.getGreen() == appearance.Os.getGreen() &&
            got.getBlue() == appearance.Os.getBlue(),
        "gman::opacity with a null shader answers Appearance::Os exactly");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const renderer = argv[3];

  check(writeOpacityMap("textureopacity_half.tif", 0, 255), "textureopacity_half.tif writes");
  check(writeOpacityMap("textureopacity_zero.tif", 0, 0), "textureopacity_zero.tif writes");

  testSeam();
  testCutout(gman, renderer, ribDir);
  testNoMap(gman, renderer, ribDir);
  testShadow(gman, renderer, ribDir);

  return checkSummary("paintedplastic's opacity map cuts out its surface; both renderers' shadows follow it");
}
