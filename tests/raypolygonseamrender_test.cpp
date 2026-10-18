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
 * `gman -r gmanraytracer` renders tests/rib/raypolygonseam.rib, an open-front
 * box of five polygons whose ceiling and right wall share an edge on the
 * screen diagonal, and every pixel comes out opaque: a ray landing exactly on
 * a shared edge hits one of the two polygons that meet there. Reverting
 * GMANRayPolygon::intersect to the strict inside test leaves a hairline of
 * transparent pixels along that edge, failing this check.
 */

#include <cstdint>
#include <cstdio>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr uint32_t kImageSize = 200;
constexpr uint32_t kOpaque = 255;

uint32_t countBelowOpaque(GmanImage const& image) {
  uint32_t count = 0;
  for (uint32_t y = 0; y < image.height; ++y) {
    for (uint32_t x = 0; x < image.width; ++x) {
      if (TIFFGetA(image.at(x, y)) < kOpaque)
        ++count;
    }
  }
  return count;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("raypolygonseam.tif");

  int const status = runGman(gman, {"-r", "gmanraytracer", ribDir + "/raypolygonseam.rib"}).exitStatus;
  check(status == 0, "raypolygonseam.rib renders under -r gmanraytracer (exit " + std::to_string(status) + ")");

  GmanImage const image = readGmanTIFF("raypolygonseam.tif");
  check(image.ok, "raypolygonseam.tif reads back");
  if (!image.ok)
    return checkSummary("gman -r gmanraytracer renders an open-front box with no gap along a shared edge");

  check(image.width == kImageSize && image.height == kImageSize,
        "raypolygonseam.tif is " + std::to_string(kImageSize) + "x" + std::to_string(kImageSize) + " (got " +
            std::to_string(image.width) + "x" + std::to_string(image.height) + ")");

  uint32_t const transparent = countBelowOpaque(image);
  check(transparent == 0, std::to_string(transparent) + " pixels have alpha below 255 along the box's shared edges");

  return checkSummary("gman -r gmanraytracer renders an open-front box with no gap along a shared edge");
}
