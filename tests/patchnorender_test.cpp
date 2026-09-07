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
 * Patch tessellation, both "bilinear" and "bicubic". SPEC.md Section 8
 * ("Found by running the corpus after phase 3 landed") tracked Patch as
 * rasterizing no pixels; this file pinned that shape until getRSPatch was
 * wired up (phase 6). tests/rib/patch_norender.rib renders a bilinear
 * Patch beside a control Sphere under identical lighting and shading --
 * the Sphere proves the renderer itself works, so a Patch failure here is
 * Patch-specific. tests/rib/patch_bicubic.rib renders a bicubic Patch
 * alone, under Sides 1 and a lit "Kd", so a transposed control-point
 * reading (a real defect this shape has shipped with) culls to nothing
 * instead of rendering.
 *
 * Reverting getRSPatch to its create()-only stub fails every check below.
 */

#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool regionHasContent(const GmanImage &img, uint32_t x0, uint32_t x1,
                      uint32_t y0, uint32_t y1) {
  const uint32_t bg = img.at(0, 0);
  for (uint32_t y = y0; y < y1; ++y) {
    for (uint32_t x = x0; x < x1; ++x) {
      uint32_t p = img.at(x, y);
      if (std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
          std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
          std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  {
    const std::string rib = ribDir + "/patch_norender.rib";
    check(runGman(gman, rib) == 0, "patch_norender.rib renders (exit 0)");

    GmanImage img = readGmanTIFF("patch_norender.tif");
    check(img.ok, "patch_norender.rib: TIFF read back");
    if (img.ok) {
      const uint32_t mid = img.width / 2;
      check(regionHasContent(img, 0, mid, 0, img.height),
            "bilinear Patch renders (left half)");
      check(regionHasContent(img, mid, img.width, 0, img.height),
            "control: the right-half Sphere renders");
    }
  }

  {
    const std::string rib = ribDir + "/patch_bicubic.rib";
    check(runGman(gman, rib) == 0, "patch_bicubic.rib renders (exit 0)");

    GmanImage img = readGmanTIFF("patch_bicubic.tif");
    check(img.ok, "patch_bicubic.rib: TIFF read back");
    if (img.ok) {
      // patch_bicubic.rib's control net puts the patch within the
      // center half of the frame; a generous margin around the known
      // extent tolerates tessellation differences across platforms
      // without accepting a stray corner pixel as "rendered".
      check(regionHasContent(img, img.width / 4, 3 * img.width / 4,
                             img.height / 4, 3 * img.height / 4),
            "bicubic Patch renders in the frame center");
    }
  }

  return checkSummary("Patch bilinear and bicubic both rasterize");
}
