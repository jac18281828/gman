/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 1's actual gate: tests/rib/sphere.rib renders a sphere silhouette
 * whose raster position and size are analytically correct, and that
 * responds to RiProjection "fov". Runs the real gman binary end to end
 * (RIB parse, transform, projection, clip, rasterize) and reads the
 * produced TIFF back, rather than exercising the library classes directly
 * -- this is the one test that can't be fooled by a correct matrix wired
 * to the wrong call site.
 *
 * The silhouette of a sphere of radius r at distance d subtends an exact
 * half-angle asin(r/d) around the camera axis (the sightline from the
 * camera is tangent to the sphere at that angle -- not an approximation).
 * Projected, that half-angle becomes an NDC half-extent of
 * tan(asin(r/d)) / tan(fov/2), scaled to raster by the screen window and
 * format. All three RIBs here use r=1, d=5 (Translate 0 0 5, matching
 * sphere.rib), so asin(1/5) = 11.5370deg, tan = 0.204124.
 *
 * Tolerance: 16 px on a 200x200 render (8%). tests/rib/sphere.rib's own
 * 640x480 render (checked separately, looser tolerance for the same
 * reason) measured within 3% of this formula; the gap here is the sphere's
 * 16x16 quad tessellation approximating a circle with flat chords, which
 * always undershoots the true radius slightly. A tolerance this size would
 * not hide a wrong axis, a swapped sign, a doubled or halved scale, or a
 * dropped aspect/fov term -- the errors this test exists to catch.
 *
 * Revert check: reverting GMANRenderManImpl::RiWorldBegin's fov fix (step
 * 7, restoring the shadowed inner `RtFloat fov = param[0];`) pins FOV at
 * 90 regardless of what RiProjection asked for, so the "doubling fov
 * shrinks the silhouette" assertion goes red -- both renders come out the
 * same size.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path);
  out << contents;
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
// the top-left corner (the background, since nothing is drawn there in any
// of these scenes).
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

const char *kSceneTemplate =
    "Display \"%s\" \"file\" \"rgba\"\n"
    "Format 200 200 1\n"
    "Projection \"perspective\" \"fov\" [%g]\n"
    "Translate 0 0 5\n"
    "WorldBegin\n"
    "Translate %g 0 0\n"
    "Sphere 1 -1 1 360\n"
    "WorldEnd\n";

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  // asin(1/5) = 11.5370deg; tan = 0.204124.
  const double tanAngularRadius = std::tan(std::asin(0.2));

  // ---- base case: fov 45, no local translate ----
  const char *baseRib = "silhouette_base.rib";
  const char *baseTif = "silhouette_base.tif";
  char scene[1024];
  std::snprintf(scene, sizeof scene, kSceneTemplate, baseTif, 45.0, 0.0);
  writeFile(baseRib, scene);
  check(runGman(gman, baseRib) == 0, "base scene renders");

  BBox base = findSilhouette(baseTif);
  check(base.found, "base scene: a silhouette was found");

  const double halfFovBase = 45.0 / 2.0 * M_PI / 180.0;
  const double expectedHalfExtent =
      tanAngularRadius / std::tan(halfFovBase) * 100.0; // 200px / 2
  const double tol = 16.0;

  check(std::fabs(base.centerX() - 100.0) <= tol,
        "base scene: silhouette centered on x (hand-computed 100)");
  check(std::fabs(base.centerY() - 100.0) <= tol,
        "base scene: silhouette centered on y (hand-computed 100)");
  check(std::fabs(base.halfWidth() - expectedHalfExtent) <= tol,
        "base scene: silhouette half-width matches asin(r/d) hand-derivation");
  check(std::fabs(base.halfHeight() - expectedHalfExtent) <= tol,
        "base scene: silhouette half-height matches asin(r/d) hand-derivation");

  // ---- doubling fov shrinks the silhouette ----
  const char *doubledRib = "silhouette_doubled_fov.rib";
  const char *doubledTif = "silhouette_doubled_fov.tif";
  std::snprintf(scene, sizeof scene, kSceneTemplate, doubledTif, 90.0, 0.0);
  writeFile(doubledRib, scene);
  check(runGman(gman, doubledRib) == 0, "doubled-fov scene renders");

  BBox doubled = findSilhouette(doubledTif);
  check(doubled.found, "doubled-fov scene: a silhouette was found");
  check(doubled.halfWidth() < base.halfWidth(),
        "doubling fov shrinks the silhouette (half-width)");
  check(doubled.halfWidth() < base.halfWidth() * 0.6,
        "doubling fov shrinks the silhouette by roughly the hand-derived "
        "ratio (0.204/0.414 vs 1.0), not just marginally");

  // ---- a known RiTranslate moves the silhouette's centre ----
  // fov=90 -> tan(45deg)=1, so ndc.x = x_cam/z_cam = 0.5/5 = 0.1, and
  // raster.x = 100*(0.1 - -1) = 110: a +10px shift from the untranslated
  // centre (100).
  const char *translatedRib = "silhouette_translated.rib";
  const char *translatedTif = "silhouette_translated.tif";
  std::snprintf(scene, sizeof scene, kSceneTemplate, translatedTif, 90.0, 0.5);
  writeFile(translatedRib, scene);
  check(runGman(gman, translatedRib) == 0, "translated scene renders");

  BBox translated = findSilhouette(translatedTif);
  check(translated.found, "translated scene: a silhouette was found");
  check(std::fabs(translated.centerX() - 110.0) <= tol,
        "RiTranslate 0.5 0 0 moves the centre to the hand-computed x (110)");
  check(std::fabs(translated.centerY() - 100.0) <= tol,
        "RiTranslate 0.5 0 0 does not move the centre on y");

  return checkSummary("silhouette holds");
}
