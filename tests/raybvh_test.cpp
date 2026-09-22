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
 * R6 proof, §8 check C: GMANRayBVH, tested standalone against a
 * hand-built GMANLinearWorldManager, before anything in the renderer
 * changes.
 *
 * Check 1: a mixed fixture's GMANRayBVH::nearestHit and a small linear
 * scan written locally here (not the removed walkWorldManager) agree on
 * every ray -- identical primitive, t, point, normal, u and v, or an
 * identical miss.
 *
 * Check 2: a second, one-axis fixture's primitiveTests counter matches
 * the leaf size or less for a targeted ray, exactly 0 for a ray whose
 * tmax ends short of every primitive, and the leaf size or less again for
 * a perpendicular ray -- the case a poor split axis actually exposes.
 */

#include <cmath>
#include <cstdio>
#include <string>
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

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

GMANTransform translated(RtFloat dx, RtFloat dy, RtFloat dz) {
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  return makeTransform(m);
}

// The BVH's own contract, written independently of it: the nearest hit
// within ray's [tmin, tmax], the earlier-inserted primitive winning an
// exact t tie -- walkWorldManager's own semantics, before its removal,
// reproduced here rather than resurrected.
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
// primitive's order fixed, so the fixture is built twice (§8's own
// requirement) to catch an order dependency.
struct FixtureHandles {
  GMANRayInterface* overlapNear = nullptr;
  GMANRayInterface* overlapFar = nullptr;
  GMANRayInterface* coincidentEarlier = nullptr;
  GMANRayInterface* coincidentLater = nullptr;
  GMANRayInterface* boxDecoyDisk = nullptr;
  GMANRayInterface* trueTarget = nullptr;
};

FixtureHandles buildCheck1Fixture(bool swapOverlap, GMANLinearWorldManager& worldManager,
                                  std::vector<GMANRayInterface*>& prims) {
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
  GMANMatrix4 singularMatrix;
  singularMatrix.scale(1.0, 1.0, 0.0);
  add(new GMANRayCone(2.0, 1.0, 360.0, GMANParameterList(), makeTransform(singularMatrix)));

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

// ---- check 2 ----
void testCheck2() {
  constexpr int kCount = 16; // 4x the leaf size
  constexpr RtFloat kSpacing = 3.0f;
  constexpr RtFloat kRadius = 0.4f;

  // A fixed-seed shuffle of insertion order -- a permanent property of
  // this fixture, hardcoded rather than re-derived per run.
  int const insertionOrder[kCount] = {7, 2, 13, 0, 9, 4, 11, 15, 1, 8, 5, 14, 3, 10, 6, 12};

  GMANLinearWorldManager worldManager;
  std::vector<GMANRayInterface*> prims(kCount, nullptr);
  GMANRayInterface* bySpatialIndex[kCount];
  for (int slot = 0; slot < kCount; ++slot) {
    int const spatialIndex = insertionOrder[slot];
    RtFloat const x = (RtFloat)spatialIndex * kSpacing;
    auto* sphere = new GMANRaySphere(kRadius, -kRadius, kRadius, 360.0, GMANParameterList(), translated(x, 0.0, 0.0));
    worldManager.add(sphere);
    prims[slot] = sphere;
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

} // namespace

int main() {
  testCheck1();
  testCheck2();

  return checkSummary("GMANRayBVH: identical hits against a linear scan, and a bounded primitive-test count");
}
