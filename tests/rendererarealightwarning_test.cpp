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
 * The z-buffer and the ray tracer never call gman::emitters and never
 * special-case an AreaLightSource-tagged primitive's own shading: each
 * renders tests/rib/arealight_sphere.rib pixel for pixel as it would with
 * every AreaLightSource request stripped out, logging exactly one warning
 * naming the area-light primitive it found; the stripped scene alone logs
 * none. Each fixture and renderer combination runs its own gman process --
 * both loadable renderers keep their own object manager in one
 * process-lifetime static instance (see renderers/zbuffer/zbufferloader.cpp
 * and renderers/raytracer/rayloader.cpp), so two parses sharing one process
 * would carry the first one's own tally into the second.
 */

#include <cstdio>
#include <fstream>
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

// arealight_sphere.rib's own text with every AreaLightSource request's own
// line dropped: the same two coincident, identically shaded spheres, none
// of them tagged.
void writeStrippedFixture(std::string const& sourcePath, std::string const& destPath) {
  std::ifstream in(sourcePath);
  std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("AreaLightSource", 0) != 0) {
      out << line << "\n";
    }
  }
}

bool imagesIdentical(GmanImage const& a, GmanImage const& b) {
  if (!a.ok || !b.ok || a.width != b.width || a.height != b.height) {
    return false;
  }
  for (uint32_t y = 0; y < a.height; ++y) {
    for (uint32_t x = 0; x < a.width; ++x) {
      if (a.at(x, y) != b.at(x, y)) {
        return false;
      }
    }
  }
  return true;
}

struct RenderResult {
  GmanImage image;
  std::string output;
};

// Renders ribPath under -r rendererFlag, its own fresh gman process, its
// combined stdout and stderr captured, arealight_sphere.rib's own Display
// target read back from tifPath.
RenderResult renderAndRead(std::string const& gman, std::string const& rendererFlag, std::string const& ribPath,
                           std::string const& tifPath) {
  std::remove(tifPath.c_str());
  GMANRunResult const run = runGman(gman, {"-r", rendererFlag, ribPath});
  check(run.exitStatus == 0,
        ribPath + " renders under " + rendererFlag + " (exit " + std::to_string(run.exitStatus) + ")");
  return {readGmanTIFF(tifPath), run.output};
}

// One renderer's own two checks: the tagged and stripped fixtures render
// pixel for pixel alike, and only the tagged one logs the warning.
void testRenderer(std::string const& gman, std::string const& rendererFlag, std::string const& taggedRib,
                  std::string const& strippedRib) {
  // Both fixtures share one Display target, arealight_sphere.rib's own
  // "arealight_sphere.tif": read back immediately after each render, before
  // the next one overwrites it.
  std::string const tifPath = "arealight_sphere.tif";

  RenderResult const tagged = renderAndRead(gman, rendererFlag, taggedRib, tifPath);
  check(tagged.image.ok, rendererFlag + ": the tagged fixture's TIFF reads back");
  RenderResult const stripped = renderAndRead(gman, rendererFlag, strippedRib, tifPath);
  check(stripped.image.ok, rendererFlag + ": the stripped fixture's TIFF reads back");

  if (tagged.image.ok && stripped.image.ok) {
    check(imagesIdentical(tagged.image, stripped.image),
          rendererFlag + ": the tagged render is pixel for pixel identical to the stripped one");
  }

  check(countOccurrences(tagged.output, "1 area-light primitive") == 1,
        rendererFlag + ": the tagged fixture logs exactly one warning naming the one area-light primitive found");

  check(countOccurrences(stripped.output, "area-light primitive") == 0,
        rendererFlag + ": the stripped fixture, with no AreaLightSource at all, logs none");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <renderer-flag> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const rendererFlag = argv[2];
  std::string const ribDir = argv[3];

  std::string const taggedRib = ribDir + "/arealight_sphere.rib";
  std::string const strippedRib = "arealight_sphere_stripped.rib";
  writeStrippedFixture(taggedRib, strippedRib);

  testRenderer(gman, rendererFlag, taggedRib, strippedRib);

  return checkSummary(
      ("the " + rendererFlag + " renders an area-light primitive unchanged, warning once naming it").c_str());
}
