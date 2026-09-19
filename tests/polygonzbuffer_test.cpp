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
 * R5a proof: tests/rib/r5a_polygon.rib, a single camera-facing polygon,
 * renders the same shape under both renderers. The two renderers place
 * samples differently (the z-buffer truncates to integer sample
 * coordinates, the tracer uses gman::sampleCentre), so edge pixels differ
 * by design. Two checks, over two different pixel sets, per §8.4:
 *
 *   1. A pixel whose 5x5 neighbourhood is covered in both renders sits
 *      well inside both silhouettes, past that edge disagreement, and
 *      has to match exactly (within GOLDEN_CHANNEL_TOL) -- this pins
 *      normal, appearance and lights away from any edge effect. At least
 *      100 such pixels exist.
 *   2. Every pixel in the whole frame -- coverage disagreements included,
 *      not only the interior set above -- where the two renders mismatch
 *      has to sit within 2px of the z-buffer's own covered/uncovered
 *      boundary, where a one-sample placement difference can plausibly
 *      flip a pixel's coverage or shift its antialiased blend. A mismatch
 *      anywhere else would mean the two renderers disagree on the shape
 *      itself, not merely on sample placement.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// A covered pixel is one that differs from its own image's corner
// (background) pixel by more than 8/255 on any channel -- the idiom
// tests/rayrender_test.cpp's coveredPixelCount already counts by.
bool covered(GmanImage const& img, uint32_t x, uint32_t y, uint32_t bg) {
  uint32_t const p = img.at(x, y);
  return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 || std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
         std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
}

std::vector<uint8_t> coveredGrid(GmanImage const& img) {
  uint32_t const bg = img.at(0, 0);
  std::vector<uint8_t> grid(img.width * img.height);
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      grid[y * img.width + x] = covered(img, x, y, bg) ? 1 : 0;
    }
  }
  return grid;
}

// True when every pixel in (x, y)'s 5x5 neighbourhood is covered --
// solidly inside the silhouette, not near its own edge.
bool neighbourhoodCovered(std::vector<uint8_t> const& grid, uint32_t w, uint32_t h, uint32_t x, uint32_t y) {
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      int const nx = (int)x + dx, ny = (int)y + dy;
      if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h || !grid[ny * w + nx]) {
        return false;
      }
    }
  }
  return true;
}

// A pixel whose covered flag differs from at least one of its four
// neighbours (an out-of-frame neighbour reads as uncovered): the
// z-buffer's own silhouette boundary.
std::vector<uint8_t> boundaryGrid(std::vector<uint8_t> const& covered, uint32_t w, uint32_t h) {
  std::vector<uint8_t> boundary(w * h, 0);
  int const dxs[4] = {-1, 1, 0, 0};
  int const dys[4] = {0, 0, -1, 1};
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      bool const c = covered[y * w + x];
      bool differs = false;
      for (int k = 0; k < 4 && !differs; ++k) {
        int const nx = (int)x + dxs[k], ny = (int)y + dys[k];
        bool const nc = (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) ? false : covered[ny * w + nx];
        differs = nc != c;
      }
      boundary[y * w + x] = differs ? 1 : 0;
    }
  }
  return boundary;
}

bool near2px(std::vector<uint8_t> const& boundary, uint32_t w, uint32_t h, uint32_t x, uint32_t y) {
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      int const nx = (int)x + dx, ny = (int)y + dy;
      if (nx >= 0 && ny >= 0 && nx < (int)w && ny < (int)h && boundary[ny * w + nx]) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const rib = ribDir + "/r5a_polygon.rib";

  std::remove("r5a_polygon_raytraced.tif");
  std::remove("r5a_polygon_zbuffer.tif");
  std::remove("r5a_polygon.tif");

  int const rayStatus = runGman("\"" + gman + "\" -r gmanraytracer \"" + rib + "\" >/dev/null 2>&1");
  check(rayStatus == 0, "r5a_polygon.rib renders under -r gmanraytracer (exit " + std::to_string(rayStatus) + ")");
  check(std::rename("r5a_polygon.tif", "r5a_polygon_raytraced.tif") == 0,
        "r5a_polygon.tif renders and renames to r5a_polygon_raytraced.tif");

  int const zStatus = runGman("\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1");
  check(zStatus == 0, "r5a_polygon.rib renders under the default z-buffer (exit " + std::to_string(zStatus) + ")");
  check(std::rename("r5a_polygon.tif", "r5a_polygon_zbuffer.tif") == 0,
        "r5a_polygon.tif renders and renames to r5a_polygon_zbuffer.tif");

  GmanImage raytraced = readGmanTIFF("r5a_polygon_raytraced.tif");
  GmanImage zbuffer = readGmanTIFF("r5a_polygon_zbuffer.tif");
  check(raytraced.ok && zbuffer.ok, "both renders read back");
  check(raytraced.width == zbuffer.width && raytraced.height == zbuffer.height, "both renders share one Format");
  if (!raytraced.ok || !zbuffer.ok || raytraced.width != zbuffer.width || raytraced.height != zbuffer.height) {
    return checkSummary("R5a: the ray tracer's polygon against the z-buffer's own");
  }

  uint32_t const w = raytraced.width, h = raytraced.height;
  std::vector<uint8_t> const covRay = coveredGrid(raytraced);
  std::vector<uint8_t> const covZ = coveredGrid(zbuffer);
  std::vector<uint8_t> const boundaryZ = boundaryGrid(covZ, w, h);

  auto mismatches = [&](uint32_t x, uint32_t y) {
    uint32_t const a = raytraced.at(x, y);
    uint32_t const b = zbuffer.at(x, y);
    return std::abs(int(TIFFGetR(a)) - int(TIFFGetR(b))) > GOLDEN_CHANNEL_TOL ||
           std::abs(int(TIFFGetG(a)) - int(TIFFGetG(b))) > GOLDEN_CHANNEL_TOL ||
           std::abs(int(TIFFGetB(a)) - int(TIFFGetB(b))) > GOLDEN_CHANNEL_TOL;
  };

  // 1. Interior: every 5x5-covered-in-both pixel must match exactly.
  long candidates = 0, interiorMismatched = 0;
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      if (!neighbourhoodCovered(covRay, w, h, x, y) || !neighbourhoodCovered(covZ, w, h, x, y)) {
        continue;
      }
      ++candidates;
      if (mismatches(x, y)) {
        ++interiorMismatched;
      }
    }
  }

  // 2. Whole frame: every mismatch anywhere, coverage disagreements
  // included, must sit within 2px of the z-buffer's own boundary.
  long totalMismatched = 0, mismatchedFarFromBoundary = 0;
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      if (!mismatches(x, y)) {
        continue;
      }
      ++totalMismatched;
      if (!near2px(boundaryZ, w, h, x, y)) {
        ++mismatchedFarFromBoundary;
      }
    }
  }
  std::printf("candidates=%ld interiorMismatched=%ld totalMismatched=%ld mismatchedFarFromBoundary=%ld\n", candidates,
              interiorMismatched, totalMismatched, mismatchedFarFromBoundary);

  check(candidates >= 100, "at least 100 pixels have their 5x5 neighbourhood covered in both renders (" +
                               std::to_string(candidates) + ")");
  check(interiorMismatched == 0, "every interior (5x5-covered-in-both) pixel matches within GOLDEN_CHANNEL_TOL (" +
                                     std::to_string(interiorMismatched) + " did not)");
  check(mismatchedFarFromBoundary == 0,
        "every mismatched pixel in the whole frame lies within 2px of the z-buffer's own covered/uncovered "
        "boundary (" +
            std::to_string(mismatchedFarFromBoundary) + " of " + std::to_string(totalMismatched) + " did not)");

  return checkSummary("R5a: the ray tracer's polygon matches the z-buffer's own, away from silhouette edges");
}
