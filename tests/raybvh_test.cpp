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
 * GMANRayBVH, tested standalone against a hand-built
 * GMANLinearWorldManager.
 *
 * Check 1: a mixed fixture's GMANRayBVH::nearestHit and a small linear
 * scan (linearScan, below) agree on every ray -- identical primitive, t,
 * point, normal, u and v, or an identical miss. A dedicated pair
 * (testExitPruningPair) isolates the one
 * case a naive additional exit-based prune gets wrong: a box entered
 * first whose own surface is hit late, next to a box entered later whose
 * surface is hit nearer but whose own box extends past the first box's
 * hit. A fixed-seed random sweep (testRandomSweep) casts rays from every
 * octant, some with an axis-parallel (including a signed-zero) direction
 * component and random tmin/tmax, against the same fixture. A dedicated
 * pair (testCoincidentTieBothOrders) builds the coincident tie in both
 * insertion orders, since the earlier-inserted primitive is not always
 * the one a leaf happens to test first.
 *
 * Check 2: a second, one-axis fixture's primitiveTests counter matches
 * the leaf size or less for a targeted ray, exactly 0 for a ray whose
 * tmax ends short of every primitive, and the leaf size or less again for
 * a perpendicular ray -- the case a poor split axis actually exposes.
 *
 * Check 4: on a fixture the object-median split serves badly (wall-sized
 * polygons around a cluster of small polygons packed in one corner),
 * the surface-area build keeps the total boxTests of a fixed ray set
 * under a bound (testSurfaceAreaFewerBoxTests), and every hit still
 * matches the linear scan. The bound sits between this build's total
 * and the object-median build's.
 *
 * Check 5: a fixture of 120 nested plates, on alternating sides, makes
 * an unbounded surface-area build peel one plate per level to a depth
 * past the traversal stack; treeDepth() must stay below the width of
 * std::size_t (testDepthFitsTheStack), with every ray along the axis
 * still matching the linear scan.
 *
 * testSignedZeroFacePlane probes GMANRayBVH::nearestHit's own box test
 * with a direction component of +0.0 and of -0.0, origin on each face in
 * turn: a leaf's own primitive must still be tested regardless of the
 * zero's sign.
 *
 * testCoincidentCentroids: nine concentric spheres share one centroid,
 * leaving the bins no extent to span, so the build halves them at the
 * object median, to a depth of exactly 2.
 *
 * testBuildIsDeterministic: two builds of check 1's fixture agree on
 * depth and on every ray's box and primitive tests.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanlinearworldmanager.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "maketransform.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

constexpr RtFloat kRoomHalfSize = 100.0f;
constexpr std::size_t kBoxTestBound = 7300;
constexpr std::ptrdiff_t kWallCount = 6;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANTransform translated(RtFloat dx, RtFloat dy, RtFloat dz) {
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  return makeTransform(m);
}

// GMANRayBVH::nearestHit's own contract, written independently of it: the
// nearest hit within ray's [tmin, tmax], the earlier-inserted primitive
// winning an exact t tie.
bool linearScan(std::vector<GMANRayInterface*> const& prims, GMANRay const& ray, GMANHit& hit,
                GMANRayInterface const*& hitPrimitive) {
  bool found = false;
  for (GMANRayInterface* p : prims) {
    GMANHit candidate;
    if (p->intersect(ray, candidate) && (!found || candidate.t < hit.t)) {
      hit = candidate;
      hitPrimitive = p;
      found = true;
    }
  }
  return found;
}

bool hitsMatch(bool foundBvh, GMANHit const& bvhHit, GMANRayInterface const* bvhPrim, bool foundLinear,
               GMANHit const& linearHit, GMANRayInterface const* linearPrim) {
  if (foundBvh != foundLinear) {
    return false;
  }
  if (!foundBvh) {
    return true;
  }
  if (bvhPrim != linearPrim) {
    return false;
  }
  return near(bvhHit.t, linearHit.t) && near(bvhHit.point.getX(), linearHit.point.getX()) &&
         near(bvhHit.point.getY(), linearHit.point.getY()) && near(bvhHit.point.getZ(), linearHit.point.getZ()) &&
         near(bvhHit.normal.getX(), linearHit.normal.getX()) && near(bvhHit.normal.getY(), linearHit.normal.getY()) &&
         near(bvhHit.normal.getZ(), linearHit.normal.getZ()) && near(bvhHit.u, linearHit.u) &&
         near(bvhHit.v, linearHit.v);
}

// Casts ray against both bvh and prims (linearScan), asserting they agree
// exactly.
void checkAgreement(GMANRayBVH const& bvh, std::vector<GMANRayInterface*> const& prims, GMANRay const& ray,
                    std::string const& what) {
  GMANHit bvhHit;
  GMANRayInterface const* bvhPrim = nullptr;
  bool const foundBvh = bvh.nearestHit(ray, bvhHit, bvhPrim);

  GMANHit linearHit;
  GMANRayInterface const* linearPrim = nullptr;
  bool const foundLinear = linearScan(prims, ray, linearHit, linearPrim);

  check(hitsMatch(foundBvh, bvhHit, bvhPrim, foundLinear, linearHit, linearPrim),
        "check 1: " + what + " -- GMANRayBVH::nearestHit matches the linear scan");
}

// ---- check 1 ----
//
// Builds a mixed, >=16-primitive fixture into worldManager/prims (both
// out-parameters, appended in insertion order) and returns the handful of
// primitives the special-case rays below target. swapOverlap reverses
// just the overlapping pair's own insertion order, leaving every other
// primitive's order fixed, so the fixture is built twice to catch an
// order dependency.
struct FixtureHandles {
  GMANRayInterface* overlapNear = nullptr;
  GMANRayInterface* overlapFar = nullptr;
  GMANRayInterface* coincidentEarlier = nullptr;
  GMANRayInterface* coincidentLater = nullptr;
  GMANRayInterface* boxDecoyDisk = nullptr;
  GMANRayInterface* trueTarget = nullptr;
};

FixtureHandles buildCheck1Fixture(bool swapOverlap, GMANLinearWorldManager& worldManager,
                                  std::vector<GMANRayInterface*>& prims, bool includeSingular = true) {
  auto add = [&](GMANRayInterface* p) {
    worldManager.add(p);
    prims.push_back(p);
  };

  FixtureHandles handles;

  // The overlapping pair: two spheres on the camera-space z axis, boxes
  // overlapping along a ray down it, nearest at z == 10. Built in one
  // order or the other depending on swapOverlap.
  auto* nearSphere = new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 10.0));
  auto* farSphere = new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 15.0));
  handles.overlapNear = nearSphere;
  handles.overlapFar = farSphere;
  if (swapOverlap) {
    add(farSphere);
    add(nearSphere);
  } else {
    add(nearSphere);
    add(farSphere);
  }

  // A coincident-surface pair: two identical disks at the same place, so
  // a ray straight down the axis ties in t exactly. Off the z-axis
  // sweep's own x/y range (x == 200) so no other primitive blocks it.
  auto* diskA = new GMANRayDisk(40.0, 3.0, 360.0, GMANParameterList(), translated(200.0, 0.0, 0.0));
  auto* diskB = new GMANRayDisk(40.0, 3.0, 360.0, GMANParameterList(), translated(200.0, 0.0, 0.0));
  handles.coincidentEarlier = diskA;
  handles.coincidentLater = diskB;
  add(diskA);
  add(diskB);

  // A singular-transform primitive mixed among valid ones: never hits,
  // whatever ray reaches it.
  if (includeSingular) {
    GMANMatrix4 singularMatrix;
    singularMatrix.scale(1.0, 1.0, 0.0);
    add(new GMANRayCone(2.0, 1.0, 360.0, GMANParameterList(), makeTransform(singularMatrix)));
  }

  // A ray whose true nearest hit lies behind a primitive whose bbox it
  // also enters: boxDecoyDisk's own box is a square (padded) around its
  // circular rim, so a ray through the square's corner, outside the
  // circle, clears the disk's own intersect() but still tests its box.
  // trueTarget sits on that same line, farther along.
  auto* decoyDisk = new GMANRayDisk(20.0, 1.0, 360.0, GMANParameterList(), GMANTransform());
  auto* target = new GMANRaySphere(0.5, -0.5, 0.5, 360.0, GMANParameterList(), translated(0.95, 0.95, 25.0));
  handles.boxDecoyDisk = decoyDisk;
  handles.trueTarget = target;
  add(decoyDisk);
  add(target);

  // Filler primitives, several other types, spread far off to the side
  // (x >= 50) so none of the rays above ever reach them; only here to
  // push the fixture past 4x the leaf size and to give the tree real
  // structure to prune.
  add(new GMANRayCylinder(1.0, 0.0, 5.0, 360.0, GMANParameterList(), translated(50.0, 0.0, 0.0)));
  add(new GMANRayCone(2.0, 1.0, 360.0, GMANParameterList(), translated(60.0, 0.0, 0.0)));
  RtPoint p1 = {0.0, 0.0, -1.0};
  RtPoint p2 = {1.0, 0.0, 1.0};
  add(new GMANRayHyperboloid(p1, p2, 360.0, GMANParameterList(), translated(70.0, 0.0, 0.0)));
  add(new GMANRayParaboloid(1.0, 0.0, 2.0, 360.0, GMANParameterList(), translated(80.0, 0.0, 0.0)));
  add(new GMANRayTorus(2.0, 0.5, -180.0, 180.0, 360.0, GMANParameterList(), translated(90.0, 0.0, 0.0)));
  std::vector<GMANPoint> const quad = {
      GMANPoint(99.0, -1.0, -1.0),
      GMANPoint(101.0, -1.0, -1.0),
      GMANPoint(101.0, 1.0, -1.0),
      GMANPoint(99.0, 1.0, -1.0),
  };
  add(new GMANRayPolygon(quad, GMANParameterList()));
  add(new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(110.0, 0.0, 0.0)));
  add(new GMANRayDisk(0.0, 1.0, 360.0, GMANParameterList(), translated(120.0, 0.0, 0.0)));
  add(new GMANRayCylinder(1.0, 0.0, 3.0, 360.0, GMANParameterList(), translated(130.0, 0.0, 0.0)));
  add(new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(140.0, 0.0, 0.0)));

  return handles;
}

void runCheck1(bool swapOverlap) {
  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims;
  FixtureHandles const handles = buildCheck1Fixture(swapOverlap, worldManager, prims);
  check(prims.size() >= 16, "check 1: the fixture holds at least 4x the leaf size");

  GMANRayBVH bvh;
  bvh.build(worldManager);

  std::string const label = swapOverlap ? " (overlap pair swapped)" : "";

  // A wide, systematic sweep: a grid of parallel rays down +z, covering
  // clean hits (the overlap pair), clean misses (the grid's corners) and
  // grazing near-misses (just past the spheres' own radius) alike.
  constexpr int kGridSteps = 12;
  constexpr RtFloat kGridExtent = 2.0f;
  for (int ix = -kGridSteps; ix <= kGridSteps; ++ix) {
    for (int iy = -kGridSteps; iy <= kGridSteps; ++iy) {
      RtFloat const x = kGridExtent * (RtFloat)ix / (RtFloat)kGridSteps;
      RtFloat const y = kGridExtent * (RtFloat)iy / (RtFloat)kGridSteps;
      GMANRay const ray(GMANPoint(x, y, -5.0), GMANVector(0.0, 0.0, 1.0));
      checkAgreement(bvh, prims, ray, "grid ray (" + std::to_string(x) + ", " + std::to_string(y) + ")" + label);
    }
  }

  // A clean miss: nothing sits anywhere near this ray's path.
  checkAgreement(bvh, prims, GMANRay(GMANPoint(0.0, 1000.0, 0.0), GMANVector(1.0, 0.0, 0.0)), "far clean miss" + label);

  // The decoy-then-target ray: its bbox the ray enters, its surface the
  // ray does not, with the true nearest hit farther along.
  checkAgreement(bvh, prims, GMANRay(GMANPoint(0.95, 0.95, -5.0), GMANVector(0.0, 0.0, 1.0)),
                 "box decoy then true target" + label);

  // An axis-aligned ray whose origin lies exactly on a box-face plane:
  // queries overlapNear's own actual bbox (already padded) so the
  // equality is exact regardless of the pad's own formula. Coverage for
  // the slab test's NaN branch, not a discriminating check on its own.
  GMANPoint const facePlane = handles.overlapNear->getBBox().getMax();
  checkAgreement(bvh, prims, GMANRay(GMANPoint(facePlane.getX(), 0.0, -5.0), GMANVector(0.0, 0.0, 1.0)),
                 "origin on a box-face plane" + label);

  // An interval-limited ray: tmax ends before the overlap pair, tmin > 0.
  checkAgreement(bvh, prims, GMANRay(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0), 1.0, 8.0),
                 "interval-limited ray" + label);

  // The shadow-ray case: origin inside the fixture (inside overlapNear's
  // own sphere).
  checkAgreement(bvh, prims, GMANRay(GMANPoint(0.0, 0.0, 10.0), GMANVector(0.0, 0.0, 1.0)),
                 "origin inside the fixture" + label);

  // The coincident tie: resolves to the earlier-inserted disk, matching
  // both the linear scan's own tie-break and the BVH's.
  GMANRay const tieRay(GMANPoint(200.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  checkAgreement(bvh, prims, tieRay, "coincident tie" + label);
  GMANHit tieHit;
  GMANRayInterface const* tiePrim = nullptr;
  bvh.nearestHit(tieRay, tieHit, tiePrim);
  check(tiePrim == handles.coincidentEarlier,
        "check 1: the coincident tie resolves to the earlier-inserted disk" + label);
}

void testCheck1() {
  runCheck1(false);
  runCheck1(true);
}

// ---- an additional exit-based prune, past the settled traversal rule,
// gets one case wrong: a box entered first (A, entry 20) whose own
// surface is hit late (24.005), next to a box entered later (B, entry
// 22, still short of A's own hit) whose surface is hit nearer (23.451)
// but whose own box extends past A's hit (exit 40, unclamped). Once A
// sets the running best to 24.005, B's own box clamps its exit to
// exactly that limit; "exit >= hit.t" then reads as true and would skip
// B's leaf outright, even though B's entry is still in range and its
// surface is the true nearest. Each of A and B carries three small
// spheres spread through its own box, off the ray's path, so the
// surface-area split parts them into two four-primitive leaves rather
// than grouping the two large spheres together. ----
void testExitPruningPair() {
  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims;
  auto add = [&](GMANRayInterface* p) {
    worldManager.add(p);
    prims.push_back(p);
  };
  auto addInside = [&](RtFloat x, RtFloat y, RtFloat z) {
    add(new GMANRaySphere(0.5, -0.5, 0.5, 360.0, GMANParameterList(), translated(x, y, z)));
  };

  auto* primA = new GMANRaySphere(5.0, -5.0, 5.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 20.0));
  auto* primB = new GMANRaySphere(9.0, -9.0, 9.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 26.0));
  add(primA);
  add(primB);
  addInside(4.0, 4.0, 19.0);
  addInside(-4.0, -4.0, 20.0);
  addInside(4.0, -4.0, 21.0);
  addInside(8.0, 8.0, 25.0);
  addInside(-8.0, -8.0, 26.0);
  addInside(8.0, -8.0, 27.0);

  GMANRayBVH bvh;
  bvh.build(worldManager);

  GMANRay const ray(GMANPoint(4.9, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  checkAgreement(bvh, prims, ray, "exit-pruning pair");

  GMANHit hit;
  GMANRayInterface const* prim = nullptr;
  std::size_t primitiveTests = 0;
  bool const found = bvh.nearestHit(ray, hit, prim, &primitiveTests);
  check(found && prim == primB,
        "exit-pruning pair: the nearer sphere (B) wins, not the one entered first but hit late (A)");
  check(primitiveTests == 8, "exit-pruning pair: A and B sit in separate four-primitive leaves, both visited");
}

std::size_t maxNodeCount(std::size_t primitiveCount) { return 2 * primitiveCount - 1; }

bool missesBox(GMANRay const& ray, GMANPoint const& boxMin, GMANPoint const& boxMax) {
  RtFloat const origin[3] = {ray.getOrigin().getX(), ray.getOrigin().getY(), ray.getOrigin().getZ()};
  RtFloat const direction[3] = {ray.getDirection().getX(), ray.getDirection().getY(), ray.getDirection().getZ()};
  RtFloat const lo[3] = {boxMin.getX(), boxMin.getY(), boxMin.getZ()};
  RtFloat const hi[3] = {boxMax.getX(), boxMax.getY(), boxMax.getZ()};
  RtFloat nearT = ray.getTMin();
  RtFloat farT = ray.getTMax();
  for (int axis = 0; axis < 3; ++axis) {
    if (direction[axis] == 0.0f) {
      if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
        return true;
      }
      continue;
    }
    RtFloat const a = (lo[axis] - origin[axis]) / direction[axis];
    RtFloat const b = (hi[axis] - origin[axis]) / direction[axis];
    nearT = std::max(nearT, std::min(a, b));
    farT = std::min(farT, std::max(a, b));
  }
  return nearT > farT;
}

void unionBounds(std::vector<GMANRayInterface*> const& prims, GMANPoint& boxMin, GMANPoint& boxMax) {
  boxMin = prims.front()->getBBox().getMin();
  boxMax = prims.front()->getBBox().getMax();
  for (GMANRayInterface const* p : prims) {
    GMANPoint const lo = p->getBBox().getMin();
    GMANPoint const hi = p->getBBox().getMax();
    boxMin = GMANPoint(std::min(boxMin.getX(), lo.getX()), std::min(boxMin.getY(), lo.getY()),
                       std::min(boxMin.getZ(), lo.getZ()));
    boxMax = GMANPoint(std::max(boxMax.getX(), hi.getX()), std::max(boxMax.getY(), hi.getY()),
                       std::max(boxMax.getZ(), hi.getZ()));
  }
}

bool sameHit(GMANHit const& a, GMANHit const& b) {
  return a.t == b.t && a.point.getX() == b.point.getX() && a.point.getY() == b.point.getY() &&
         a.point.getZ() == b.point.getZ() && a.normal.getX() == b.normal.getX() && a.normal.getY() == b.normal.getY() &&
         a.normal.getZ() == b.normal.getZ() && a.u == b.u && a.v == b.v;
}

struct BoxTestBounds {
  std::size_t nodeCount = 0;
  GMANPoint rootMin;
  GMANPoint rootMax;
};

bool checkTraversalCounts(GMANRayBVH const& bvh, BoxTestBounds const& bounds, GMANRay const& ray,
                          std::string const& what) {
  GMANHit fourHit;
  GMANRayInterface const* fourPrim = nullptr;
  std::size_t fourPrimitiveTests = 0;
  bool const fourFound = bvh.nearestHit(ray, fourHit, fourPrim, &fourPrimitiveTests);

  GMANHit fiveHit;
  GMANRayInterface const* fivePrim = nullptr;
  std::size_t fivePrimitiveTests = 0;
  std::size_t boxTests = 0;
  bool const fiveFound = bvh.nearestHit(ray, fiveHit, fivePrim, &fivePrimitiveTests, &boxTests);

  check(fourFound == fiveFound && (!fourFound || (fourPrim == fivePrim && sameHit(fourHit, fiveHit))) &&
            fourPrimitiveTests == fivePrimitiveTests,
        "check 3: " + what + " -- the two nearestHit overloads agree on hit, primitive and primitiveTests");
  check(boxTests >= 1 && boxTests <= bounds.nodeCount,
        "check 3: " + what + " -- boxTests is at least the root's and at most the node count");

  bool const missesRoot = missesBox(ray, bounds.rootMin, bounds.rootMax);
  if (missesRoot) {
    check(boxTests == 1, "check 3: " + what + " -- a ray that misses the root's box costs exactly one box test");
  }
  return missesRoot;
}

// ---- a fixed-seed sweep of random rays -- every octant of direction and
// origin, some with an axis-parallel component (including a signed
// zero), random tmin and a random or infinite tmax -- against check 1's
// own mixed fixture, catching what a hand-picked ray set might miss:
// among others, a slab test that skips std::swap(t0, t1) still passes a
// positive-direction ray on every axis this fixture's own hand-picked
// rays travel, since a positive invD never needs the swap. ----
void runRandomSweep(bool includeSingular) {
  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims;
  buildCheck1Fixture(false, worldManager, prims, includeSingular);

  GMANRayBVH bvh;
  bvh.build(worldManager);

  BoxTestBounds bounds;
  bounds.nodeCount = maxNodeCount(prims.size());
  unionBounds(prims, bounds.rootMin, bounds.rootMax);
  int rootMisses = 0;

  std::mt19937 rng(0xC0FFEEu); // fixed seed: this sweep is reproducible, not re-derived per run
  std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);
  std::uniform_real_distribution<float> originDist(-10.0f, 30.0f);
  std::uniform_real_distribution<float> tminDist(0.0f, 2.0f);
  std::uniform_real_distribution<float> tmaxSpanDist(5.0f, 60.0f);

  constexpr int kSweepCount = 3000;
  for (int i = 0; i < kSweepCount; ++i) {
    RtFloat dx = dirDist(rng);
    RtFloat dy = dirDist(rng);
    RtFloat dz = dirDist(rng);
    switch (i % 8) {
    case 0:
      dx = 0.0f;
      break;
    case 1:
      dy = 0.0f;
      break;
    case 2:
      dz = 0.0f;
      break;
    case 3:
      dx = -0.0f;
      break;
    default:
      break; // the remaining four eighths keep a fully random direction
    }
    if (dx == 0.0f && dy == 0.0f && dz == 0.0f) {
      dz = 1.0f; // never hand GMANRay a zero-length direction
    }

    GMANPoint const origin(originDist(rng), originDist(rng), originDist(rng));
    GMANVector const direction(dx, dy, dz);
    RtFloat const tmin = tminDist(rng);
    bool const bounded = (i % 3 == 0);
    RtFloat const tmax = bounded ? tmin + tmaxSpanDist(rng) : RI_INFINITY;

    GMANRay const ray(origin, direction, tmin, tmax);
    std::string const what =
        std::string("random sweep ray ") + std::to_string(i) + (includeSingular ? "" : " (finite)");
    checkAgreement(bvh, prims, ray, what);
    if (checkTraversalCounts(bvh, bounds, ray, what)) {
      ++rootMisses;
    }
  }
  if (includeSingular) {
    return;
  }

  std::printf("check 3: %d of %d sweep rays miss the finite fixture's root box\n", rootMisses, kSweepCount);
  check(rootMisses > 0, "check 3: at least one sweep ray misses the finite fixture's root box");

  GMANRay const farMiss(GMANPoint(0.0, 1000.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  check(checkTraversalCounts(bvh, bounds, farMiss, "far clean miss"),
        "check 3: the far clean miss misses the finite fixture's root box");
}

void testRandomSweep() {
  runRandomSweep(true);
  runRandomSweep(false);
}

// ---- the coincident tie resolves to the earlier-inserted disk in
// either insertion order -- a leaf's own internal test order need not
// match insertion order, so testing only one leaves the other silent ----
void testCoincidentTieBothOrders() {
  for (bool swapOrder : {false, true}) {
    GMANLinearWorldManager worldManager;
    auto* diskA = new GMANRayDisk(40.0, 3.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 0.0));
    auto* diskB = new GMANRayDisk(40.0, 3.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 0.0));
    GMANRayInterface* first = swapOrder ? static_cast<GMANRayInterface*>(diskB) : static_cast<GMANRayInterface*>(diskA);
    GMANRayInterface* second =
        swapOrder ? static_cast<GMANRayInterface*>(diskA) : static_cast<GMANRayInterface*>(diskB);
    worldManager.add(first);
    worldManager.add(second);

    GMANRayBVH bvh;
    bvh.build(worldManager);

    GMANRay const ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    bool const found = bvh.nearestHit(ray, hit, prim);
    check(found && prim == first, std::string("tie: resolves to the earlier-inserted disk (") +
                                      (swapOrder ? "B inserted first" : "A inserted first") + ")");
  }
}

void testBoxTestsPerNode() {
  constexpr std::size_t kCount = 5;
  constexpr RtFloat kSpacing = 3.0f;
  constexpr RtFloat kRadius = 0.4f;

  GMANLinearWorldManager worldManager;
  for (std::size_t i = 0; i < kCount; ++i) {
    worldManager.add(new GMANRaySphere(kRadius, -kRadius, kRadius, 360.0, GMANParameterList(),
                                       translated((RtFloat)i * kSpacing, 0.0, 0.0)));
  }

  GMANRayBVH bvh;
  bvh.build(worldManager);
  check(bvh.treeDepth() == 1, "box tests: five primitives make a root and two leaves");

  GMANRay const ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;
  GMANRayInterface const* prim = nullptr;
  std::size_t primitiveTests = 0;
  std::size_t boxTests = 0;
  bool const found = bvh.nearestHit(ray, hit, prim, &primitiveTests, &boxTests);
  check(found, "box tests: the along-row ray hits");
  std::printf("box tests: five-primitive tree boxTests=%zu (expect 3) primitiveTests=%zu\n", boxTests, primitiveTests);
  check(boxTests == 3, "box tests: a ray entering both leaves tests the root and each leaf once");
}

void addSpheresInPlane(GMANLinearWorldManager& worldManager, std::initializer_list<RtFloat> const& xs) {
  for (RtFloat const x : xs) {
    worldManager.add(new GMANRaySphere(0.4, -0.4, 0.4, 360.0, GMANParameterList(), translated(x, 0.0, 0.0)));
  }
}

std::vector<GMANPoint> quadInPlane(RtFloat minX, RtFloat maxX) {
  return {GMANPoint(minX, -1.0, 0.0), GMANPoint(maxX, -1.0, 0.0), GMANPoint(maxX, 1.0, 0.0), GMANPoint(minX, 1.0, 0.0)};
}

void testTieAtClampedEntry() {
  constexpr RtFloat kOriginZ = -5.0f;
  RtFloat const hitT = -kOriginZ;

  for (bool swapOrder : {false, true}) {
    GMANLinearWorldManager worldManager;
    auto* leftPolygon = new GMANRayPolygon(quadInPlane(-12.0, 1.0), GMANParameterList());
    auto* rightPolygon = new GMANRayPolygon(quadInPlane(-1.0, 12.0), GMANParameterList());

    addSpheresInPlane(worldManager, {-9.0, -6.0});
    GMANRayInterface* first = swapOrder ? static_cast<GMANRayInterface*>(rightPolygon) : leftPolygon;
    GMANRayInterface* second = swapOrder ? static_cast<GMANRayInterface*>(leftPolygon) : rightPolygon;
    worldManager.add(first);
    worldManager.add(second);
    addSpheresInPlane(worldManager, {6.0, 9.0});

    GMANRayBVH bvh;
    bvh.build(worldManager);

    GMANRay const ray(GMANPoint(0.0, 0.0, kOriginZ), GMANVector(0.0, 0.0, 1.0), hitT, RI_INFINITY);
    GMANHit leftHit;
    GMANHit rightHit;
    check(leftPolygon->intersect(ray, leftHit) && rightPolygon->intersect(ray, rightHit) && leftHit.t == hitT &&
              rightHit.t == hitT,
          "clamped-entry tie: both polygons hit at exactly tmin");

    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    std::size_t primitiveTests = 0;
    bool const found = bvh.nearestHit(ray, hit, prim, &primitiveTests);
    std::string const order = swapOrder ? "right polygon inserted first" : "left polygon inserted first";
    check(found && prim == first, "clamped-entry tie: resolves to the earlier-inserted polygon (" + order + ")");
    check(primitiveTests == 6,
          "clamped-entry tie: both three-primitive leaves are visited, so the tie is decided by insertion index (" +
              order + ")");
  }
}

void testTieUnderBestHitLimit() {
  constexpr RtFloat kOriginZ = -5.0f;
  RtFloat const hitT = -kOriginZ;

  for (bool swapOrder : {false, true}) {
    GMANLinearWorldManager worldManager;
    auto* leftPolygon = new GMANRayPolygon(quadInPlane(-12.0, 1.0), GMANParameterList());
    auto* rightPolygon = new GMANRayPolygon(quadInPlane(-1.0, 12.0), GMANParameterList());
    GMANRayInterface* first = swapOrder ? static_cast<GMANRayInterface*>(rightPolygon) : leftPolygon;
    GMANRayInterface* second = swapOrder ? static_cast<GMANRayInterface*>(leftPolygon) : rightPolygon;

    addSpheresInPlane(worldManager, {-9.0, -6.0});
    worldManager.add(first);
    worldManager.add(second);
    addSpheresInPlane(worldManager, {6.0, 9.0, 10.0, 11.0});

    GMANRayBVH bvh;
    bvh.build(worldManager);

    GMANRay const ray(GMANPoint(0.0, 0.0, kOriginZ), GMANVector(0.0, 0.0, 1.0), hitT, RI_INFINITY);
    GMANHit leftHit;
    GMANHit rightHit;
    check(leftPolygon->intersect(ray, leftHit) && rightPolygon->intersect(ray, rightHit) && leftHit.t == hitT &&
              rightHit.t == hitT,
          "push-time tie: both polygons hit at exactly tmin");

    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    std::size_t primitiveTests = 0;
    bool const found = bvh.nearestHit(ray, hit, prim, &primitiveTests);
    std::string const order = swapOrder ? "right polygon inserted first" : "left polygon inserted first";
    check(found && prim == first, "push-time tie: resolves to the earlier-inserted polygon (" + order + ")");
    check(primitiveTests == 4,
          "push-time tie: the first polygon's three-primitive leaf and the second's own are visited, and no other (" +
              order + ")");
  }
}

std::vector<GMANPoint> unitSquareAt(RtFloat centreX, RtFloat centreY) {
  return {GMANPoint(centreX - 0.5, centreY - 0.5, 0.0), GMANPoint(centreX + 0.5, centreY - 0.5, 0.0),
          GMANPoint(centreX + 0.5, centreY + 0.5, 0.0), GMANPoint(centreX - 0.5, centreY + 0.5, 0.0)};
}

std::size_t primitiveTestsAlongZ(GMANRayBVH const& bvh, RtFloat x, RtFloat y) {
  GMANRay const ray(GMANPoint(x, y, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  GMANRayInterface const* prim = nullptr;
  std::size_t primitiveTests = 0;
  check(bvh.nearestHit(ray, hit, prim, &primitiveTests), "tie rule: the ray hits a quad");
  return primitiveTests;
}

void testTieWithinAxisTakesLowestPlane() {
  GMANLinearWorldManager worldManager;
  for (RtFloat const x : {-4.0f, -2.0f, 0.0f, 2.0f, 4.0f}) {
    worldManager.add(new GMANRayPolygon(quadInPlane(x - 0.5f, x + 0.5f), GMANParameterList()));
  }

  GMANRayBVH bvh;
  bvh.build(worldManager);
  check(primitiveTestsAlongZ(bvh, -4.0f, 0.0f) == 2,
        "tie within an axis: the lowest of the equally cheap planes splits two quads from three");
}

void testTieAcrossAxesTakesLowestAxis() {
  GMANLinearWorldManager worldManager;
  for (auto const& centre :
       {std::pair<RtFloat, RtFloat>{-2.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, -2.0f}, {0.0f, 2.0f}, {0.0f, 0.0f}}) {
    worldManager.add(new GMANRayPolygon(unitSquareAt(centre.first, centre.second), GMANParameterList()));
  }

  GMANRayBVH bvh;
  bvh.build(worldManager);
  check(primitiveTestsAlongZ(bvh, -2.0f, 0.0f) == 1,
        "tie across axes: the x sweep, the lower axis, isolates the quad at x = -2 in a leaf of one");
}

void addWall(GMANLinearWorldManager& worldManager, std::vector<GMANRayInterface*>& prims, int axis, RtFloat side) {
  RtFloat const h = kRoomHalfSize;
  auto corner = [&](RtFloat u, RtFloat v) {
    RtFloat c[3];
    int const a1 = (axis + 1) % 3;
    int const a2 = (axis + 2) % 3;
    c[axis] = side * h;
    c[a1] = u;
    c[a2] = v;
    return GMANPoint(c[0], c[1], c[2]);
  };
  std::vector<GMANPoint> const quad = {corner(-h, -h), corner(h, -h), corner(h, h), corner(-h, h)};
  auto* wall = new GMANRayPolygon(quad, GMANParameterList());
  worldManager.add(wall);
  prims.push_back(wall);
}

void addCornerCluster(GMANLinearWorldManager& worldManager, std::vector<GMANRayInterface*>& prims) {
  constexpr int kSide = 7;
  constexpr RtFloat kPitch = 1.5f;
  constexpr RtFloat kHalf = 0.4f;
  for (int ix = 0; ix < kSide; ++ix) {
    for (int iy = 0; iy < kSide; ++iy) {
      for (int iz = 0; iz < kSide; ++iz) {
        RtFloat const x = 60.0f + kPitch * (RtFloat)ix + 0.013f * (RtFloat)iy + 0.0071f * (RtFloat)iz;
        RtFloat const y = 60.0f + kPitch * (RtFloat)iy + 0.011f * (RtFloat)iz + 0.0059f * (RtFloat)ix;
        RtFloat const z = 60.0f + kPitch * (RtFloat)iz + 0.017f * (RtFloat)ix + 0.0047f * (RtFloat)iy;
        int const plane = (ix + iy + iz) % 3;
        std::vector<GMANPoint> quad;
        if (plane == 0) {
          quad = {GMANPoint(x - kHalf, y - kHalf, z), GMANPoint(x + kHalf, y - kHalf, z),
                  GMANPoint(x + kHalf, y + kHalf, z), GMANPoint(x - kHalf, y + kHalf, z)};
        } else if (plane == 1) {
          quad = {GMANPoint(x, y - kHalf, z - kHalf), GMANPoint(x, y + kHalf, z - kHalf),
                  GMANPoint(x, y + kHalf, z + kHalf), GMANPoint(x, y - kHalf, z + kHalf)};
        } else {
          quad = {GMANPoint(x - kHalf, y, z - kHalf), GMANPoint(x + kHalf, y, z - kHalf),
                  GMANPoint(x + kHalf, y, z + kHalf), GMANPoint(x - kHalf, y, z + kHalf)};
        }
        auto* polygon = new GMANRayPolygon(quad, GMANParameterList());
        worldManager.add(polygon);
        prims.push_back(polygon);
      }
    }
  }
}

void testSurfaceAreaFewerBoxTests() {
  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims;
  for (int axis = 0; axis < 3; ++axis) {
    addWall(worldManager, prims, axis, -1.0f);
    addWall(worldManager, prims, axis, 1.0f);
  }
  addCornerCluster(worldManager, prims);

  GMANRayBVH bvh;
  bvh.build(worldManager);

  std::size_t boxTests = 0;
  std::size_t clusterHits = 0;
  constexpr int kRaysPerAxis = 6;
  for (int i = 0; i < kRaysPerAxis; ++i) {
    for (int j = 0; j < kRaysPerAxis; ++j) {
      for (int k = 0; k < kRaysPerAxis; ++k) {
        GMANPoint const origin(-60.0 + 7.0 * i, -60.0 + 9.0 * j, -60.0 + 11.0 * k);
        GMANPoint const target(60.0 + 1.5 * ((i * 3 + j) % 7) + 0.3, 60.0 + 1.5 * ((j * 2 + k) % 7) + 0.2,
                               60.0 + 1.5 * ((k * 5 + i) % 7) + 0.1);
        GMANRay const ray(origin, GMANVector(target.getX() - origin.getX(), target.getY() - origin.getY(),
                                             target.getZ() - origin.getZ()));
        GMANHit hit;
        GMANRayInterface const* prim = nullptr;
        std::size_t primitiveTests = 0;
        bool const found = bvh.nearestHit(ray, hit, prim, &primitiveTests, &boxTests);
        GMANHit linearHit;
        GMANRayInterface const* linearPrim = nullptr;
        bool const foundLinear = linearScan(prims, ray, linearHit, linearPrim);
        clusterHits += found && std::find(prims.begin() + kWallCount, prims.end(), prim) != prims.end();
        check(hitsMatch(found, hit, prim, foundLinear, linearHit, linearPrim),
              "surface area: a ray at the corner cluster matches the linear scan");
      }
    }
  }
  check(clusterHits > 0, "surface area: the rays reach the corner cluster");
  check(boxTests <= kBoxTestBound, "surface area: the rays at the corner cluster test few boxes");
}

void testDepthFitsTheStack() {
  constexpr int kPlateCount = 120;
  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims;
  for (int j = 0; j < kPlateCount; ++j) {
    RtFloat const x = (j % 2 == 0 ? 1.0f : -1.0f) * std::pow(0.9f, (RtFloat)j);
    RtFloat const half = 100.0f * std::pow(0.999f, (RtFloat)j);
    std::vector<GMANPoint> const plate = {GMANPoint(x, -half, -half), GMANPoint(x, half, -half),
                                          GMANPoint(x, half, half), GMANPoint(x, -half, half)};
    auto* polygon = new GMANRayPolygon(plate, GMANParameterList());
    worldManager.add(polygon);
    prims.push_back(polygon);
  }

  GMANRayBVH bvh;
  bvh.build(worldManager);
  check(bvh.treeDepth() < (std::size_t)std::numeric_limits<std::size_t>::digits,
        "depth: a tree over nested plates stays inside the traversal stack");

  for (RtFloat const y : {0.0f, 3.0f, -40.0f}) {
    checkAgreement(bvh, prims, GMANRay(GMANPoint(-5.0, y, 0.0), GMANVector(1.0, 0.0, 0.0)),
                   "nested plates from the near end, y = " + std::to_string(y));
    checkAgreement(bvh, prims, GMANRay(GMANPoint(5.0, y, 0.0), GMANVector(-1.0, 0.0, 0.0)),
                   "nested plates from the far end, y = " + std::to_string(y));
  }
}

// ---- check 2 ----
void testCheck2() {
  constexpr int kCount = 16; // 4x the leaf size
  constexpr RtFloat kSpacing = 3.0f;
  constexpr RtFloat kRadius = 0.4f;

  // A fixed-seed shuffle of insertion order -- a permanent property of
  // this fixture, hardcoded rather than re-derived per run.
  int const insertionOrder[kCount] = {7, 2, 13, 0, 9, 4, 11, 15, 1, 8, 5, 14, 3, 10, 6, 12};

  GMANLinearWorldManager worldManager;
  GMANRayInterface* bySpatialIndex[kCount];
  for (int slot = 0; slot < kCount; ++slot) {
    int const spatialIndex = insertionOrder[slot];
    RtFloat const x = (RtFloat)spatialIndex * kSpacing;
    auto* sphere = new GMANRaySphere(kRadius, -kRadius, kRadius, 360.0, GMANParameterList(), translated(x, 0.0, 0.0));
    worldManager.add(sphere);
    bySpatialIndex[spatialIndex] = sphere;
  }

  GMANRayBVH bvh;
  bvh.build(worldManager);

  // Query 1: aimed at the row's near end (spatial index 0), direction
  // along the row.
  {
    GMANRay const ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
    std::size_t tests = 0;
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    bool const found = bvh.nearestHit(ray, hit, prim, &tests);
    check(found && prim == bySpatialIndex[0], "check 2: the along-row ray hits the near-end primitive");
    std::printf("check 2: along-row ray primitiveTests=%zu (leaf size 4)\n", tests);
    check(tests <= 4, "check 2: the along-row ray's primitiveTests is at most the leaf size");
  }

  // Query 2: same axis, tmax ends before the first primitive's own box.
  {
    RtFloat const originX = -5.0f;
    RtFloat const firstBoxMinX = bySpatialIndex[0]->getBBox().getMin().getX();
    RtFloat const distanceToFirstBox = firstBoxMinX - originX;
    GMANRay const ray(GMANPoint(originX, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0), RI_EPSILON, distanceToFirstBox - 0.5f);
    std::size_t tests = 0;
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    bool const found = bvh.nearestHit(ray, hit, prim, &tests);
    check(!found, "check 2: the short-tmax ray misses everything");
    std::printf("check 2: short-tmax ray primitiveTests=%zu (expect 0)\n", tests);
    check(tests == 0, "check 2: the short-tmax ray's primitiveTests is exactly 0");
  }

  // Query 3: perpendicular to the row, aimed at a primitive in the
  // middle (spatial index 8) -- the case a poor split axis exposes.
  {
    RtFloat const midX = (RtFloat)8 * kSpacing;
    GMANRay const ray(GMANPoint(midX, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
    std::size_t tests = 0;
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    bool const found = bvh.nearestHit(ray, hit, prim, &tests);
    check(found && prim == bySpatialIndex[8], "check 2: the perpendicular ray hits the middle primitive");
    std::printf("check 2: perpendicular ray primitiveTests=%zu (leaf size 4)\n", tests);
    check(tests <= 4, "check 2: the perpendicular ray's primitiveTests is at most the leaf size");
  }
}

// ---- a direction component of +0.0 or -0.0, origin exactly on the
// box's own min or max face on that axis, must still test the leaf's own
// primitive: 1.0 / -0.0 is negative infinity, which folds a zero offset
// on that face into a NaN on one side and a wrongly-signed infinity on
// the other, and only the NaN side is caught by treating a NaN as no
// constraint. primitiveTests, not the hit/miss outcome, is the signal
// here -- the ray may or may not cross the sphere's own surface at this
// exact x, but the leaf must be reached either way. ----
void testSignedZeroFacePlane() {
  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 10.0));
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);

  GMANPoint const boxMin = sphere->getBBox().getMin();
  GMANPoint const boxMax = sphere->getBBox().getMax();

  struct Case {
    RtFloat originX;
    RtFloat dirX;
    char const* label;
  };
  RtFloat const posZero = 0.0f;
  RtFloat const negZero = -0.0f;
  Case const cases[] = {
      {boxMin.getX(), posZero, "+0.0 direction, origin on the min face"},
      {boxMax.getX(), posZero, "+0.0 direction, origin on the max face"},
      {boxMin.getX(), negZero, "-0.0 direction, origin on the min face"},
      {boxMax.getX(), negZero, "-0.0 direction, origin on the max face"},
  };

  for (Case const& c : cases) {
    GMANRay const ray(GMANPoint(c.originX, 0.0, -5.0), GMANVector(c.dirX, 0.0, 1.0));
    std::size_t tests = 0;
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    bvh.nearestHit(ray, hit, prim, &tests);
    check(tests >= 1, std::string("signed zero: ") + c.label + " still tests the leaf's own primitive");
  }
}

// A miss must leave hit and hitPrimitive exactly as the caller had them
// -- nearestHit's own contract -- not overwrite either with an empty,
// default-constructed hit or a null primitive. Covers both of
// nearestHit's own miss paths: a built tree the ray clears, and the
// rootIndex < 0 early return for a tree build() never populated.
void testMissLeavesHitUntouched() {
  // A sentinel prim never touched by build() or nearestHit here: its
  // address, not its contents, is what a miss must leave alone.
  GMANRaySphere sentinelPrimitive(1.0, -1.0, 1.0, 360.0, GMANParameterList(), GMANTransform());
  constexpr RtFloat kSentinelT = 12345.0f;
  GMANRay const missRay(GMANPoint(100.0, 100.0, -5.0), GMANVector(0.0, 0.0, 1.0));

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 10.0)));

  GMANRayBVH bvh;
  bvh.build(worldManager);

  GMANHit hit;
  hit.t = kSentinelT;
  GMANRayInterface const* prim = &sentinelPrimitive;
  bool const found = bvh.nearestHit(missRay, hit, prim);
  check(!found, "miss: nearestHit returns false when no primitive is hit");
  check(hit.t == kSentinelT, "miss: nearestHit leaves the caller's own hit untouched");
  check(prim == &sentinelPrimitive, "miss: nearestHit leaves the caller's own hitPrimitive untouched");

  GMANRayBVH emptyBvh;
  GMANHit emptyHit;
  emptyHit.t = kSentinelT;
  GMANRayInterface const* emptyPrim = &sentinelPrimitive;
  bool const emptyFound = emptyBvh.nearestHit(missRay, emptyHit, emptyPrim);
  check(!emptyFound, "empty tree: nearestHit returns false");
  check(emptyHit.t == kSentinelT, "empty tree: nearestHit leaves the caller's own hit untouched");
  check(emptyPrim == &sentinelPrimitive, "empty tree: nearestHit leaves the caller's own hitPrimitive untouched");
}

// ---- nine concentric spheres share one centroid: the build falls back to
// the object median, to a depth of exactly 2. ----
void testCoincidentCentroids() {
  constexpr int kCount = 9;

  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims;
  for (int i = 1; i <= kCount; ++i) {
    RtFloat const radius = (RtFloat)i;
    auto* sphere = new GMANRaySphere(radius, -radius, radius, 360.0, GMANParameterList(), translated(0.0, 0.0, 20.0));
    worldManager.add(sphere);
    prims.push_back(sphere);
  }

  GMANRayBVH bvh;
  bvh.build(worldManager);
  check(bvh.treeDepth() == 2, "coincident centroids: nine primitives split at the object median");

  for (int i = 0; i <= kCount; ++i) {
    GMANRay const ray(GMANPoint((RtFloat)i + 0.5f, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
    checkAgreement(bvh, prims, ray, "coincident centroids ray " + std::to_string(i));
  }
}

// ---- the same primitives in the same order build the same tree: two
// builds of check 1's fixture agree on depth and every sweep ray's box
// and primitive tests. ----
void testBuildIsDeterministic() {
  GMANLinearWorldManager firstWorld;
  GMANLinearWorldManager secondWorld;
  std::vector<GMANRayInterface*> firstPrims;
  std::vector<GMANRayInterface*> secondPrims;
  buildCheck1Fixture(false, firstWorld, firstPrims, false);
  buildCheck1Fixture(false, secondWorld, secondPrims, false);

  GMANRayBVH first;
  GMANRayBVH second;
  first.build(firstWorld);
  second.build(secondWorld);
  check(first.treeDepth() == second.treeDepth(), "deterministic: two builds agree on depth");

  std::mt19937 rng(0xB0C5u); // fixed seed: the rays are reproducible; the build draws none
  std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);
  std::uniform_real_distribution<float> originDist(-10.0f, 150.0f);
  bool same = true;
  for (int i = 0; i < 500; ++i) {
    GMANPoint const origin(originDist(rng), dirDist(rng), dirDist(rng));
    GMANRay const ray(origin, GMANVector(dirDist(rng), dirDist(rng), 0.25f));
    std::size_t firstCounts[2] = {0, 0};
    std::size_t secondCounts[2] = {0, 0};
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    first.nearestHit(ray, hit, prim, &firstCounts[0], &firstCounts[1]);
    second.nearestHit(ray, hit, prim, &secondCounts[0], &secondCounts[1]);
    same = same && firstCounts[0] == secondCounts[0] && firstCounts[1] == secondCounts[1];
  }
  check(same, "deterministic: two builds cost every ray the same box and primitive tests");
}

} // namespace

int main() {
  testCheck1();
  testExitPruningPair();
  testRandomSweep();
  testCoincidentTieBothOrders();
  testCheck2();
  testSignedZeroFacePlane();
  testBoxTestsPerNode();
  testTieAtClampedEntry();
  testTieUnderBestHitLimit();
  testTieWithinAxisTakesLowestPlane();
  testTieAcrossAxesTakesLowestAxis();
  testSurfaceAreaFewerBoxTests();
  testDepthFitsTheStack();
  testMissLeavesHitUntouched();
  testCoincidentCentroids();
  testBuildIsDeterministic();

  return checkSummary("GMANRayBVH: identical hits against a linear scan, and a bounded primitive-test count");
}
