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
 * `gman -r gmanpathtracer` renders tests/rib/pathtracer_samples.rib (a
 * matte unit sphere lit by one distant light from the eye's side) to a
 * non-black centre pixel and a black corner, and a second render of the
 * same fixture writes the same pixels: a stochastic image's bytes pin the
 * sampler, never the estimator, so this proves reproducibility in place of
 * a golden.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(std::string const& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const rib = ribDir + "/pathtracer_samples.rib";

  std::remove("pathtracer_samples.tif");
  check(runGman("\"" + gman + "\" -r gmanpathtracer \"" + rib + "\" >/dev/null 2>&1") == 0,
        "pathtracer_samples.rib renders under -r gmanpathtracer");

  GmanImage first = readGmanTIFF("pathtracer_samples.tif");
  check(first.ok, "the first render reads back");
  if (!first.ok) {
    return checkSummary("gman -r gmanpathtracer renders pathtracer_samples.rib");
  }

  uint32_t const centre = first.at(first.width / 2, first.height / 2);
  uint32_t const corner = first.at(0, 0);
  bool const centreNonBlack = TIFFGetR(centre) != 0 || TIFFGetG(centre) != 0 || TIFFGetB(centre) != 0;
  bool const cornerBlack = TIFFGetR(corner) == 0 && TIFFGetG(corner) == 0 && TIFFGetB(corner) == 0;
  check(centreNonBlack, "the centre pixel is non-black");
  check(cornerBlack, "the corner pixel is black");

  std::remove("pathtracer_samples.tif");
  check(runGman("\"" + gman + "\" -r gmanpathtracer \"" + rib + "\" >/dev/null 2>&1") == 0,
        "pathtracer_samples.rib renders a second time");
  check(std::rename("pathtracer_samples.tif", "pathtracer_samples_second.tif") == 0,
        "the second render's output renames to pathtracer_samples_second.tif");

  GmanImage second = readGmanTIFF("pathtracer_samples_second.tif");
  check(second.ok, "the second render reads back");
  check(first.width == second.width && first.height == second.height, "both renders share one Format");
  if (!second.ok || first.width != second.width || first.height != second.height) {
    return checkSummary("gman -r gmanpathtracer renders pathtracer_samples.rib");
  }

  bool everyPixelMatches = true;
  for (uint32_t y = 0; y < first.height; ++y) {
    for (uint32_t x = 0; x < first.width; ++x) {
      if (first.at(x, y) != second.at(x, y)) {
        everyPixelMatches = false;
      }
    }
  }
  check(everyPixelMatches, "a second render writes the same pixels as the first");

  return checkSummary("gman -r gmanpathtracer renders a matte sphere under a distant light, repeatably");
}
