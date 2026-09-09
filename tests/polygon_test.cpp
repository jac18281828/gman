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
 * Phase 5's original gate: tests/rib/polygon.rib renders a convex pentagon
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
 * This task's own gate: a concave polygon's reflex notch renders as
 * background, not fill. tests/rib/polygon_concave.rib is an L-shape with
 * one reflex vertex placed so the fan-from-vertex-0 diagonals a fan would
 * have drawn cross outside the polygon -- world (0.2, 0.2), raster
 * (104, 96), sits in the removed notch square, inside the fan's own
 * triangles but outside the true L. polygon_concave_cw.rib is the same
 * shape wound the other way, polygon_concave_multi.rib a 4-pointed star
 * with four reflex vertices instead of one, and polygon_collinear.rib the
 * same L with a vertex added on a straight edge. All four raster
 * coordinates below are hand-projected the same way as the pentagon's,
 * from each fixture's own "P".
 *
 * checkTriangleCount below calls GMANPatchPolyObjectManager::getRSPolygon
 * directly (white-box, mirroring normals_test.cpp and patchmesh_test.cpp)
 * to assert nverts - 2 triangles come out the other side -- concave,
 * reversed winding, collinear or duplicate vertices included -- which
 * distinguishes a finished triangulation from a stalled one; a stalled
 * loop would time out under ctest rather than fail an assertion.
 *
 * Revert checks (verified by actually reverting, not asserted):
 *   - Restoring GMANPatchPolyObjectManager::getRSPolygon's triangulation
 *     to the fan-from-vertex-0 it replaced fails every notch-background
 *     assertion below (polygon_concave, polygon_concave_cw,
 *     polygon_concave_multi, polygon_collinear all go red) while the
 *     convex pentagon assertions stay green -- the fan is correct for
 *     convex input, wrong only for concave.
 *   - Reverting to a bare create() instead: the pentagon fixture renders
 *     pure background, failing every coverage and color assertion.
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
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanobject.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanprimitives.h"
#include "gmantransform.h"

namespace {

struct Point {
  double x, y;
};

// The pentagon's exact raster-space vertices, hand-projected from
// tests/rib/polygon.rib's "P" as described in the file comment above.
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

const int kColorTol = 8; // per-channel tolerance out of 255, this
                          // codebase's standard (silhouette_test.cpp,
                          // lighting_test.cpp)

void testConvexPentagon(const std::string &gman, const std::string &ribDir) {
  const std::string rib = ribDir + "/polygon.rib";
  check(runGman(gman, rib) == 0, "polygon.rib renders");

  Image img = readTIFF("polygon.tif");
  check(img.ok, "polygon.tif reads back");
  if (!img.ok) {
    return;
  }

  const uint32_t bg = img.at(0, 0);
  std::vector<Point> inner = scaled(kPentagon, 5, kCentroid, 0.85);
  std::vector<Point> outer = scaled(kPentagon, 5, kCentroid, 1.15);

  int insideChecked = 0, outsideChecked = 0;
  int insideFailures = 0, outsideFailures = 0;
  for (int y = 70; y <= 126; y += 2) {
    for (int x = 70; x <= 130; x += 2) {
      bool coveredPixel = differsFromBackground(img.at(x, y), bg, kColorTol);
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
}

// Shared by every concave/collinear/winding fixture below: render, read
// the named TIFF back, and check each hand-projected pixel against
// background -- the same differsFromBackground comparison
// testConvexPentagon already uses, not a second mechanism.
void checkFillPattern(const std::string &gman, const std::string &ribDir,
                       const std::string &name,
                       const std::vector<std::pair<int, int>> &mustBeBg,
                       const std::vector<std::pair<int, int>> &mustBeFilled) {
  const std::string rib = ribDir + "/" + name + ".rib";
  check(runGman(gman, rib) == 0, name + ".rib renders");

  Image img = readTIFF(name + ".tif");
  check(img.ok, name + ".tif reads back");
  if (!img.ok) {
    return;
  }
  const uint32_t bg = img.at(0, 0);

  for (const std::pair<int, int> &pix : mustBeBg) {
    bool covered = differsFromBackground(img.at(pix.first, pix.second), bg,
                                          kColorTol);
    check(!covered, name + ": (" + std::to_string(pix.first) + "," +
                        std::to_string(pix.second) +
                        ") stays background, outside the true polygon");
  }
  for (const std::pair<int, int> &pix : mustBeFilled) {
    bool covered = differsFromBackground(img.at(pix.first, pix.second), bg,
                                          kColorTol);
    check(covered, name + ": (" + std::to_string(pix.first) + "," +
                       std::to_string(pix.second) +
                       ") is filled, inside the true polygon");
  }
}

// ---- white-box: n - 2 triangles, whatever ear clipping had to do ----
//
// Calls GMANPatchPolyObjectManager::getRSPolygon directly (mirroring
// normals_test.cpp and patchmesh_test.cpp's own direct-instantiation
// tests) so the triangle count is read straight off the returned face
// chain, not inferred from a render. A stalled ear-clipping loop would
// time out under ctest here rather than fail this assertion.

int countFaces(GMANObject *object) {
  if (object == nullptr || object->getBody() == nullptr) {
    return 0;
  }
  GMANSurface *surface = object->getBody()->getSurface();
  int count = 0;
  for (GMANFace *face = surface ? surface->getFace() : nullptr;
       face != nullptr; face = face->getNext()) {
    ++count;
  }
  return count;
}

void checkTriangleCount(const std::string &label, std::vector<RtFloat> p,
                         RtInt nverts) {
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/nverts,
                       /*varying=*/nverts, /*uniform=*/1);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;

  GMANPrimitive *prim =
      mgr.getRSPolygon(nverts, pl, &options, &attr, &transform);
  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, label + ": getRSPolygon returns an object");

  int faces = countFaces(object);
  check(faces == nverts - 2,
        label + ": " + std::to_string(nverts) + " vertices yield " +
            std::to_string(nverts - 2) + " triangles (got " +
            std::to_string(faces) + ")");
  delete prim;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n",
                  argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testConvexPentagon(gman, ribDir);

  // polygon_concave.rib: an L-shape, one reflex vertex at world (0,0,0).
  // World (0.2, 0.2) -> raster (104, 96) sits in the removed notch
  // square: inside the fan-from-vertex-0 triangulation this replaces,
  // outside the true L.
  checkFillPattern(gman, ribDir, "polygon_concave",
                   {{104, 96}},
                   {{110, 110}, {90, 90}, {90, 110}});

  // polygon_concave_cw.rib: the same L, vertex order reversed. A
  // triangulator correct for one winding and wrong for the other is the
  // standard error.
  checkFillPattern(gman, ribDir, "polygon_concave_cw",
                   {{104, 96}},
                   {{110, 110}, {90, 90}, {90, 110}});

  // polygon_concave_multi.rib: a 4-pointed star, four reflex vertices.
  // World (0.495, 0.495) -> raster (110, 90) sits between two tips,
  // outside the star; the centre and a point toward each tip are inside.
  checkFillPattern(gman, ribDir, "polygon_concave_multi",
                   {{110, 90}},
                   {{100, 100}, {118, 100}, {82, 100}});

  // polygon_collinear.rib: polygon_concave.rib's L with a vertex added on
  // a straight edge -- a zero-area ear a naive test rejects forever.
  // Renders identically to polygon_concave.rib if the loop still
  // terminates.
  checkFillPattern(gman, ribDir, "polygon_collinear",
                   {{104, 96}},
                   {{110, 110}, {90, 90}, {90, 110}});

  // n - 2 triangles, direct: convex, concave, reversed winding, a star
  // with four reflex vertices, a collinear vertex and a duplicate vertex.
  checkTriangleCount("convex pentagon",
                     {0, 1, 0,  -0.9510565f, 0.3090170f, 0,
                      -0.5877853f, -0.8090170f, 0,  0.5877853f, -0.8090170f, 0,
                      0.9510565f, 0.3090170f, 0},
                     5);
  checkTriangleCount("concave L",
                     {1, -1, 0,  1, 0, 0,  0, 0, 0,
                      0, 1, 0,  -1, 1, 0,  -1, -1, 0},
                     6);
  checkTriangleCount("concave L, reversed winding",
                     {-1, -1, 0,  -1, 1, 0,  0, 1, 0,
                      0, 0, 0,  1, 0, 0,  1, -1, 0},
                     6);
  checkTriangleCount("4-pointed star, four reflex vertices",
                     {1.0f, 0.0f, 0,  0.2475f, 0.2475f, 0,  0.0f, 1.0f, 0,
                      -0.2475f, 0.2475f, 0,  -1.0f, 0.0f, 0,
                      -0.2475f, -0.2475f, 0,  0.0f, -1.0f, 0,
                      0.2475f, -0.2475f, 0},
                     8);
  checkTriangleCount("concave L with a collinear vertex",
                     {1, -1, 0,  1, -0.5, 0,  1, 0, 0,  0, 0, 0,
                      0, 1, 0,  -1, 1, 0,  -1, -1, 0},
                     7);
  checkTriangleCount("concave L with a duplicate vertex",
                     {1, -1, 0,  1, -1, 0,  1, 0, 0,  0, 0, 0,
                      0, 1, 0,  -1, 1, 0,  -1, -1, 0},
                     7);

  return checkSummary("polygon holds");
}
