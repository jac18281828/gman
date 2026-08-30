/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 1, proof item 6 (secondary): tests/rib/transforms.rib renders the
 * same sphere at three RiTranslate offsets. This project has no
 * golden-image regression infrastructure yet (that is Phase 4's
 * deliverable per SPEC.md's phase map), so this checks the three
 * hand-computed silhouette positions directly instead of a checked-in
 * reference image.
 *
 * fov=90 (tan(45deg)=1), distance=5, format 300x100 (aspect 3, screen
 * window [-3,3]x[-1,1]): raster.x = 50*(x_cam/z_cam + 3), giving centres
 * at x=135 (Translate -1.5), x=150 (Translate 0) and x=165
 * (Translate 1.5), all at y=50.
 *
 * Revert check: reverting GMANTransform::apply to `return p;` (step 1)
 * collapses all three spheres onto the same untransformed geometry --
 * three distinct silhouettes become one, and the "three separated
 * clusters" assertion goes red.
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

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <transforms.rib>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = argv[2];

  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "transforms.rib renders");

  const char *tif = "transforms.tif";
  const int y0 = 40, y1 = 60; // centre row is y=50 for all three

  check(hasNonBackgroundPixel(tif, 120, 150, y0, y1),
        "a silhouette is present around x=135 (Translate -1.5 0 0)");
  check(hasNonBackgroundPixel(tif, 143, 157, y0, y1),
        "a silhouette is present around x=150 (Translate 0 0 0)");
  check(hasNonBackgroundPixel(tif, 150, 180, y0, y1),
        "a silhouette is present around x=165 (Translate 1.5 0 0)");

  // The gap between the left and right clusters should be empty
  // background -- three separated spheres, not one smear, and not the
  // "every offset collapses to the origin" symptom of a reverted step 1.
  check(!hasNonBackgroundPixel(tif, 0, 100, y0, y1),
        "background stays background to the left of the left sphere");
  check(!hasNonBackgroundPixel(tif, 200, 300, y0, y1),
        "background stays background to the right of the right sphere");

  return checkSummary("transforms holds");
}
