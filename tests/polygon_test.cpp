/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 5's actual gate: tests/rib/polygon.rib renders a convex pentagon
 * whose raster coverage matches an analytically-computed region, and whose
 * shaded color matches the ambient-only value the fixture's Cs/Ka/light
 * intensity predict.
 *
 * The fixture's camera (Translate 0 0 5, fov 90) puts every vertex at the
 * same camera-space depth (world z=0 maps to camera z=5), so its
 * perspective projection is exactly affine here: ndc = (x_cam, y_cam) / 5
 * (tan(45deg)=1), and raster = 100*(ndc.x+1), 100*(1-ndc.y) -- the
 * empirically-confirmed sign convention (+y_cam maps to a smaller raster
 * y, i.e. up the image). Hand-projecting the fixture's five "P" vertices
 * with that formula gives the pentagon's exact raster-space vertices below
 * -- not an approximation, since the projection is affine at this fixed
 * depth. The polygon is regular and centred on the world origin, so its
 * centroid maps exactly to raster (100, 100), independent of any rendering
 * detail: this is also the pixel the shading assertion samples, deep
 * inside the pentagon (the nearest edge is roughly 16px away).
 *
 * Coverage is checked by an exact point-in-polygon test (ray casting)
 * against those five vertices, shrunk 15% toward the centroid for the
 * "must be non-background" test and grown 15% for the "must be
 * background" test -- a tolerance band around each edge that absorbs
 * rasterization/antialiasing rounding without weakening what the test
 * actually proves: coverage that matches the analytic pentagon, not just
 * "something got drawn somewhere".
 *
 * Revert checks (verified by actually reverting, not asserted):
 *   - Revert step 3 (GMANPatchPolyObjectManager::getRSPolygon back to a
 *     bare create()): the fixture renders pure background -- every
 *     coverage and color assertion here goes red.
 *   - Revert step 1 alone (RiPolygonV's parameter-list sizing back to the
 *     literal 4, 4): a heap over-read in getRSPolygon reading "P" back out
 *     of a too-small allocation. See phase-5-REPORT.md for the valgrind
 *     evidence this revert demonstrates; not guaranteed to be observable
 *     as a wrong render or a crash on its own.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"

namespace {

struct Point {
  double x, y;
};

// The pentagon's exact raster-space vertices, hand-projected from
// tests/rib/polygon.rib's "P" as described above.
const Point kPentagon[5] = {
    {100.0, 80.0},
    {80.97887, 93.81966},
    {88.24429, 116.18034},
    {111.75571, 116.18034},
    {119.02113, 93.81966},
};
const Point kCentroid = {100.0, 100.0};

// Scales a polygon toward (factor<1) or away from (factor>1) a centre
// point -- the tolerance band a point-in-polygon test needs to absorb
// rasterization rounding at an edge without weakening what a point deep
// inside or outside the true edge proves.
std::vector<Point> scaled(const Point *poly, int n, const Point &centre,
                           double factor) {
  std::vector<Point> out(n);
  for (int i = 0; i < n; ++i) {
    out[i].x = centre.x + (poly[i].x - centre.x) * factor;
    out[i].y = centre.y + (poly[i].y - centre.y) * factor;
  }
  return out;
}

// Even-odd rule point-in-polygon test.
bool pointInPolygon(const std::vector<Point> &poly, double px, double py) {
  bool inside = false;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const Point &a = poly[i];
    const Point &b = poly[j];
    bool crosses = (a.y > py) != (b.y > py);
    if (crosses) {
      double xCross = a.x + (py - a.y) * (b.x - a.x) / (b.y - a.y);
      if (px < xCross) {
        inside = !inside;
      }
    }
  }
  return inside;
}

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct Image {
  bool ok = false;
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> raster;

  uint32_t at(int x, int y) const { return raster[y * width + x]; }
};

Image readTIFF(const std::string &path) {
  Image img;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &img.width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &img.height);
  img.raster.resize(img.width * img.height);
  img.ok = TIFFReadRGBAImageOriented(tif, img.width, img.height,
                                      img.raster.data(), ORIENTATION_TOPLEFT,
                                      0);
  TIFFClose(tif);
  return img;
}

bool differsFromBackground(uint32_t p, uint32_t bg, int tol) {
  int dr = std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg)));
  int dg = std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg)));
  int db = std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg)));
  return dr > tol || dg > tol || db > tol;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <polygon.rib>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = argv[2];

  check(runGman(gman, rib) == 0, "polygon.rib renders");

  Image img = readTIFF("polygon.tif");
  check(img.ok, "polygon.tif reads back");
  if (!img.ok) {
    return checkSummary("polygon holds");
  }

  const uint32_t bg = img.at(0, 0);
  const int colorTol = 8; // per-channel tolerance out of 255, this
                           // codebase's standard (silhouette_test.cpp,
                           // lighting_test.cpp)

  std::vector<Point> inner = scaled(kPentagon, 5, kCentroid, 0.85);
  std::vector<Point> outer = scaled(kPentagon, 5, kCentroid, 1.15);

  int insideChecked = 0, outsideChecked = 0;
  int insideFailures = 0, outsideFailures = 0;
  for (int y = 70; y <= 126; y += 2) {
    for (int x = 70; x <= 130; x += 2) {
      bool coveredPixel = differsFromBackground(img.at(x, y), bg, colorTol);
      if (pointInPolygon(inner, (double) x, (double) y)) {
        ++insideChecked;
        if (!coveredPixel) {
          ++insideFailures;
        }
      } else if (!pointInPolygon(outer, (double) x, (double) y)) {
        ++outsideChecked;
        if (coveredPixel) {
          ++outsideFailures;
        }
      }
      // Points between inner and outer straddle the true edge -- left
      // unchecked, the tolerance band this test relies on.
    }
  }

  check(insideChecked > 0, "sampled at least one point inside the pentagon");
  check(outsideChecked > 0, "sampled at least one point outside the pentagon");
  check(insideFailures == 0,
        "every sampled interior point is non-background (" +
            std::to_string(insideFailures) + "/" +
            std::to_string(insideChecked) + " failed)");
  check(outsideFailures == 0,
        "every sampled exterior point stays background (" +
            std::to_string(outsideFailures) + "/" +
            std::to_string(outsideChecked) + " failed)");

  // The shading assertion: an ambient-only light (intensity 0.3) and a
  // matte surface with Ka=1, Cs=(0.8, 0.4, 0.2) give
  // Ci = Cs * Os * Ka * ambient_intensity = (0.24, 0.12, 0.06) at every
  // interior pixel (GMANMatte::computeCi), independent of N/I/E since
  // diffuse() is 0 with no non-ambient light. (100, 100) is the pentagon's
  // exact centroid (see file comment), roughly 16px from the nearest edge.
  const int centreX = 100, centreY = 100;
  uint32_t centre = img.at(centreX, centreY);
  const double expectedR = 0.8 * 1.0 * 1.0 * 0.3 * 255.0;
  const double expectedG = 0.4 * 1.0 * 1.0 * 0.3 * 255.0;
  const double expectedB = 0.2 * 1.0 * 1.0 * 0.3 * 255.0;
  const double shadeTol = 8.0; // testMetalKaResponse's own tolerance

  check(std::fabs((double) TIFFGetR(centre) - expectedR) <= shadeTol,
        "interior pixel red matches Cs*Os*Ka*ambient_intensity (got " +
            std::to_string(TIFFGetR(centre)) + ", expected " +
            std::to_string(expectedR) + ")");
  check(std::fabs((double) TIFFGetG(centre) - expectedG) <= shadeTol,
        "interior pixel green matches Cs*Os*Ka*ambient_intensity (got " +
            std::to_string(TIFFGetG(centre)) + ", expected " +
            std::to_string(expectedG) + ")");
  check(std::fabs((double) TIFFGetB(centre) - expectedB) <= shadeTol,
        "interior pixel blue matches Cs*Os*Ka*ambient_intensity (got " +
            std::to_string(TIFFGetB(centre)) + ", expected " +
            std::to_string(expectedB) + ")");

  return checkSummary("polygon holds");
}
