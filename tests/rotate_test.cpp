/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * tests/rib/rotate.rib: an in-world Rotate must compose ahead of the
 * transforms declared after it, about the world origin, not about the
 * camera. GMANGraphicState::buildTransform folds a local transform in as
 * CTM_new = Local . CTM_old (row-vector convention); reverting that back to
 * CTM_new = CTM_old . Local instead orbits the primitive around the camera.
 *
 * Hand-derived from GMANMatrix4::rot's own matrix (dx,dy,dz)=(0,1,0),
 * angle=90deg maps (x,y,z) -> (z,y,-x), and GMANMatrix4::p3m's row-vector
 * product: the fixture's object-space origin, translated to (1.5,0,0),
 * rotated to (0,0,-1.5), then carried by the pre-WorldBegin
 * "Translate 0 0 5" to camera space (0,0,3.5) -- dead centre of frame,
 * d=3.5.
 *
 * Position alone is not enough: dropping the fixture's "Translate 1.5 0 0"
 * leaves a sphere still centred on the camera axis (the rotation of the
 * object-space origin is itself the origin), so a position-only test cannot
 * tell a working Rotate from a no-op one. Size closes that gap -- at
 * d=3.5, r=0.5, the silhouette half-angle is asin(0.5/3.5)=8.213deg;
 * projected at fov=45, raster half-extent is
 * tan(8.213deg)/tan(22.5deg)*100 = 34.8px. Without the Translate (d=5),
 * the same formula gives tan(asin(0.1))/tan(22.5deg)*100 = 24.3px. A 6px
 * tolerance separates the two with margin either side (34.8-6=28.8 clears
 * 24.3 by 4.5px) while still covering the 16x16 quad-tessellation
 * undershoot silhouette_test.cpp's own sphere measures, and is tight
 * enough that the wrong-distance case cannot pass by accident --
 * silhouette_test.cpp's own 16px figure is sized for its ~49px half-extent
 * and is too loose for this fixture's 34.8px one.
 *
 * Revert checks (see SPEC.md's ledger for the measurements this pins):
 * reverting GMANGraphicState::buildTransform back to CTM_new = CTM_old .
 * Local puts the sphere at camera-space (6.5,0,0), on the eye plane
 * (z=0) -- degenerate and off-screen, so no silhouette is found at all.
 * Neutralizing RiRotate instead (with the fix in place) leaves the sphere
 * translated but not rotated, moving its centre away from the raster
 * centre while its size stays close to the expected value -- the position
 * assertion catches that, not the size one.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct BBox {
  bool found = false;
  int xmin = 0, xmax = 0, ymin = 0, ymax = 0;

  double centerX() const { return (xmin + xmax) / 2.0; }
  double centerY() const { return (ymin + ymax) / 2.0; }
  double halfWidth() const { return (xmax - xmin) / 2.0; }
  double halfHeight() const { return (ymax - ymin) / 2.0; }
};

// Reads an RGBA TIFF and finds the bounding box of pixels that differ from
// the top-left corner (the background, since nothing is drawn there).
BBox findSilhouette(const std::string &path) {
  BBox box;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return box;
  }

  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(width * height);
  if (!TIFFReadRGBAImageOriented(tif, width, height, raster.data(),
                                  ORIENTATION_TOPLEFT, 0)) {
    TIFFClose(tif);
    return box;
  }

  const uint32_t bg = raster[0];
  const int tol = 8; // per-channel tolerance out of 255

  auto differs = [&](uint32_t p) {
    int dr = std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg)));
    int dg = std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg)));
    int db = std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg)));
    return dr > tol || dg > tol || db > tol;
  };

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      if (differs(raster[y * width + x])) {
        if (!box.found) {
          box.found = true;
          box.xmin = box.xmax = int(x);
          box.ymin = box.ymax = int(y);
        } else {
          box.xmin = std::min(box.xmin, int(x));
          box.xmax = std::max(box.xmax, int(x));
          box.ymin = std::min(box.ymin, int(y));
          box.ymax = std::max(box.ymax, int(y));
        }
      }
    }
  }

  TIFFClose(tif);
  return box;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rotate.rib>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = argv[2];

  check(runGman(gman, rib) == 0, "rotate.rib renders");

  const BBox box = findSilhouette("rotate.tif");
  check(box.found, "a silhouette was found");

  const double tol = 6.0;

  check(box.found && std::fabs(box.centerX() - 100.0) <= tol,
        "silhouette centred on x (hand-derived: Rotate then Translate "
        "carries the sphere to raster centre 100)");
  check(box.found && std::fabs(box.centerY() - 100.0) <= tol,
        "silhouette centred on y (hand-derived 100)");

  // asin(0.5/3.5) = 8.213deg; tan(8.213deg) = 0.144332.
  const double tanAngularRadius = std::tan(std::asin(0.5 / 3.5));
  const double halfFov = 45.0 / 2.0 * M_PI / 180.0;
  const double expectedHalfExtent =
      tanAngularRadius / std::tan(halfFov) * 100.0; // ~34.8px, 200px/2

  check(box.found && std::fabs(box.halfWidth() - expectedHalfExtent) <= tol,
        "silhouette half-width matches the d=3.5 hand-derivation (~34.8px), "
        "not the d=5 no-Translate figure (~24.3px)");
  check(box.found && std::fabs(box.halfHeight() - expectedHalfExtent) <= tol,
        "silhouette half-height matches the d=3.5 hand-derivation");

  return checkSummary("rotate holds");
}
