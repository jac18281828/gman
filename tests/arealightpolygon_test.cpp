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
 * tests/rib/arealight_polygon.rib tags two polygons with one
 * AreaLightSource: one with a "P", one with only a "Cs". The z-buffer's
 * area-light count names only the one that reaches the object manager,
 * agreeing with the ray tracer, whose stub carries no appearance.
 */

#include <cstdio>
#include <string>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

std::size_t countOccurrences(std::string const& haystack, std::string const& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::string const tifPath = "arealight_polygon.tif";
  std::remove(tifPath.c_str());

  GMANRunResult const run = runGman(gman, {"-r", "gmanzbuffer", ribDir + "/arealight_polygon.rib"});
  check(run.exitStatus == 0, "arealight_polygon.rib renders (exit " + std::to_string(run.exitStatus) + ")");

  GmanImage const image = readGmanTIFF(tifPath);
  check(image.ok, "arealight_polygon.tif is written");

  check(countOccurrences(run.output, "an AreaLightSource has no effect here") == 1,
        "exactly one warning naming an ignored AreaLightSource");
  check(run.output.find("1 area-light primitive") != std::string::npos,
        "the warning names 1 area-light primitive, not the \"P\"-less polygon too");

  return checkSummary("the z-buffer's area-light count skips a polygon request with no \"P\"");
}
