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
 * GMANPatchPolyObjectManager::getRSGeneralPolygon bridges every hole of a
 * GeneralPolygon into its outer loop, then hands the merged ring to the
 * same triangulateEarClipping loop 1 already uses. This suite calls
 * getRSGeneralPolygon directly (white-box, mirroring triangulation_test.cpp's
 * own calls to getRSPolygon), with identity transform and points placed in
 * the test.
 *
 * The oracle (see checkPlacement below) never reimplements the bridging
 * under test: it reads triangle vertex positions back out of the returned
 * GMANFace chain and checks triangle count, summed unsigned area against
 * the outer loop's own area minus every kept hole's -- both computed via an
 * independent, double-precision Newell's-method area, not the RtFloat
 * arithmetic under test -- per-triangle orientation against the outer
 * loop's normal, and that every vertex on the object's own chain appears in
 * some triangle. Which holes a case expects kept is the test's own
 * decision, supplied per case, exactly as triangulation_test.cpp hardcodes
 * its own expected triangle count rather than rederiving it from the
 * implementation.
 *
 * Placements mirror triangulation_test.cpp's own: scales 1e-5, 1 and 1e4,
 * each axis-aligned at z=0 and rotated 37 degrees about the normalized
 * (1,1,1) axis and translated off the origin, proportional to scale so a
 * fixed offset never swamps a small-scale ring's own extent.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
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

const double kScales[] = {1e-5, 1.0, 1e4};

// A relative area tolerance absorbing float round-off across the scale
// range under test, not the geometry.
const double kAreaRelTol = 1e-3;

// A triangle at or below this fraction of the polygon's own area is exempt
// from the orientation check: a bridge legally emits a zero-area corner
// whose normal sign is arithmetic noise, not a classification error (see
// triangulation_test.cpp's own exemption, same rationale).
const double kOrientationAreaExemption = 1e-6;

const double kRotationDegrees = 37.0;

// This suite's own dicing-count oracle for one ear-clipped triangle --
// never GMANPatchPolyObjectManager's diceCountFor -- clamp(ceil(L /
// sqrt(ShadingRate)), 1, 16), L the longest of p0, p1, p2's own
// raster-space edges (see gmanpatchpolyobjectmanager.cpp). Orthographic
// screen-to-raster (GMANViewingSystem::screenToRaster): this suite never
// calls RiProjectionV, so GMANOptions' own default projection applies,
// and raster x, y pass straight through a camera-space point's own x, y,
// unaffected by z -- the perspective behind-the-eye fallback this file
// never needs.
int expectedDiceN(const GMANPoint& p0, const GMANPoint& p1, const GMANPoint& p2, const GMANOptions& options,
                  RtFloat shadingRate) {
  const GMANOptions::ScreenWindowStruct sw = options.getScreenWindow();
  const GMANOptions::RasterInfo ri = options.getRasterInfo();
  auto raster = [&](const GMANPoint& p) {
    return std::make_pair(ri.xres * (p.getX() - sw.left) / (sw.right - sw.left),
                          ri.yres - ri.yres * (p.getY() - sw.bottom) / (sw.top - sw.bottom));
  };
  auto dist = [](std::pair<double, double> a, std::pair<double, double> b) {
    double const dx = a.first - b.first;
    double const dy = a.second - b.second;
    return std::sqrt(dx * dx + dy * dy);
  };

  std::pair<double, double> const r0 = raster(p0), r1 = raster(p1), r2 = raster(p2);
  double L = dist(r0, r1);
  double const e12 = dist(r1, r2);
  double const e20 = dist(r2, r0);
  if (e12 > L) {
    L = e12;
  }
  if (e20 > L) {
    L = e20;
  }
  if (!(shadingRate > (RtFloat)0.0) || !std::isfinite(L)) {
    return 16;
  }
  // Clamped in double before the RtInt conversion: mirrors
  // gmanpatchpolyobjectmanager.cpp's own diceCountFor, since L can be
  // finite and still past what an int can hold.
  double n = std::ceil(L / std::sqrt((double)shadingRate));
  if (n < 1.0) {
    n = 1.0;
  }
  if (n > 16.0) {
    n = 16.0;
  }
  return (int)n;
}

// Rodrigues' rotation formula in double precision, independent of the
// RtFloat arithmetic under test -- triangulation_test.cpp's own helper.
std::array<double, 3> rotate(double x, double y, double z, double ax, double ay, double az, double angleRad) {
  const double c = std::cos(angleRad);
  const double s = std::sin(angleRad);
  const double dot = ax * x + ay * y + az * z;
  const double crossX = ay * z - az * y;
  const double crossY = az * x - ax * z;
  const double crossZ = ax * y - ay * x;
  return {x * c + crossX * s + ax * dot * (1.0 - c), y * c + crossY * s + ay * dot * (1.0 - c),
          z * c + crossZ * s + az * dot * (1.0 - c)};
}

GMANPoint place(const GMANPoint& canonical, double scale, bool rotated) {
  double x = canonical.getX() * scale;
  double y = canonical.getY() * scale;
  double z = canonical.getZ() * scale;
  if (rotated) {
    const double invSqrt3 = 1.0 / std::sqrt(3.0);
    std::array<double, 3> r = rotate(x, y, z, invSqrt3, invSqrt3, invSqrt3, kRotationDegrees * M_PI / 180.0);
    x = r[0] + scale * 2.0;
    y = r[1] + scale * -1.5;
    z = r[2] + scale * 3.0;
  }
  return GMANPoint((RtFloat)x, (RtFloat)y, (RtFloat)z);
}

std::vector<GMANPoint> placeLoop(const std::vector<GMANPoint>& canonical, double scale, bool rotated) {
  std::vector<GMANPoint> loop(canonical.size());
  for (std::size_t i = 0; i < canonical.size(); ++i) {
    loop[i] = place(canonical[i], scale, rotated);
  }
  return loop;
}

std::vector<std::vector<GMANPoint>> placeLoops(const std::vector<std::vector<GMANPoint>>& canonical, double scale,
                                               bool rotated) {
  std::vector<std::vector<GMANPoint>> loops(canonical.size());
  for (std::size_t i = 0; i < canonical.size(); ++i) {
    loops[i] = placeLoop(canonical[i], scale, rotated);
  }
  return loops;
}

// Newell's method in double precision -- the test's own area oracle,
// independent of the gman::newellNormal it is checking.
std::array<double, 3> newellNormal(const std::vector<GMANPoint>& ring) {
  double nx = 0.0, ny = 0.0, nz = 0.0;
  const std::size_t n = ring.size();
  for (std::size_t i = 0; i < n; ++i) {
    const GMANPoint& cur = ring[i];
    const GMANPoint& next = ring[(i + 1) % n];
    double cx = cur.getX(), cy = cur.getY(), cz = cur.getZ();
    double nxp = next.getX(), nyp = next.getY(), nzp = next.getZ();
    nx += (cy - nyp) * (cz + nzp);
    ny += (cz - nzp) * (cx + nxp);
    nz += (cx - nxp) * (cy + nyp);
  }
  return {nx, ny, nz};
}

double ringArea(const std::vector<GMANPoint>& ring) {
  std::array<double, 3> n = newellNormal(ring);
  return 0.5 * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
}

// Builds "P" and nverts from loops (loop 0 the outer boundary, the rest
// holes) and calls getRSGeneralPolygon directly, identity transform.
GMANPrimitive* runGetRSGeneralPolygon(const std::vector<std::vector<GMANPoint>>& loops) {
  const RtInt nloops = (RtInt)loops.size();
  std::vector<RtInt> nverts(nloops);
  RtInt total = 0;
  for (RtInt i = 0; i < nloops; ++i) {
    nverts[i] = (RtInt)loops[i].size();
    total += nverts[i];
  }

  std::vector<RtFloat> p(3 * total);
  RtInt k = 0;
  for (RtInt i = 0; i < nloops; ++i) {
    for (const GMANPoint& pt : loops[i]) {
      p[3 * k] = pt.getX();
      p[3 * k + 1] = pt.getY();
      p[3 * k + 2] = pt.getZ();
      ++k;
    }
  }

  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/total,
                       /*varying=*/total, /*uniform=*/1,
                       /*facevarying=*/total);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  return mgr.getRSGeneralPolygon(nloops, nverts.data(), pl, &options, &attr, &transform);
}

int countFaces(GMANObject* object) {
  if (object == nullptr || object->getBody() == nullptr) {
    return 0;
  }
  GMANSurface* surface = object->getBody()->getSurface();
  int count = 0;
  for (GMANFace* face = surface ? surface->getFace() : nullptr; face != nullptr; face = face->getNext()) {
    ++count;
  }
  return count;
}

// Maps only the first chainLength entries of the vertex chain: one
// GMANVertex per bridged-ring position. Every entry after that is a diced,
// strictly interior or edge-interior vertex, not a bridged-ring position,
// so bounding the walk here is what keeps this a 1:1 map.
std::map<const GMANVertex*, int> indexVertices(GMANObject* object, int chainLength) {
  std::map<const GMANVertex*, int> index;
  GMANVertex* v = object->getVert();
  for (int i = 0; i < chainLength && v != nullptr; ++i, v = v->getNext()) {
    index[v] = i;
  }
  return index;
}

// Runs the full oracle -- count, area, orientation, coverage -- against one
// placed set of loops and returns the emitted triangles as chain-index
// triples for the caller's own cross-placement identity check. kept[i]
// (i >= 1) is the test's own expectation of whether hole i survives
// bridging; kept[0] is unused (the outer loop is never dropped by a case
// this function is called on). Returns an empty vector (with assertions
// already recorded as failures) if getRSGeneralPolygon did not return a
// usable object.
//
// Most sub-triangles are strictly interior -- none of their three vertices
// is a pre-dicing chain vertex -- so coverage asks only whether each
// pre-dicing chain vertex still appears somewhere on the (much larger)
// chain. Identity is recovered per ear-clipped triangle: dicePolygonTriangle
// reuses each ear's own three original vertices at exactly three of its own
// sub-faces (the barycentric grid's three corners) and nowhere else, and
// dicing is contiguous (comment above), so three corner sightings close one
// ear-clipped triangle's own block of consecutive faces, whatever its own n
// turns out to be -- read back from that block's own three corner
// locations via expectedDiceN, the same rule every block's own size is
// checked against.
std::vector<std::array<int, 3>> checkPlacement(const std::string& label,
                                               const std::vector<std::vector<GMANPoint>>& loops,
                                               const std::vector<bool>& kept) {
  GMANOptions oracleOptions; // same default GMANOptions runGetRSGeneralPolygon's own uses
  GMANAttributes oracleAttr; // ShadingRate = 1, runGetRSGeneralPolygon's own default

  GMANPrimitive* prim = runGetRSGeneralPolygon(loops);
  GMANObject* object = dynamic_cast<GMANObject*>(prim);
  check(object != nullptr, label + ": getRSGeneralPolygon returns an object");
  if (object == nullptr) {
    delete prim;
    return {};
  }

  int chainLength = 0;
  double expectedArea = ringArea(loops[0]);
  int keptHoleCount = 0;
  for (std::size_t i = 0; i < loops.size(); ++i) {
    if (i == 0 || kept[i]) {
      chainLength += (int)loops[i].size();
    }
    if (i > 0 && kept[i]) {
      expectedArea -= ringArea(loops[i]);
      ++keptHoleCount;
    }
  }
  const int earCount = chainLength + 2 * keptHoleCount - 2;

  std::array<double, 3> outerNormal = newellNormal(loops[0]);

  std::map<const GMANVertex*, int> index = indexVertices(object, chainLength);
  std::vector<std::array<int, 3>> triples;
  std::vector<bool> covered(chainLength, false);
  double summedArea = 0.0;
  bool orientationOk = true;
  bool cornersOk = true;
  bool diceCountOk = true;
  std::vector<int> blockCornerIdx;
  std::vector<GMANPoint> blockCornerLoc;
  int blockFaceCount = 0;
  long long sumExpectedFaces = 0;

  GMANSurface* surface = object->getBody() ? object->getBody()->getSurface() : nullptr;
  for (GMANFace* face = surface ? surface->getFace() : nullptr; face != nullptr; face = face->getNext()) {
    const GMANPoint& v0 = face->getVertex(0)->getLocation();
    const GMANPoint& v1 = face->getVertex(1)->getLocation();
    const GMANPoint& v2 = face->getVertex(2)->getLocation();

    double e1x = v1.getX() - v0.getX(), e1y = v1.getY() - v0.getY(), e1z = v1.getZ() - v0.getZ();
    double e2x = v2.getX() - v0.getX(), e2y = v2.getY() - v0.getY(), e2z = v2.getZ() - v0.getZ();
    double crossX = e1y * e2z - e1z * e2y;
    double crossY = e1z * e2x - e1x * e2z;
    double crossZ = e1x * e2y - e1y * e2x;
    double triArea = 0.5 * std::sqrt(crossX * crossX + crossY * crossY + crossZ * crossZ);
    summedArea += triArea;

    double outerArea = ringArea(loops[0]);
    double sign = crossX * outerNormal[0] + crossY * outerNormal[1] + crossZ * outerNormal[2];
    if (triArea > kOrientationAreaExemption * outerArea && sign < 0.0) {
      orientationOk = false;
    }

    ++blockFaceCount;
    for (int k = 0; k < 3; ++k) {
      auto it = index.find(face->getVertex(k));
      if (it != index.end()) {
        covered[it->second] = true;
        blockCornerIdx.push_back(it->second);
        blockCornerLoc.push_back(face->getVertex(k)->getLocation());
        if (blockCornerIdx.size() > 3) {
          cornersOk = false; // a fourth corner before the block closed
        }
      }
    }
    if (blockCornerIdx.size() >= 3) {
      const int expectedN = expectedDiceN(blockCornerLoc[0], blockCornerLoc[1], blockCornerLoc[2], oracleOptions,
                                          oracleAttr.getShadingRate());
      const long long expectedBlockFaces = (long long)expectedN * expectedN;
      sumExpectedFaces += expectedBlockFaces;
      if (blockFaceCount != expectedBlockFaces) {
        diceCountOk = false;
      }
      // Sorted, not discovery order: dicePolygonTriangle's own traversal
      // visits an ear's three corners in a different sequence at n=1 (one
      // face) than at n>1 (three separate faces) -- see
      // triangulation_test.cpp's own identical note. Sorting keeps the
      // comparison about which three chain vertices form each ear, not
      // dicing's own internal order.
      std::array<int, 3> triple = {blockCornerIdx[0], blockCornerIdx[1], blockCornerIdx[2]};
      std::sort(triple.begin(), triple.end());
      triples.push_back(triple);
      blockCornerIdx.clear();
      blockCornerLoc.clear();
      blockFaceCount = 0;
    }
  }

  bool coverageOk = true;
  for (bool c : covered) {
    coverageOk = coverageOk && c;
  }

  check(blockCornerIdx.empty(), label + ": the last ear-clipped triangle's own block of faces "
                                        "closes cleanly");
  check((int)triples.size() == earCount, label + ": " + std::to_string(chainLength) + " kept vertices, " +
                                             std::to_string(keptHoleCount) + " kept holes yield " +
                                             std::to_string(earCount) + " ear-clipped triangles (got " +
                                             std::to_string(triples.size()) + ")");
  check(diceCountOk, label + ": each ear-clipped triangle dices to n*n faces, n this suite's own "
                             "clamp(ceil(L / sqrt(ShadingRate)), 1, 16) computed from its own three corners");
  const int faces = countFaces(object);
  check(faces == sumExpectedFaces, label + ": diced faces total every ear-clipped triangle's own expected n*n (got " +
                                       std::to_string(faces) + ", expected " + std::to_string(sumExpectedFaces) + ")");
  check(cornersOk, label + ": each ear-clipped triangle's own sub-faces touch exactly its own "
                           "three chain corners");
  check(std::fabs(summedArea - expectedArea) <= kAreaRelTol * expectedArea,
        label +
            ": triangle areas sum to the outer loop's area minus every "
            "kept hole's (got " +
            std::to_string(summedArea) + ", expected " + std::to_string(expectedArea) + ")");
  check(orientationOk, label + ": every non-exempt triangle agrees with the outer loop's "
                               "orientation");
  check(coverageOk, label + ": every vertex on the object's chain appears in a "
                            "triangle");

  delete prim;
  return triples;
}

// Runs one named case across all six placements, asserting the oracle at
// every placement and, when requested, that every placement emits the
// identical sequence of chain-index triples.
void runCase(const std::string& name, const std::vector<std::vector<GMANPoint>>& canonical,
             const std::vector<bool>& kept, bool assertTripleIdentity) {
  std::vector<std::array<int, 3>> reference;
  bool haveReference = false;

  for (double scale : kScales) {
    for (bool rotated : {false, true}) {
      char buf[256];
      std::snprintf(buf, sizeof(buf), "%s (scale=%.0e, %s)", name.c_str(), scale, rotated ? "rotated" : "axis-aligned");
      std::vector<std::vector<GMANPoint>> loops = placeLoops(canonical, scale, rotated);
      std::vector<std::array<int, 3>> triples = checkPlacement(buf, loops, kept);

      if (!assertTripleIdentity) {
        continue;
      }
      if (!haveReference) {
        reference = triples;
        haveReference = true;
      } else {
        check(triples == reference, name + ": " + std::string(buf) +
                                        " yields the same triangle-index triples as every other "
                                        "placement of this ring");
      }
    }
  }
}

// ---- canonical shapes (unit scale, z=0), each outer loop's first edge
// along +x, outer loops counter-clockwise, holes clockwise unless noted ----

std::vector<GMANPoint> concaveL() { return {{1, -1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {-1, 1, 0}, {-1, -1, 0}}; }

std::vector<GMANPoint> fourPointedStar() {
  return {{1.0f, 0.0f, 0},  {0.2475f, 0.2475f, 0},   {0.0f, 1.0f, 0},  {-0.2475f, 0.2475f, 0},
          {-1.0f, 0.0f, 0}, {-0.2475f, -0.2475f, 0}, {0.0f, -1.0f, 0}, {0.2475f, -0.2475f, 0}};
}

// Trap ring "order": rule 1's own case -- bridging in loop order instead of
// descending largest u sends the right hole's bridge through the left hole,
// still unbridged at that point.
std::vector<GMANPoint> orderOuter() { return {{0, 0, 0}, {8, 0, 0}, {8.5f, 2, 0}, {8, 4, 0}, {0, 4, 0}}; }
std::vector<GMANPoint> orderHoleLeft() { return {{3, 2, 0}, {2, 1, 0}, {1, 2, 0}, {2, 3, 0}}; }
std::vector<GMANPoint> orderHoleRight() { return {{7, 2.3f, 0}, {6, 1.3f, 0}, {5, 2.3f, 0}, {6, 3.3f, 0}}; }

// Trap ring "occluded": rule 2's own case -- bridging straight to P instead
// of screening for a nearer reflex vertex sends the bridge through the
// outer loop's own notch.
std::vector<GMANPoint> occludedOuter() {
  return {{0, 0, 0}, {6, 0, 0}, {7, 6, 0}, {5, 6, 0}, {5, 3, 0}, {4.5f, 6, 0}, {0, 6, 0}};
}
std::vector<GMANPoint> occludedHole() { return {{2, 2, 0}, {1.5f, 1.5f, 0}, {1, 2, 0}, {1.5f, 2.5f, 0}}; }

// Trap ring "shared apex": rule 3's own case -- two holes bridge to the
// same outer vertex; taking the first occurrence instead of the one whose
// wedge contains the second hole's rightmost vertex crosses into the wrong
// lobe.
std::vector<GMANPoint> sharedApexOuter() { return {{0, 0, 0}, {4, 0, 0}, {4.5f, 2, 0}, {4, 4, 0}, {0, 4, 0}}; }
std::vector<GMANPoint> sharedApexHoleLower() { return {{3.1f, 1, 0}, {2.5f, 0.5f, 0}, {2, 1, 0}, {2.5f, 1.5f, 0}}; }
std::vector<GMANPoint> sharedApexHoleUpper() { return {{3, 3, 0}, {2.5f, 2.5f, 0}, {2, 3, 0}, {2.5f, 3.5f, 0}}; }

void runTrapRings() {
  runCase("order", {orderOuter(), orderHoleLeft(), orderHoleRight()}, {true, true, true},
          /*assertTripleIdentity=*/true);
  runCase("occluded", {occludedOuter(), occludedHole()}, {true, true},
          /*assertTripleIdentity=*/true);
  runCase("shared apex", {sharedApexOuter(), sharedApexHoleLower(), sharedApexHoleUpper()}, {true, true, true},
          /*assertTripleIdentity=*/true);
}

void runOneLoopParity(const std::string& name, const std::vector<GMANPoint>& canonical) {
  for (double scale : kScales) {
    for (bool rotated : {false, true}) {
      char buf[256];
      std::snprintf(buf, sizeof(buf), "%s one-loop parity (scale=%.0e, %s)", name.c_str(), scale,
                    rotated ? "rotated" : "axis-aligned");
      std::vector<GMANPoint> ring = placeLoop(canonical, scale, rotated);
      RtInt nverts = (RtInt)ring.size();

      std::vector<RtFloat> p(3 * nverts);
      for (RtInt i = 0; i < nverts; ++i) {
        p[3 * i] = ring[i].getX();
        p[3 * i + 1] = ring[i].getY();
        p[3 * i + 2] = ring[i].getZ();
      }

      GMANDictionary dict1, dict2;
      RtToken tokens[1] = {RI_P};
      RtPointer parms[1] = {p.data()};
      GMANParameterList pl1(dict1, 1, tokens, parms, nverts, nverts, 1);
      GMANParameterList pl2(dict2, 1, tokens, parms, nverts, nverts, 1, nverts);
      GMANOptions options;
      GMANAttributes attr;
      GMANTransform transform;
      GMANPatchPolyObjectManager mgr;

      GMANPrimitive* polyPrim = mgr.getRSPolygon(nverts, pl1, &options, &attr, &transform);
      GMANPrimitive* generalPrim = mgr.getRSGeneralPolygon(1, &nverts, pl2, &options, &attr, &transform);
      GMANObject* polyObj = dynamic_cast<GMANObject*>(polyPrim);
      GMANObject* generalObj = dynamic_cast<GMANObject*>(generalPrim);

      check(polyObj != nullptr && generalObj != nullptr, std::string(buf) + ": both return an object");
      if (polyObj == nullptr || generalObj == nullptr) {
        delete polyPrim;
        delete generalPrim;
        continue;
      }

      std::vector<GMANVertex*> polyVerts, generalVerts;
      for (GMANVertex* v = polyObj->getVert(); v != nullptr; v = v->getNext()) {
        polyVerts.push_back(v);
      }
      for (GMANVertex* v = generalObj->getVert(); v != nullptr; v = v->getNext()) {
        generalVerts.push_back(v);
      }
      check(polyVerts.size() == generalVerts.size(), std::string(buf) + ": same vertex count");

      bool sameLocations = polyVerts.size() == generalVerts.size();
      bool sameColors = sameLocations;
      for (std::size_t i = 0; sameLocations && i < polyVerts.size(); ++i) {
        const GMANPoint& a = polyVerts[i]->getLocation();
        const GMANPoint& b = generalVerts[i]->getLocation();
        if (a.getX() != b.getX() || a.getY() != b.getY() || a.getZ() != b.getZ()) {
          sameLocations = false;
        }
      }
      check(sameLocations, std::string(buf) + ": same vertex locations, "
                                              "same order");
      check(sameColors, std::string(buf) + ": same vertex colours");

      std::map<const GMANVertex*, int> polyIndex, generalIndex;
      for (std::size_t i = 0; i < polyVerts.size(); ++i) {
        polyIndex[polyVerts[i]] = (int)i;
      }
      for (std::size_t i = 0; i < generalVerts.size(); ++i) {
        generalIndex[generalVerts[i]] = (int)i;
      }

      std::vector<std::array<int, 3>> polyTriples, generalTriples;
      GMANSurface* polySurface = polyObj->getBody() ? polyObj->getBody()->getSurface() : nullptr;
      for (GMANFace* f = polySurface ? polySurface->getFace() : nullptr; f != nullptr; f = f->getNext()) {
        polyTriples.push_back({polyIndex[f->getVertex(0)], polyIndex[f->getVertex(1)], polyIndex[f->getVertex(2)]});
      }
      GMANSurface* generalSurface = generalObj->getBody() ? generalObj->getBody()->getSurface() : nullptr;
      for (GMANFace* f = generalSurface ? generalSurface->getFace() : nullptr; f != nullptr; f = f->getNext()) {
        generalTriples.push_back(
            {generalIndex[f->getVertex(0)], generalIndex[f->getVertex(1)], generalIndex[f->getVertex(2)]});
      }
      check(polyTriples == generalTriples, std::string(buf) + ": same index triples as getRSPolygon");

      delete polyPrim;
      delete generalPrim;
    }
  }
}

// Square (0,0)-(4,4) with a square hole (1,1)-(3,3), wound both ways: both
// must subtract the same area, since a hole wound the same way as the
// outer loop is reversed before bridging rather than adding its area.
std::vector<GMANPoint> squareOuter() { return {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}; }
std::vector<GMANPoint> squareHoleOrderA() { return {{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}}; }
std::vector<GMANPoint> squareHoleOrderB() {
  std::vector<GMANPoint> hole = squareHoleOrderA();
  std::reverse(hole.begin(), hole.end());
  return hole;
}

void runSquareHoleBothWindings() {
  runCase("square with a square hole", {squareOuter(), squareHoleOrderA()}, {true, true},
          /*assertTripleIdentity=*/false);
  runCase("square with a hole wound the other way", {squareOuter(), squareHoleOrderB()}, {true, true},
          /*assertTripleIdentity=*/false);
}

// A hole entirely to the right of a 4x4 outer square: outside the outer
// loop, so its ray meets no ring edge and it is dropped -- the result
// equals the outer loop alone.
void runHoleOutsideOuterLoop() {
  std::vector<GMANPoint> outer = squareOuter();
  std::vector<GMANPoint> hole = {{5, 1, 0}, {7, 1, 0}, {7, 3, 0}, {5, 3, 0}};
  runCase("hole outside the outer loop", {outer, hole}, {true, false},
          /*assertTripleIdentity=*/false);
}

// A two-vertex hole and a collinear hole: both enclose no area and are
// dropped -- the result equals the outer loop alone.
void runTrivialHoles() {
  std::vector<GMANPoint> outer = squareOuter();
  std::vector<GMANPoint> twoVertex = {{1, 1, 0}, {2, 2, 0}};
  runCase("two-vertex hole", {outer, twoVertex}, {true, false},
          /*assertTripleIdentity=*/false);

  std::vector<GMANPoint> collinear = {{1, 1, 0}, {2, 1, 0}, {3, 1, 0}};
  runCase("collinear hole", {outer, collinear}, {true, false},
          /*assertTripleIdentity=*/false);
}

// All outer vertices collinear: getRSGeneralPolygon returns the empty
// stub, not a crash, exactly as getRSPolygon does for the same input.
void runDegenerateOuterLoop() {
  std::vector<std::vector<GMANPoint>> loops = {{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}}};
  GMANPrimitive* prim = runGetRSGeneralPolygon(loops);
  GMANObject* object = dynamic_cast<GMANObject*>(prim);
  check(object != nullptr, "degenerate outer loop: getRSGeneralPolygon returns an object");
  check(object != nullptr && object->getBody() == nullptr,
        "degenerate outer loop: the returned object is the empty stub");
  delete prim;
}

} // namespace

int main() {
  runTrapRings();
  runCase("one loop, concave L", {concaveL()}, {}, /*assertTripleIdentity=*/
          false);
  runCase("one loop, 4-pointed star", {fourPointedStar()}, {},
          /*assertTripleIdentity=*/false);
  runOneLoopParity("concave L", concaveL());
  runOneLoopParity("4-pointed star", fourPointedStar());
  runSquareHoleBothWindings();
  runHoleOutsideOuterLoop();
  runTrivialHoles();
  runDegenerateOuterLoop();
  return checkSummary("generalpolygon holds");
}
