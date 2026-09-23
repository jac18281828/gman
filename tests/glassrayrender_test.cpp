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
 * R8 proof, §8 E.2: `gman -r gmanraytracer` renders tests/rib/r8_glass.rib
 * -- a glass sphere in front of a two-tone backdrop, its edge off the
 * sphere's own centre column -- matching a checked-in golden image. A
 * horizontal scanline through the sphere shows the backdrop's edge at a
 * raster x displaced from where the same edge falls outside the sphere's
 * silhouette, near the top of the frame, well clear of it.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(std::string const& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The first raster x in [xStart, xEnd) at which the scanline's dominant
// channel (red or blue -- the backdrop's own two Cs colours) flips, or -1
// if no flip happens in that range. Direction-agnostic: refraction
// through the sphere inverts the image left-right, so the flip runs
// blue-then-red inside the silhouette where it runs red-then-blue outside
// it.
int findColorTransition(GmanImage const& img, uint32_t y, uint32_t xStart, uint32_t xEnd) {
  bool havePrev = false;
  bool prevRed = false;
  for (uint32_t x = xStart; x < xEnd; ++x) {
    uint32_t const px = img.at(x, y);
    int const r = (int)TIFFGetR(px);
    int const b = (int)TIFFGetB(px);
    if (r == b) {
      continue; // neither dominant -- an edge-antialiased or sphere-rim pixel
    }
    bool const isRed = r > b;
    if (havePrev && isRed != prevRed) {
      return (int)x;
    }
    prevRed = isRed;
    havePrev = true;
  }
  return -1;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("r8_glass.tif");
  int const status = runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/r8_glass.rib\" >/dev/null 2>&1");
  check(status == 0, "r8_glass.rib renders under -r gmanraytracer (exit " + std::to_string(status) + ")");

  checkGoldenImage("r8_glass.tif", ribDir + "/r8_glass_golden.tif", GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "r8_glass_diff.tif");

  GmanImage const img = readGmanTIFF("r8_glass.tif");
  check(img.ok, "r8_glass.tif reads back");
  if (!img.ok) {
    return checkSummary("R8 E.2: a glass sphere bends a background edge");
  }

  // Near the top of the frame: clear of the sphere's own silhouette, the
  // backdrop's edge at its true, unrefracted raster x.
  int const outsideX = findColorTransition(img, 5, 0, img.width);
  check(outsideX >= 0, "the backdrop's edge is found outside the sphere's silhouette");

  // The sphere's own equator row, searched only in its central span --
  // comfortably inside the silhouette, clear of its own bright rim --
  // for the refracted edge.
  uint32_t const centreY = img.height / 2;
  int const insideX = findColorTransition(img, centreY, 60, 140);
  check(insideX >= 0, "the backdrop's edge is found inside the sphere's silhouette");

  if (outsideX >= 0 && insideX >= 0) {
    int const displacement = outsideX > insideX ? outsideX - insideX : insideX - outsideX;
    check(displacement >= 15, "the glass sphere bends the backdrop's edge: outside x=" + std::to_string(outsideX) +
                                  ", inside x=" + std::to_string(insideX) +
                                  ", displacement=" + std::to_string(displacement));
  }

  return checkSummary("R8 E.2: a glass sphere bends a background edge from its unrefracted raster position");
}
