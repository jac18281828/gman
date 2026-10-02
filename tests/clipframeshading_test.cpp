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
 * A wall that crosses the frame edge renders the colour its unclipped
 * pixels have: a vertex the clipper adds at the crossing takes the colour
 * interpolated along the edge. One scene renders twice at 200 pixels per
 * unit. tests/rib/clipframe_narrow.rib is 640x400 with the default window
 * [-1.6,1.6]x[-1,1]; tests/rib/clipframe_wide.rib is 960x600 with
 * ScreenWindow -2.4 2.4 -1.5 1.5. The wide window extends the narrow one by
 * 0.8 units left and 0.5 units up, which at 200 pixels per unit is 160
 * columns and 100 rows, so narrow pixel (x, y) is wide pixel (x + 160,
 * y + 100).
 *
 * The wall clips at narrow column 0 and at wide column 0, which is narrow
 * column -160. Over narrow columns 0-119 and rows 100-299 the wide frame's
 * pixels do not depend on a clip vertex's colour, so it is the reference.
 * A clip vertex coloured by its end vertex instead of by t lays wedges of
 * wrong colour along the narrow frame's edge and the frames part. Other
 * rows carry differences from other frame-edge clipping and stay outside
 * the region.
 *
 * The tolerance sits between the largest difference at the fix (8, the
 * Gouraud residual of a clipped triangle against an unclipped one) and the
 * largest unfixed (80, with about 8,900 of the region's 24,000 pixels
 * beyond 8).
 *
 * Revert check: weight GMANClipEdge::intersect's colour blend by
 * e.getAlpha() again and the frames-agree assertion goes red.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr int narrowWidth = 640;
constexpr int narrowHeight = 400;
constexpr int wideWidth = 960;
constexpr int wideHeight = 600;
constexpr int offsetX = 160;
constexpr int offsetY = 100;
constexpr int regionX0 = 0;
constexpr int regionX1 = 119;
constexpr int regionY0 = 100;
constexpr int regionY1 = 299;
constexpr int backgroundMargin = 8;
constexpr int agreeTolerance = 16;

int channelDifference(uint32_t a, uint32_t b) {
  return std::max({std::abs(int(TIFFGetR(a)) - int(TIFFGetR(b))), std::abs(int(TIFFGetG(a)) - int(TIFFGetG(b))),
                   std::abs(int(TIFFGetB(a)) - int(TIFFGetB(b)))});
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <clipframe_narrow.rib> <clipframe_wide.rib>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  check(runGman(gman, {argv[2]}).exitStatus == 0, "clipframe_narrow.rib renders");
  check(runGman(gman, {argv[3]}).exitStatus == 0, "clipframe_wide.rib renders");

  const GmanImage narrow = readGmanTIFF("clipframe_narrow.tif");
  const GmanImage wide = readGmanTIFF("clipframe_wide.tif");
  check(narrow.ok && narrow.width == narrowWidth && narrow.height == narrowHeight,
        "the narrow frame reads back at 640x400");
  check(wide.ok && wide.width == wideWidth && wide.height == wideHeight, "the wide frame reads back at 960x600");
  if (!narrow.ok || !wide.ok || narrow.width != narrowWidth || narrow.height != narrowHeight ||
      wide.width != wideWidth || wide.height != wideHeight) {
    return checkSummary("clipframeshading holds");
  }

  const uint32_t background = narrow.at(narrowWidth - 1, 0);
  int minFromBackground = 255;
  int largest = 0;
  for (int y = regionY0; y <= regionY1; ++y) {
    for (int x = regionX0; x <= regionX1; ++x) {
      const uint32_t n = narrow.at(x, y);
      minFromBackground = std::min(minFromBackground, channelDifference(n, background));
      largest = std::max(largest, channelDifference(n, wide.at(x + offsetX, y + offsetY)));
    }
  }

  check(minFromBackground > backgroundMargin,
        "every compared narrow pixel differs from the background by more than 8 (least " +
            std::to_string(minFromBackground) + ")");
  check(largest <= agreeTolerance, "narrow columns 0-119, rows 100-299 agree with the wide frame within 16 levels "
                                   "(largest " +
                                       std::to_string(largest) + ")");

  return checkSummary("clipframeshading holds");
}
