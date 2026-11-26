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
 * The ray tracer's shadows pass glass and an opacity-mapped card as the path
 * tracer's do. A matte sphere lies under a clear glass slab whose two faces
 * each pass 1 - F of the light, and a green receiver lies behind a card
 * whose opacity map holds red 128 of 255. Each scene renders under both
 * renderers beside its twin without the slab or the card; at one block of
 * pixels in the shadow the ray tracer reads within a fixed tolerance of the
 * path tracer, and its ratio to its own unshadowed reading matches the
 * prediction: (1 - F)^2 for the slab, 1 - 128/255 for the card.
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr int kBlockHalfWidth = 2;
constexpr double kRendererTolerance = 2.0;
constexpr double kRatioTolerance = 0.01;
constexpr double kBareFloor = 64.0;

constexpr double kGlassIndex = 1.5;
constexpr int kMapRed = 128;
constexpr int kMapGreen = 255;
constexpr int kMapBlue = 0;
constexpr int kMapSideTexels = 2;
constexpr int kMapChannels = 3;
constexpr int kMapMaxValue = 255;

char const* const kMapName = "rayshadow_cutout_rgb.tif";

using Mean = std::array<double, 3>;

struct Scene {
  char const* label;
  char const* shadowed;
  char const* bare;
  uint32_t centreX;
  uint32_t centreY;
  std::array<bool, 3> lit;
  double predictedRatio;
};

bool writeCardMap() {
  TIFF* tif = TIFFOpen(kMapName, "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(kMapSideTexels));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(kMapSideTexels));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, kMapChannels);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  std::array<unsigned char, kMapSideTexels * kMapChannels> row{};
  for (int texel = 0; texel < kMapSideTexels; ++texel) {
    row[static_cast<std::size_t>(texel * kMapChannels)] = static_cast<unsigned char>(kMapRed);
    row[static_cast<std::size_t>(texel * kMapChannels + 1)] = static_cast<unsigned char>(kMapGreen);
    row[static_cast<std::size_t>(texel * kMapChannels + 2)] = static_cast<unsigned char>(kMapBlue);
  }
  bool ok = true;
  for (int y = 0; y < kMapSideTexels; ++y) {
    ok = ok && TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(y), 0) >= 0;
  }
  TIFFClose(tif);
  return ok;
}

Mean blockMean(GmanImage const& image, uint32_t centreX, uint32_t centreY) {
  Mean mean{};
  int count = 0;
  for (int dy = -kBlockHalfWidth; dy <= kBlockHalfWidth; ++dy) {
    for (int dx = -kBlockHalfWidth; dx <= kBlockHalfWidth; ++dx) {
      uint32_t const px = image.at(centreX + static_cast<uint32_t>(dx), centreY + static_cast<uint32_t>(dy));
      mean[0] += TIFFGetR(px);
      mean[1] += TIFFGetG(px);
      mean[2] += TIFFGetB(px);
      ++count;
    }
  }
  for (double& channel : mean) {
    channel /= count;
  }
  return mean;
}

std::string describe(Mean const& mean) {
  return "(" + std::to_string(mean[0]) + ", " + std::to_string(mean[1]) + ", " + std::to_string(mean[2]) + ")";
}

bool renderBlock(std::string const& gman, std::string const& ribDir, std::string const& renderer, char const* name,
                 Scene const& scene, Mean& mean) {
  std::string const tif = std::string(name) + ".tif";
  std::remove(tif.c_str());
  int const status = runGman(gman, {"-r", renderer, ribDir + "/" + name + ".rib"}).exitStatus;
  check(status == 0, renderer + " renders " + name);
  GmanImage const image = readGmanTIFF(tif);
  check(image.ok, renderer + " " + name + ": TIFF read back");
  if (!image.ok) {
    return false;
  }
  mean = blockMean(image, scene.centreX, scene.centreY);
  return true;
}

void checkScene(std::string const& gman, std::string const& ribDir, Scene const& scene) {
  Mean rayShadowed{};
  Mean rayBare{};
  Mean pathShadowed{};
  Mean pathBare{};
  bool const read = renderBlock(gman, ribDir, "gmanraytracer", scene.shadowed, scene, rayShadowed) &&
                    renderBlock(gman, ribDir, "gmanraytracer", scene.bare, scene, rayBare) &&
                    renderBlock(gman, ribDir, "gmanpathtracer", scene.shadowed, scene, pathShadowed) &&
                    renderBlock(gman, ribDir, "gmanpathtracer", scene.bare, scene, pathBare);
  if (!read) {
    return;
  }

  std::string const label = std::string(scene.label) + ": ";
  for (std::size_t channel = 0; channel < scene.lit.size(); ++channel) {
    std::string const name = label + "channel " + std::to_string(channel) + " ";
    check(std::abs(rayShadowed[channel] - pathShadowed[channel]) <= kRendererTolerance,
          name + "the ray tracer reads within " + std::to_string(kRendererTolerance) + " of the path tracer, " +
              describe(rayShadowed) + " against " + describe(pathShadowed));
    if (!scene.lit[channel]) {
      continue;
    }
    check(rayBare[channel] > kBareFloor && pathBare[channel] > kBareFloor,
          name + "setup: the unshadowed receiver is lit, " + describe(rayBare) + " and " + describe(pathBare));
    double const ratio = rayShadowed[channel] / rayBare[channel];
    check(std::abs(ratio - scene.predictedRatio) <= kRatioTolerance,
          name + "the ray tracer's ratio to its unshadowed reading is " + std::to_string(ratio) + ", predicted " +
              std::to_string(scene.predictedRatio));
  }
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  check(writeCardMap(), std::string(kMapName) + " writes");

  double const reflectance = (kGlassIndex - 1.0) / (kGlassIndex + 1.0);
  double const facePass = 1.0 - reflectance * reflectance;
  Scene const glass = {"glass slab", "rayshadow_glass",  "rayshadow_glass_bare", 160,
                       120,          {true, true, true}, facePass * facePass};
  Scene const cutout = {"cut-out card",
                        "rayshadow_cutout",
                        "rayshadow_cutout_bare",
                        87,
                        100,
                        {false, true, false},
                        1.0 - static_cast<double>(kMapRed) / kMapMaxValue};
  checkScene(gman, ribDir, glass);
  checkScene(gman, ribDir, cutout);

  return checkSummary("the ray tracer's shadows pass a glass slab and an opacity-mapped card as the path tracer's do");
}
