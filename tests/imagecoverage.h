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
 * Coverage checks over the image a tests/rib/malformed fixture renders. Every
 * such fixture draws its request under test, then a trailing sphere well to
 * the right, so counting covered pixels on each side of a column tells a
 * primitive that degraded and still drew from one that was dropped.
 *
 * Coverage is the count of pixels with alpha above 0, so a shading change
 * leaves it alone.
 */

#pragma once

#include <cstdint>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

// Every fixture renders at Format 200 200.
constexpr uint32_t malformedImageWidth = 200;

// The request under test ends by column 161 and the trailing sphere, at
// Translate 2 0 0, starts at column 170. Column 166 sits four columns clear of
// each.
constexpr uint32_t malformedRequestEndColumn = 166;

// The trailing sphere covers 1180 pixels in every fixture.
constexpr long malformedTrailingSphereFloor = 500;

// Fixtures write the image here; the working directory is the test's own.
constexpr char const* malformedImagePath = "malformed_out.tif";

// Counts the pixels in columns [firstColumn, endColumn), every row, whose
// alpha exceeds 0. endColumn clamps to the image width; a failed read counts
// 0.
inline long coveredPixels(GmanImage const& image, uint32_t firstColumn, uint32_t endColumn) {
  if (!image.ok) {
    return 0;
  }
  if (endColumn > image.width) {
    endColumn = image.width;
  }
  long covered = 0;
  for (uint32_t y = 0; y < image.height; ++y) {
    for (uint32_t x = firstColumn; x < endColumn; ++x) {
      if (TIFFGetA(image.at(x, y)) > 0) {
        ++covered;
      }
    }
  }
  return covered;
}

// Reads the fixture's image and checks that the request under test covers at
// least requestFloor pixels and the trailing sphere at least
// malformedTrailingSphereFloor. A requestFloor of 0 skips the first check, for
// a request that draws nothing.
inline void checkDegradedRender(std::string const& fixture, long requestFloor) {
  GmanImage const image = readGmanTIFF(malformedImagePath);
  check(image.ok, fixture + ": the rendered TIFF reads back");
  if (requestFloor > 0) {
    long const request = coveredPixels(image, 0, malformedRequestEndColumn);
    check(request >= requestFloor, fixture + ": the degraded request still draws (" + std::to_string(request) +
                                       " covered pixels, at least " + std::to_string(requestFloor) + ")");
  }
  long const sphere = coveredPixels(image, malformedRequestEndColumn, malformedImageWidth);
  check(sphere >= malformedTrailingSphereFloor, fixture + ": the scene completes past the request (" +
                                                    std::to_string(sphere) + " trailing-sphere pixels, at least " +
                                                    std::to_string(malformedTrailingSphereFloor) + ")");
}
