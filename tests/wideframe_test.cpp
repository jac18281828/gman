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
 * tests/rib/wideframe.rib: a flat rectangle spanning most of a wide,
 * non-square frame under perspective. Format 800x200 (aspect 4) with no
 * RiScreenWindow puts the default window at [-4,4]x[-1,1];
 * fov=90 (tan(45deg)=1) and Translate 0 0 5 give ndc.x = x_cam/5, so the
 * rectangle's world x=+-19.5 vertices land at ndc.x=+-3.9 -- inside the
 * true screen window, far outside the canonical +-1 NDC cube.
 *
 * raster.x = 100*(ndc.x + 4): the rectangle's edges hand-compute to
 * columns 10 and 790, ten pixels shy of the frame's own edges (0 and
 * 799) by construction. raster.y = 100 - 100*ndc.y, with ndc.y = y_cam/5
 * and the rectangle's y=+-2 giving rows 60 and 140, so row 100 (frame
 * centre) is inside its span regardless of the x fix.
 *
 * A render still clipping perspective at the fixed +-1 cube instead of
 * the screen window keeps only ndc.x in [-1,1] -- raster columns
 * [300,500] -- leaving both edges background.
 *
 * Revert check: reverting the screen-window rebuild in
 * GMANPolygonClipper::clip (step 1) restores the fixed +-1 planes, and
 * both "reaches near column 0"/"reaches near column 799" assertions go
 * red -- the rectangle is clipped back to columns [300,500].
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"

namespace {

bool hasNonBackgroundPixel(const std::string &path, int xmin, int xmax,
                           int ymin, int ymax) {
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return false;
  }
  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(width * height);
  bool ok = TIFFReadRGBAImageOriented(tif, width, height, raster.data(),
                                      ORIENTATION_TOPLEFT, 0);
  bool found = false;
  if (ok) {
    const uint32_t bg = raster[0];
    const int tol = 8;
    for (int y = ymin; y <= ymax && !found; ++y) {
      for (int x = xmin; x <= xmax && !found; ++x) {
        if (x < 0 || y < 0 || uint32_t(x) >= width || uint32_t(y) >= height) {
          continue;
        }
        uint32_t p = raster[y * width + x];
        int dr = std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg)));
        int dg = std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg)));
        int db = std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg)));
        if (dr > tol || dg > tol || db > tol) {
          found = true;
        }
      }
    }
  }
  TIFFClose(tif);
  return found;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <wideframe.rib>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = argv[2];

  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "wideframe.rib renders");

  const char *tif = "wideframe.tif";

  // Row 100 (frame centre) sits inside the rectangle's raster row span
  // [60,140] regardless of the x-axis fix under test.
  const int y0 = 95, y1 = 105;

  // The rectangle's own edges hand-compute to columns 10 and 790, so a
  // window of the 20 nearest columns on each side of the frame catches
  // them with headroom for rasterization rounding, without reaching far
  // enough in to also catch a false pass from the old +-1 cube (whose
  // own edge sits at column 300, 280 columns further in).
  check(hasNonBackgroundPixel(tif, 0, 20, y0, y1),
        "the rectangle reaches within 20px of the left frame edge "
        "(hand-computed column 10)");
  check(hasNonBackgroundPixel(tif, 779, 799, y0, y1),
        "the rectangle reaches within 20px of the right frame edge "
        "(hand-computed column 790)");

  return checkSummary("wideframe holds");
}
