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
 * Step 3/5: tests/rib/clipping.rib -- a cylinder far longer than the
 * scene's [near,far] Clipping range, straddling both planes at once.
 * GMANPolygonClipper::clip must cut it to a finite visible band rather
 * than rendering the whole tube, a degenerate sliver, or crashing.
 *
 * Two checks: a non-background silhouette exists at all (a mutation that
 * broke either clip plane, or clipping generally, tends to either blank
 * the frame or leave it unbounded -- neither is a bounded, present
 * silhouette), and a golden-image comparison via the shared harness.
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

bool hasSilhouette(const GmanImage &img) {
  if (!img.ok) {
    return false;
  }
  const uint32_t bg = img.at(0, 0);
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
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

  const std::string rib = ribDir + "/clipping.rib";
  check(runGman(gman, rib) == 0, "clipping.rib renders");

  GmanImage img = readGmanTIFF("clipping.tif");
  check(img.ok, "clipping.rib: TIFF read back");
  if (img.ok) {
    check(hasSilhouette(img),
          "clipping.rib: a bounded, clipped silhouette is visible");
  }

  checkGoldenImage("clipping.tif", ribDir + "/clipping_golden.tif",
                   GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "clipping_diff.tif");

  return checkSummary("clipping holds");
}
