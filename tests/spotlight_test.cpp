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
 * tests/rib/spotlight.rib: a spotlight on a flat disk's own axis, so
 * radius from the disk's centre maps directly to the cone's angle. Proves
 * three of the RISpec's own claims about GMAN_LIGHT_SPOT: a point inside
 * the cone is lit, a point past coneangle is not (exactly, not just
 * dimmer), and the boundary between them is a taper spanning several
 * pixels, not a single-pixel step -- the case a missing conedeltaangle
 * clamp gets wrong.
 *
 * Revert check (verified by actually reverting, not asserted): removing
 * the "spotlight" branch in GMANRenderManImpl::RiLightSourceV makes the
 * light fall through to "Unknown light shader", RiLightSourceV returns
 * handle 0, and no light reaches the scene at all -- testInsideLit's own
 * brightness assertion goes red because the disk renders solid black.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct Image {
  bool ok = false;
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> raster;

  uint32_t at(uint32_t x, uint32_t y) const { return raster[y * width + x]; }
};

Image readTIFF(const std::string &path) {
  Image img;
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

// The disk is matte, lit only by the spotlight (no ambient term), so its
// channels track the light's own greyscale falloff directly.
int redAt(const Image &img, uint32_t x, uint32_t y) {
  return (int) TIFFGetR(img.at(x, y));
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = std::string(argv[2]) + "/spotlight.rib";

  check(runGman(gman, rib) == 0, "spotlight scene renders");

  Image img = readTIFF("spotlight.tif");
  check(img.ok, "spotlight scene: TIFF read back");
  if (!img.ok) {
    return checkSummary("spotlight holds");
  }

  // The disk's silhouette on the frame's centre row: background (white)
  // outside it, the light's own falloff inside.
  const uint32_t midY = img.height / 2;
  const uint32_t bg = img.at(0, midY);
  auto differsFromBackground = [&](uint32_t x) {
    uint32_t p = img.at(x, midY);
    return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
           std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
           std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
  };

  uint32_t silXmin = img.width, silXmax = 0;
  bool silFound = false;
  for (uint32_t x = 0; x < img.width; ++x) {
    if (differsFromBackground(x)) {
      silFound = true;
      silXmin = std::min(silXmin, x);
      silXmax = std::max(silXmax, x);
    }
  }
  check(silFound, "spotlight: the disk's silhouette was found");
  if (!silFound) {
    return checkSummary("spotlight holds");
  }

  const uint32_t silCentreX = (silXmin + silXmax) / 2;

  // ---- inside the cone: lit ----
  const int centreBrightness = redAt(img, silCentreX, midY);
  check(centreBrightness > 200,
        "spotlight: the disk's centre, well inside the cone, is lit "
        "(R=" + std::to_string(centreBrightness) + ")");

  // ---- outside the cone: exactly unlit, not just dim ----
  // A few pixels in from the silhouette's own edge, past the pixel
  // filter's antialiased rim, but still well outside the cone's small
  // half-angle at this disk's radius.
  const uint32_t margin = 4;
  check(silXmax - silXmin > 2 * margin,
        "spotlight: the silhouette is wide enough to sample past its rim");
  const int outsideNear = redAt(img, silXmin + margin, midY);
  const int outsideFar = redAt(img, silXmax - margin, midY);
  check(outsideNear <= 2 && outsideFar <= 2,
        "spotlight: past the cone, the disk reads exactly unlit on both "
        "sides (R=" + std::to_string(outsideNear) + "," +
        std::to_string(outsideFar) + ")");

  // ---- the taper: a band, not a step ----
  // Walking outward from the centre, count consecutive samples strictly
  // between "unlit" and "fully lit" -- a hard cutoff with no
  // conedeltaangle taper would skip straight from >200 to <=2 in one
  // pixel; a real taper spans several.
  auto taperWidth = [&](int step) -> int {
    int width = 0;
    for (uint32_t x = silCentreX; x >= silXmin + 1 && x + 1 <= silXmax; x += step) {
      int v = redAt(img, x, midY);
      if (v > 2 && v <= 200) {
        ++width;
      } else if (v <= 2) {
        break;
      }
    }
    return width;
  };
  const int rightTaper = taperWidth(1);
  const int leftTaper = taperWidth(-1);
  check(rightTaper >= 5 && leftTaper >= 5,
        "spotlight: the cone's edge tapers over several pixels on both "
        "sides, not a single-pixel step (right=" +
        std::to_string(rightTaper) + ", left=" + std::to_string(leftTaper) +
        ")");

  return checkSummary("spotlight holds");
}
