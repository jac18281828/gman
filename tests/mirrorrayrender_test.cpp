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
 * R8 proof, §8 E.1: `gman -r gmanraytracer` renders tests/rib/r8_mirror.rib
 * -- a mirror sphere dead-on to the camera, reflecting a distinctly
 * coloured sphere placed behind the camera (invisible to every primary
 * ray) -- matching a checked-in golden image, and the same reflected
 * colour appears nowhere in tests/rib/r8_mirror_matte.rib's render, the
 * identical scene with the mirror sphere replaced by matte.
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

// The reflected sphere's own Color [0 1 0]: green well clear of red and
// blue, distinguishable from every other colour this scene's ambient and
// distant lighting produce (white, grey and the mirror sphere's own
// white base).
constexpr int kGreenMargin = 40;

bool greenDominant(uint32_t px) {
  int const r = (int)TIFFGetR(px);
  int const g = (int)TIFFGetG(px);
  int const b = (int)TIFFGetB(px);
  return (g - r) > kGreenMargin && (g - b) > kGreenMargin;
}

bool anyGreenDominant(GmanImage const& img) {
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      if (greenDominant(img.at(x, y))) {
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

  std::remove("r8_mirror.tif");
  int const status = runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/r8_mirror.rib\" >/dev/null 2>&1");
  check(status == 0, "r8_mirror.rib renders under -r gmanraytracer (exit " + std::to_string(status) + ")");

  checkGoldenImage("r8_mirror.tif", ribDir + "/r8_mirror_golden.tif", GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "r8_mirror_diff.tif");

  GmanImage const mirrorImg = readGmanTIFF("r8_mirror.tif");
  check(mirrorImg.ok, "r8_mirror.tif reads back");
  if (mirrorImg.ok) {
    check(anyGreenDominant(mirrorImg),
          "the reflected sphere's own colour appears somewhere within the mirror sphere's silhouette");
  }

  std::remove("r8_mirror_matte.tif");
  int const matteStatus =
      runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/r8_mirror_matte.rib\" >/dev/null 2>&1");
  check(matteStatus == 0,
        "r8_mirror_matte.rib renders under -r gmanraytracer (exit " + std::to_string(matteStatus) + ")");

  GmanImage const matteImg = readGmanTIFF("r8_mirror_matte.tif");
  check(matteImg.ok, "r8_mirror_matte.tif reads back");
  if (matteImg.ok) {
    check(!anyGreenDominant(matteImg),
          "the reflected sphere's own colour appears nowhere in the matte comparison's render");
  }

  return checkSummary("R8 E.1: a mirror sphere reflects a sphere placed behind the camera, invisible without it");
}
