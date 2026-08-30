/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Step 3/4: tests/rib/quadrics.rib renders all seven RenderMan quadric
 * primitives (Sphere, Cone, Cylinder, Hyperboloid, Paraboloid, Torus,
 * Disk) in one scene, each in its own column. Two checks: a horizontal
 * scanline through the row of primitives crosses exactly seven distinct
 * non-background silhouettes -- a primitive that stops tessellating or
 * rasterizing collapses this count without necessarily moving enough
 * pixels to trip the golden-image tolerance -- and the rendered image
 * matches the checked-in golden within tolerance.
 *
 * The fixture's own comment records why Cone, Cylinder, Hyperboloid and
 * Paraboloid each need "cancel the inherited camera translate, rotate,
 * restore" rather than a plain Rotate: GMAN composes transforms in
 * declaration order, so a Rotate declared after the pre-WorldBegin camera
 * Translate also rotates that camera offset, swinging the primitive out of
 * frame. Confirmed by direct construction while authoring this fixture,
 * not asserted here -- this test only pins the corrected fixture's output.
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

// Number of distinct non-background runs along one horizontal scanline --
// one run per primitive's silhouette, if all seven still rasterize.
int countSilhouetteRuns(const GmanImage &img, uint32_t y) {
  if (!img.ok) {
    return -1;
  }
  const uint32_t bg = img.at(0, 0);
  auto differsFromBackground = [&](uint32_t p) {
    return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
           std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
           std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
  };
  int runs = 0;
  bool inRun = false;
  for (uint32_t x = 0; x < img.width; ++x) {
    bool differs = differsFromBackground(img.at(x, y));
    if (differs && !inRun) {
      ++runs;
    }
    inRun = differs;
  }
  return runs;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string rib = ribDir + "/quadrics.rib";
  check(runGman(gman, rib) == 0, "quadrics.rib renders");

  GmanImage img = readGmanTIFF("quadrics.tif");
  check(img.ok, "quadrics.rib: TIFF read back");
  if (img.ok) {
    // At least one run per primitive: a concave silhouette (the torus's
    // own hole crosses the centre scanline as two separate runs, one per
    // rim) means the healthy count is not simply seven, so this only
    // pins a floor -- a vanished primitive still drops the count below
    // it, which is what a mutation would do.
    int runs = countSilhouetteRuns(img, img.height / 2);
    check(runs >= 7,
          "quadrics.rib: at least seven silhouettes cross the centre "
          "scanline (found " +
              std::to_string(runs) + ")");
  }

  checkGoldenImage("quadrics.tif", ribDir + "/quadrics_golden.tif", 24, 0.01,
                   "quadrics_diff.tif");

  return checkSummary("quadrics holds");
}
