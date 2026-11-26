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
 * tests/rib/arealight_panel.rib tags two Polygon panels with one
 * AreaLightSource, the left wound to light the camera and the right wound
 * away. Under gmanpathtracer and under gmanraytracer alike the left panel
 * reads the light's intensity times its colour, the right reads black,
 * gman::emitters skips neither and no renderer warns that an area light has
 * no effect.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr std::uint32_t kResolution = 64;
constexpr double kIntensity = 0.8;
constexpr double kLightColor[3] = {1.0, 0.5, 0.25};
constexpr double kCameraDistance = 5.0;
constexpr double kPanelHalfHeight = 0.8;
constexpr double kLeftPanelXMin = -1.8;
constexpr double kLeftPanelXMax = -0.2;
constexpr double kRightPanelXMin = 0.2;
constexpr double kRightPanelXMax = 1.8;
constexpr int kEightBitSteps = 255;

struct PixelRange {
  std::uint32_t first;
  std::uint32_t last;
};

// The pixels one pixel clear of both edges of [low, high], a span at the
// panel's own depth, along an axis of the 64-pixel image whose half-width at
// that depth is halfWidth.
PixelRange insidePixels(double low, double high, double halfWidth) {
  double const scale = (double)kResolution / (2.0 * halfWidth);
  double const first = std::ceil((low + halfWidth) * scale) + 1.0;
  double const last = std::floor((high + halfWidth) * scale) - 2.0;
  return {(std::uint32_t)first, (std::uint32_t)last};
}

bool within(int actual, int expected, int tolerance) { return std::abs(actual - expected) <= tolerance; }

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <renderer-flag> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const rendererFlag = argv[2];
  std::string const ribDir = argv[3];

  std::string const tifPath = "arealight_panel.tif";
  std::remove(tifPath.c_str());

  GMANRunResult const run = runGman(gman, {"-r", rendererFlag, ribDir + "/arealight_panel.rib"});
  check(run.exitStatus == 0,
        "arealight_panel.rib renders under " + rendererFlag + " (exit " + std::to_string(run.exitStatus) + ")");
  check(run.output.find("ineligible or unsupported") == std::string::npos,
        rendererFlag + ": no area-light primitive is reported ineligible or unsupported");
  check(run.output.find("has no effect here") == std::string::npos,
        rendererFlag + ": no warning says an area light has no effect");

  GmanImage const image = readGmanTIFF(tifPath);
  check(image.ok && image.width == kResolution && image.height == kResolution,
        "arealight_panel.tif is written at 64 x 64");
  if (!image.ok || image.width != kResolution || image.height != kResolution) {
    return checkSummary("");
  }

  double const halfWidth = kCameraDistance * std::tan(std::numbers::pi / 8.0);
  PixelRange const rows = insidePixels(-kPanelHalfHeight, kPanelHalfHeight, halfWidth);
  PixelRange const left = insidePixels(kLeftPanelXMin, kLeftPanelXMax, halfWidth);
  PixelRange const right = insidePixels(kRightPanelXMin, kRightPanelXMax, halfWidth);

  int expected[3];
  for (int c = 0; c < 3; ++c) {
    expected[c] = (int)std::lround(kIntensity * kLightColor[c] * kEightBitSteps);
  }

  std::uint32_t litCount = 0, darkCount = 0;
  bool litExact = true, darkExact = true;
  for (std::uint32_t y = rows.first; y <= rows.last; ++y) {
    for (std::uint32_t x = left.first; x <= left.last; ++x) {
      std::uint32_t const pixel = image.at(x, y);
      ++litCount;
      litExact = litExact && within((int)TIFFGetR(pixel), expected[0], 1) &&
                 within((int)TIFFGetG(pixel), expected[1], 1) && within((int)TIFFGetB(pixel), expected[2], 1);
    }
    for (std::uint32_t x = right.first; x <= right.last; ++x) {
      std::uint32_t const pixel = image.at(x, y);
      ++darkCount;
      darkExact = darkExact && TIFFGetR(pixel) == 0 && TIFFGetG(pixel) == 0 && TIFFGetB(pixel) == 0;
    }
  }
  check(litCount >= 16, "at least 16 pixels lie squarely inside the left panel");
  check(darkCount >= 16, "at least 16 pixels lie squarely inside the right panel");
  check(litExact, "every pixel inside the left panel is within one 8-bit step of intensity x lightcolor");
  check(darkExact, "every pixel inside the right panel, wound away from the camera, is 0");

  return checkSummary(
      ("a Polygon under an AreaLightSource lights the camera along its winding's normal under " + rendererFlag)
          .c_str());
}
