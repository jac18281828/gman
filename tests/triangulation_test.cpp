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
 * getRSPolygon's triangulation classifies each vertex from an orientation
 * value compared against an absolute tolerance. That comparison is scale-
 * dependent unless the value it tests is dimensionless: a polygon authored
 * a million times smaller, or moved a million units from the origin, is
 * the same polygon and must triangulate the same way. This suite calls
 * GMANPatchPolyObjectManager::getRSPolygon directly (white-box, mirroring
 * polygon_test.cpp's checkTriangleCount and normals_test.cpp) and runs each
 * of nine hand-built rings at six scales (1e-6 .. 1e6) in two placements --
 * axis-aligned at z=0, and rotated 37 degrees about the normalized
 * (1,1,1) axis and translated off the origin -- asserting every placement
 * satisfies the same geometric oracle and every placement of the same ring
 * yields the identical sequence of triangle-index triples. The rotated
 * placement's translation scales with the ring's own scale rather than
 * sitting at a fixed offset: adding a fixed offset to a 1e-6-scale ring
 * would swamp the polygon's own extent in the offset's rounding error
 * (float resolves ~1e-7 relative), collapsing distinct vertices into the
 * same representable float and turning a classification test into a
 * precision-loss test instead.
 *
 * A 200-polygon randomized stress (star-shaped, 5-40 vertices) runs the
 * same oracle without the triple-identity check, since two candidate ears
 * can tie on a random ring. A collinear-only and an identical-vertex ring
 * exercise getRSPolygon's degeneracy guard; an asymmetric self-intersecting
 * bow-tie exercises ear clipping's clipAt<0 fallback.
 *
 * The oracle (see checkPlacement below) never reimplements the
 * classification under test: it reads triangle vertex positions back out
 * of the returned GMANFace chain and checks triangle count, summed
 * unsigned area against the polygon's own Newell-derived area, per-
 * triangle orientation against the polygon's normal, and that every ring
 * index appears in some triangle. An inverted or missing-ear triangulation
 * fails the area or orientation check even when the triangle count still
 * happens to be right.
 *
 * Revert check (verified by actually reverting, not asserted): restoring
 * turnOrientation and getRSPolygon's degeneracy guard to their absolute-
 * RI_EPSILON comparisons turns the small-scale placements (1e-6, 1e-5) of
 * the concave rings red -- see the fix commit's own message for the
 * specific cases.
 */

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
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

// Six scales spanning the range getRSPolygon's degeneracy guard and
// turnOrientation's classification both stop being scale-invariant under
// the absolute RI_EPSILON this suite falsifies (see the fix commit).
const double kScales[] = {1e-6, 1e-5, 1e-4, 1.0, 1e4, 1e6};

// A relative area tolerance absorbing float round-off across six decades
// of scale, not the geometry under test.
const double kAreaRelTol = 1e-3;

// A triangle at or below this fraction of the polygon's own area is
// exempt from the orientation check: clipping a collinear vertex legally
// emits a zero-area triangle, and the sign of a near-zero cross product is
// arithmetic noise, not a classification error.
const double kOrientationAreaExemption = 1e-6;

const double kRotationDegrees = 37.0;

// Rodrigues' rotation formula in double precision -- an independent
// computation from the RtFloat arithmetic under test, so the placement
// itself never hides behind the same rounding the oracle is meant to
// catch.
std::array<double, 3> rotate(double x, double y, double z, double ax, double ay,
                             double az, double angleRad) {
  const double c = std::cos(angleRad);
  const double s = std::sin(angleRad);
  const double dot = ax * x + ay * y + az * z;
  const double crossX = ay * z - az * y;
  const double crossY = az * x - ax * z;
  const double crossZ = ax * y - ay * x;
  return {x * c + crossX * s + ax * dot * (1.0 - c),
          y * c + crossY * s + ay * dot * (1.0 - c),
          z * c + crossZ * s + az * dot * (1.0 - c)};
}

// Places a canonical (unit-scale, z=0) ring vertex at a given scale and,
// for the rotated plane, tilts it 37 degrees about the normalized
// (1,1,1) axis and offsets it by a vector proportional to the same scale
// -- "translated off the origin" without adding a scale-independent
// constant that would swamp a small-scale ring's own extent.
GMANPoint place(const GMANPoint &canonical, double scale, bool rotated) {
  double x = canonical.getX() * scale;
  double y = canonical.getY() * scale;
  double z = canonical.getZ() * scale;
  if (rotated) {
    const double invSqrt3 = 1.0 / std::sqrt(3.0);
    std::array<double, 3> r = rotate(x, y, z, invSqrt3, invSqrt3, invSqrt3,
                                     kRotationDegrees * M_PI / 180.0);
    x = r[0] + scale * 2.0;
    y = r[1] + scale * -1.5;
    z = r[2] + scale * 3.0;
  }
  return GMANPoint((RtFloat)x, (RtFloat)y, (RtFloat)z);
}

std::vector<GMANPoint> placeRing(const std::vector<GMANPoint> &canonical,
                                 double scale, bool rotated) {
  std::vector<GMANPoint> ring(canonical.size());
  for (std::size_t i = 0; i < canonical.size(); ++i) {
    ring[i] = place(canonical[i], scale, rotated);
  }
  return ring;
}

// Newell's method in double precision: the test's own area and normal
// oracle, independent of the anonymous-namespace newellNormal it is
// checking.
std::array<double, 3> newellNormal(const std::vector<GMANPoint> &ring) {
  double nx = 0.0, ny = 0.0, nz = 0.0;
  const std::size_t n = ring.size();
  for (std::size_t i = 0; i < n; ++i) {
    const GMANPoint &cur = ring[i];
    const GMANPoint &next = ring[(i + 1) % n];
    double cx = cur.getX(), cy = cur.getY(), cz = cur.getZ();
    double nxp = next.getX(), nyp = next.getY(), nzp = next.getZ();
    nx += (cy - nyp) * (cz + nzp);
    ny += (cz - nzp) * (cx + nxp);
    nz += (cx - nxp) * (cy + nyp);
  }
  return {nx, ny, nz};
}

GMANPrimitive *runGetRSPolygon(const std::vector<GMANPoint> &ring) {
  const RtInt nverts = (RtInt)ring.size();
  std::vector<RtFloat> p(3 * nverts);
  for (RtInt i = 0; i < nverts; ++i) {
    p[3 * i] = ring[i].getX();
    p[3 * i + 1] = ring[i].getY();
    p[3 * i + 2] = ring[i].getZ();
  }

  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/nverts,
                       /*varying=*/nverts, /*uniform=*/1);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  return mgr.getRSPolygon(nverts, pl, &options, &attr, &transform);
}

int countFaces(GMANObject *object) {
  if (object == nullptr || object->getBody() == nullptr) {
    return 0;
  }
  GMANSurface *surface = object->getBody()->getSurface();
  int count = 0;
  for (GMANFace *face = surface ? surface->getFace() : nullptr; face != nullptr;
       face = face->getNext()) {
    ++count;
  }
  return count;
}

// Maps every GMANVertex pointer getRSPolygon allocated back to its
// position in the ring, by walking the chain GMANObject::getVert() heads
// in ring order (per getRSPolygon's own construction).
std::map<const GMANVertex *, int> indexVertices(GMANObject *object) {
  std::map<const GMANVertex *, int> index;
  int i = 0;
  for (GMANVertex *v = object->getVert(); v != nullptr; v = v->getNext()) {
    index[v] = i++;
  }
  return index;
}

// Runs the full oracle -- count, area, orientation, coverage -- against
// one placed ring and returns the emitted triangles as ring-index triples
// for the caller's own cross-placement identity check. Returns an empty
// vector (with assertions already recorded as failures) if getRSPolygon
// did not return a usable object.
std::vector<std::array<int, 3>> checkPlacement(
    const std::string &label, const std::vector<GMANPoint> &ring) {
  const int nverts = (int)ring.size();
  GMANPrimitive *prim = runGetRSPolygon(ring);
  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, label + ": getRSPolygon returns an object");
  if (object == nullptr) {
    delete prim;
    return {};
  }

  const int faces = countFaces(object);
  check(faces == nverts - 2,
        label + ": " + std::to_string(nverts) + " vertices yield " +
            std::to_string(nverts - 2) + " triangles (got " +
            std::to_string(faces) + ")");

  std::array<double, 3> polyNormal = newellNormal(ring);
  const double polyArea = 0.5 * std::sqrt(polyNormal[0] * polyNormal[0] +
                                          polyNormal[1] * polyNormal[1] +
                                          polyNormal[2] * polyNormal[2]);

  std::map<const GMANVertex *, int> index = indexVertices(object);
  std::vector<std::array<int, 3>> triples;
  std::vector<bool> covered(nverts, false);
  double summedArea = 0.0;
  bool orientationOk = true;
  bool coverageOk = true;
  bool indicesOk = true;

  GMANSurface *surface =
      object->getBody() ? object->getBody()->getSurface() : nullptr;
  for (GMANFace *face = surface ? surface->getFace() : nullptr; face != nullptr;
       face = face->getNext()) {
    const GMANPoint &v0 = face->getVertex(0)->getLocation();
    const GMANPoint &v1 = face->getVertex(1)->getLocation();
    const GMANPoint &v2 = face->getVertex(2)->getLocation();

    double e1x = v1.getX() - v0.getX(), e1y = v1.getY() - v0.getY(),
           e1z = v1.getZ() - v0.getZ();
    double e2x = v2.getX() - v0.getX(), e2y = v2.getY() - v0.getY(),
           e2z = v2.getZ() - v0.getZ();
    double crossX = e1y * e2z - e1z * e2y;
    double crossY = e1z * e2x - e1x * e2z;
    double crossZ = e1x * e2y - e1y * e2x;
    double triArea =
        0.5 * std::sqrt(crossX * crossX + crossY * crossY + crossZ * crossZ);
    summedArea += triArea;

    double sign = crossX * polyNormal[0] + crossY * polyNormal[1] +
                  crossZ * polyNormal[2];
    if (triArea > kOrientationAreaExemption * polyArea && sign < 0.0) {
      orientationOk = false;
    }

    auto it0 = index.find(face->getVertex(0));
    auto it1 = index.find(face->getVertex(1));
    auto it2 = index.find(face->getVertex(2));
    if (it0 == index.end() || it1 == index.end() || it2 == index.end()) {
      indicesOk = false;
      continue;
    }
    covered[it0->second] = true;
    covered[it1->second] = true;
    covered[it2->second] = true;
    triples.push_back({it0->second, it1->second, it2->second});
  }

  for (bool c : covered) {
    coverageOk = coverageOk && c;
  }

  check(indicesOk, label + ": every face vertex maps back to a ring index");
  check(std::fabs(summedArea - polyArea) <= kAreaRelTol * polyArea,
        label + ": triangle areas sum to the polygon's area (got " +
            std::to_string(summedArea) + ", expected " +
            std::to_string(polyArea) + ")");
  check(orientationOk,
        label +
            ": every non-exempt triangle agrees with the polygon's "
            "orientation");
  check(coverageOk, label + ": every ring index appears in some triangle");

  delete prim;
  return triples;
}

// Runs one named canonical ring across all twelve placements (six scales,
// two planes), asserting the oracle at every placement and, when
// requested, that every placement of the same ring emits the identical
// sequence of index triples -- the invariant this whole task exists to
// establish.
void runRing(const std::string &name, const std::vector<GMANPoint> &canonical,
             bool assertTripleIdentity) {
  std::vector<std::array<int, 3>> reference;
  bool haveReference = false;

  for (double scale : kScales) {
    for (bool rotated : {false, true}) {
      char buf[256];
      std::snprintf(buf, sizeof(buf), "%s (scale=%.0e, %s)", name.c_str(),
                    scale, rotated ? "rotated" : "axis-aligned");
      std::vector<GMANPoint> ring = placeRing(canonical, scale, rotated);
      std::vector<std::array<int, 3>> triples = checkPlacement(buf, ring);

      if (!assertTripleIdentity) {
        continue;
      }
      if (!haveReference) {
        reference = triples;
        haveReference = true;
      } else {
        check(triples == reference,
              name + ": " + std::string(buf) +
                  " yields the same triangle-index triples as every other "
                  "placement of this ring");
      }
    }
  }
}

// ---- canonical rings (unit scale, z=0) ----

std::vector<GMANPoint> convexPentagon() {
  return {{0, 1, 0},
          {-0.9510565f, 0.3090170f, 0},
          {-0.5877853f, -0.8090170f, 0},
          {0.5877853f, -0.8090170f, 0},
          {0.9510565f, 0.3090170f, 0}};
}

std::vector<GMANPoint> concaveL() {
  return {{1, -1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {-1, 1, 0}, {-1, -1, 0}};
}

std::vector<GMANPoint> concaveLReversed() {
  return {{-1, -1, 0}, {-1, 1, 0}, {0, 1, 0}, {0, 0, 0}, {1, 0, 0}, {1, -1, 0}};
}

// concaveL() rotated so ring index 0 is the reflex vertex (0,0,0) -- the
// case a three-vertex cross product at indices 0,1,2 reads a normal
// opposite the true face normal, which is what newellNormal exists for.
std::vector<GMANPoint> concaveLFromReflex() {
  return {{0, 0, 0}, {0, 1, 0}, {-1, 1, 0}, {-1, -1, 0}, {1, -1, 0}, {1, 0, 0}};
}

std::vector<GMANPoint> fourPointedStar() {
  return {{1.0f, 0.0f, 0},  {0.2475f, 0.2475f, 0},
          {0.0f, 1.0f, 0},  {-0.2475f, 0.2475f, 0},
          {-1.0f, 0.0f, 0}, {-0.2475f, -0.2475f, 0},
          {0.0f, -1.0f, 0}, {0.2475f, -0.2475f, 0}};
}

// concaveL() with a vertex inserted at t=1/3 along the (1,-1,0)-(1,0,0)
// edge -- not the midpoint, which is exactly representable in binary and
// hides the defect this suite exists to catch.
std::vector<GMANPoint> concaveLCollinear() {
  return {{1, -1, 0}, {1, -1.0f + 1.0f / 3.0f, 0},
          {1, 0, 0},  {0, 0, 0},
          {0, 1, 0},  {-1, 1, 0},
          {-1, -1, 0}};
}

std::vector<GMANPoint> concaveLDuplicate() {
  return {{1, -1, 0}, {1, -1, 0}, {1, 0, 0},  {0, 0, 0},
          {0, 1, 0},  {-1, 1, 0}, {-1, -1, 0}};
}

// A rectangle (0,0)-(10,3) with four rectangular notches cut into its top
// edge, alternating eight reflex vertices (the notch floors) with convex
// ones (the notch shoulders) -- the ring measured in this task's own
// prompt as the worst-case failure under an absolute tolerance. A 2.5-unit
// margin separates the last notch from the left edge so the polygon stays
// simple: a notch wall flush with the outer boundary would make it
// self-touching.
std::vector<GMANPoint> comb() {
  std::vector<GMANPoint> ring;
  ring.push_back({0, 0, 0});
  ring.push_back({10, 0, 0});
  ring.push_back({10, 3, 0});
  const double notchRight[4] = {9.0, 7.0, 5.0, 3.0};
  const double notchLeft[4] = {8.5, 6.5, 4.5, 2.5};
  for (int i = 0; i < 4; ++i) {
    ring.push_back({(RtFloat)notchRight[i], 3, 0});
    ring.push_back({(RtFloat)notchRight[i], 1.5, 0});
    ring.push_back({(RtFloat)notchLeft[i], 1.5, 0});
    ring.push_back({(RtFloat)notchLeft[i], 3, 0});
  }
  ring.push_back({0, 3, 0});
  return ring;
}

// comb() with an extra vertex at t=1/3 down each notch wall (eight in
// total) -- inexactly collinear once scaled and rotated, sitting among
// the reflex vertices rather than replacing them.
std::vector<GMANPoint> combWithWallVertices() {
  std::vector<GMANPoint> ring;
  ring.push_back({0, 0, 0});
  ring.push_back({10, 0, 0});
  ring.push_back({10, 3, 0});
  const double notchRight[4] = {9.0, 7.0, 5.0, 3.0};
  const double notchLeft[4] = {8.5, 6.5, 4.5, 2.5};
  const double top = 3.0, bottom = 1.5;
  for (int i = 0; i < 4; ++i) {
    double r = notchRight[i], l = notchLeft[i];
    ring.push_back({(RtFloat)r, (RtFloat)top, 0});
    ring.push_back({(RtFloat)r, (RtFloat)(top - (top - bottom) / 3.0), 0});
    ring.push_back({(RtFloat)r, (RtFloat)bottom, 0});
    ring.push_back({(RtFloat)l, (RtFloat)bottom, 0});
    ring.push_back({(RtFloat)l, (RtFloat)(bottom + (top - bottom) / 3.0), 0});
    ring.push_back({(RtFloat)l, (RtFloat)top, 0});
  }
  ring.push_back({0, 3, 0});
  return ring;
}

void runCaseTable() {
  runRing("convex pentagon", convexPentagon(), true);
  runRing("concave L", concaveL(), true);
  runRing("concave L, reversed winding", concaveLReversed(), true);
  runRing("concave L starting at the reflex vertex", concaveLFromReflex(),
          true);
  runRing("4-pointed star", fourPointedStar(), true);
  runRing("concave L with a collinear vertex at t=1/3", concaveLCollinear(),
          true);
  runRing("concave L with a duplicate vertex", concaveLDuplicate(), true);
  runRing("comb", comb(), true);
  // Every placement of this ring independently satisfies the oracle --
  // count, area, orientation, coverage all hold -- but at scale=1e6,
  // rotated, one of its eight inexactly-collinear wall vertices ties
  // between two equally valid candidate ears, so this specific placement
  // picks a different (still correct) diagonal than the others. Per this
  // task's own case-table note, a tie is dropped rather than the
  // assertion weakened.
  runRing("comb with inexactly-collinear wall vertices", combWithWallVertices(),
          false);
}

// 200 star-shaped polygons -- a fixed centre, sorted random angles, random
// radii -- each with 5 to 40 vertices, every one through the oracle.
// Star-shaped-from-the-centre guarantees simple only when every consecutive
// angular gap stays under a half turn: a gap that wide lets the edge
// spanning it sweep the long way around the centre, which a plain sort of
// independently-drawn angles does not rule out (verified by construction:
// an unconstrained draw produced a genuinely self-intersecting heptagon).
// Stratified sampling -- angle i drawn from its own 1/n slice of the
// circle -- bounds every gap under 4*pi/n, safely below pi for every n in
// [5, 40], so the resulting ring is provably simple without rejecting or
// resampling. No triple-identity check here: a random ring can hold two
// candidate ears that tie, which is not a defect. std::mt19937's raw
// output is scaled by hand rather than through
// std::uniform_real_distribution, whose sequence is not specified to
// match across standard library implementations.
void runRandomizedStress() {
  std::mt19937 rng(0xA11CE5EEu);
  const double twoPi = 2.0 * M_PI;

  for (int poly = 0; poly < 200; ++poly) {
    const int n = 5 + (int)(rng() % 36);
    std::vector<double> angles(n);
    const double slice = twoPi / n;
    for (int i = 0; i < n; ++i) {
      double u = (double)rng() / (double)std::mt19937::max();
      angles[i] = (i + u) * slice;
    }

    std::vector<GMANPoint> ring(n);
    for (int i = 0; i < n; ++i) {
      double radius = 0.5 + (double)rng() / (double)std::mt19937::max() * 1.5;
      ring[i] = GMANPoint((RtFloat)(radius * std::cos(angles[i])),
                          (RtFloat)(radius * std::sin(angles[i])), 0);
    }

    checkPlacement("random star-shaped polygon #" + std::to_string(poly) +
                       " (n=" + std::to_string(n) + ")",
                   ring);
  }
}

void runDegenerateInput() {
  // All vertices collinear: getRSPolygon returns an empty stub, not a
  // crash.
  std::vector<GMANPoint> collinear = {
      {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
  GMANPrimitive *prim = runGetRSPolygon(collinear);
  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr,
        "all-collinear input: getRSPolygon returns an object");
  check(object != nullptr && object->getBody() == nullptr,
        "all-collinear input: the returned object is the empty stub");
  delete prim;

  // All vertices identical: the zero-extent case the ratio guard must not
  // divide by zero on.
  std::vector<GMANPoint> identical = {
      {5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}};
  prim = runGetRSPolygon(identical);
  object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr,
        "all-identical input: getRSPolygon returns an object");
  check(object != nullptr && object->getBody() == nullptr,
        "all-identical input: the returned object is the empty stub");
  delete prim;

  // An asymmetric bow-tie: self-intersecting, excluded by the RISpec's
  // simple-polygon contract. The only claim is that the clipAt<0 fallback
  // terminates and emits n-2 triangles -- nothing about which ones. The
  // lobes are unequal so the Newell normal does not cancel to zero, which
  // would otherwise return the empty stub before the triangulator ever
  // runs.
  std::vector<GMANPoint> bowtie = {
      {0, 0, 0}, {10, 10, 0}, {10, 0, 0}, {0, 3, 0}};
  prim = runGetRSPolygon(bowtie);
  object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr,
        "asymmetric bow-tie: getRSPolygon returns an object");
  if (object != nullptr) {
    int faces = countFaces(object);
    check(faces == (int)bowtie.size() - 2,
          "asymmetric bow-tie: the clipAt<0 fallback still emits n-2 "
          "triangles (got " +
              std::to_string(faces) + ")");
  }
  delete prim;
}

}  // namespace

int main() {
  runCaseTable();
  runRandomizedStress();
  runDegenerateInput();
  return checkSummary("triangulation holds");
}
