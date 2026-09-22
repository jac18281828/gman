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
 * GMANRayHyperboloid::intersect on a segment with point1.z == point2.z, a
 * flat annulus rather than a surface spanning z: z is constant across the
 * segment, so v comes from the plane hit's own radius instead of from z
 * (dSq*v^2 + 2*pDot*v + (p0sq - x*x - y*y) == 0), and the wedge is a
 * spiral sector since phi(v) varies with v.
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanrayhyperboloid.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// p0sq == 13, pDot == -8, dSq == 16: r(v)^2 == 13 - 16v + 16v^2, minimum
// r == 3 at v == 0.5, r(0) == r(1) == sqrt(13) -- the two endpoint radii
// are equal to each other and strictly greater than the true minimum.
GMANRayHyperboloid symmetricSegment(RtFloat thetamax = 360.0) {
  RtPoint p1 = {3.0, -2.0, 0.0};
  RtPoint p2 = {3.0, 2.0, 0.0};
  return GMANRayHyperboloid(p1, p2, thetamax, GMANParameterList());
}

// p0sq == 4, pDot == 0, dSq == 9: r(v)^2 == 4 + 9v^2 is strictly
// increasing on [0, 1], so a radius has a unique v here.
GMANRayHyperboloid wedgeSegment(RtFloat thetamax) {
  RtPoint p1 = {2.0, 0.0, 0.0};
  RtPoint p2 = {2.0, 3.0, 0.0};
  return GMANRayHyperboloid(p1, p2, thetamax, GMANParameterList());
}

void testPinnedHit() {
  // A flat annulus at z == 0, inner radius 1 (point1), outer radius 2
  // (point2): a ray straight down through r == 1.5 should hit it.
  RtPoint p1 = {1.0, 0.0, 0.0};
  RtPoint p2 = {2.0, 0.0, 0.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(1.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound, "flat annulus: a ray through r == 1.5 hits the annulus at z == 0");
  check(hitFound && hit.t == 5.0, "flat annulus: t == 5");
}

// x == 2.95 sits below the true minimum radius 3: no real root. x == 3.05
// hits, at the smaller of the two ascending roots {0.3625, 0.6375}.
void testRadialBandInnerEdge() {
  GMANRayHyperboloid hyperboloid = symmetricSegment();

  GMANRay missRay(GMANPoint(2.95, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit missHit;
  check(!hyperboloid.intersect(missRay, missHit), "inner edge: x == 2.95 is below the true minimum radius and misses");

  GMANRay hitRay(GMANPoint(3.05, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  bool const hitFound = hyperboloid.intersect(hitRay, hit);
  check(hitFound && near(hit.v, 0.3625), "inner edge: x == 3.05 hits, v near the smaller ascending root 0.3625");
}

// x == 3.6 hits, at the smaller of the two ascending roots {0.00251,
// 0.99749}. x == 3.61 puts both roots outside [0, 1]: a miss.
void testRadialBandOuterEdge() {
  GMANRayHyperboloid hyperboloid = symmetricSegment();

  GMANRay hitRay(GMANPoint(3.6, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  bool const hitFound = hyperboloid.intersect(hitRay, hit);
  check(hitFound && near(hit.v, 0.00251), "outer edge: x == 3.6 hits, v near the smaller ascending root 0.00251");

  GMANRay missRay(GMANPoint(3.61, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit missHit;
  check(!hyperboloid.intersect(missRay, missHit), "outer edge: x == 3.61 puts both roots outside [0, 1] and misses");
}

// x == 3.3: both ascending roots (v ~= 0.15631 and ~= 0.84369) fall
// inside the full 360-degree wedge, a genuine tie. The smaller wins.
void testAmbiguousRootPicksSmallerV() {
  GMANRayHyperboloid hyperboloid = symmetricSegment();
  GMANRay ray(GMANPoint(3.3, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.v, 0.15631),
        "ambiguous root: the ascending, smaller root (~= 0.15631) wins over ~= 0.84369");
}

// x == 3.3 again, now against a 30-degree wedge aimed at theta == 30
// degrees: the smaller root's theta (~= 54.62 degrees) falls outside the
// wedge, so the loop must fall through to the larger root (v ~= 0.84369,
// theta ~= 5.38 degrees, u ~= 0.1793) instead of stopping at the first miss.
void testWedgeFallsThroughToSecondRoot() {
  GMANRayHyperboloid hyperboloid = symmetricSegment(30.0);
  GMANRay ray(GMANPoint(2.85788, 1.65, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.v, 0.84369) && near(hit.u, 0.1793),
        "wedge fall-through: the smaller root misses the wedge, so the larger root wins");
}

// dSq == 0 forces pDot == 0 too, so GMANQuadraticRoots' own a == 0.0
// contract returns zero roots: no separate guard needed.
void testFlatSamePointMisses() {
  RtPoint p1 = {1.0, 2.0, 0.0};
  GMANRayHyperboloid hyperboloid(p1, p1, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(1.0, 2.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  check(!hyperboloid.intersect(ray, hit), "point1 == point2: zero radial extent misses");
}

// Mirrors rayhyperboloid_test.cpp's testIntervalRejects: a positive
// control, then a tmax below the wall hit and a tmin above it, both
// rejecting.
void testFlatIntervalRejects() {
  GMANRayHyperboloid hyperboloid = symmetricSegment();
  GMANPoint const origin(3.3, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRay fullRay(origin, direction);
  check(hyperboloid.intersect(fullRay, hit) && near(hit.t, 5.0),
        "interval: the full [RI_EPSILON, RI_INFINITY] interval hits at t == 5");

  GMANRay shortRay(origin, direction, RI_EPSILON, 4.0);
  check(!hyperboloid.intersect(shortRay, hit), "interval: tmax below the wall hit (5.0) rejects it");

  GMANRay farRay(origin, direction, 6.0, RI_INFINITY);
  check(!hyperboloid.intersect(farRay, hit), "interval: tmin above the wall hit (5.0) rejects it");
}

// A ray lying entirely in the object-space z == point1.z plane never
// reaches a division: objDirection.getZ() == 0.0 rejects it first.
void testFlatCoplanarRayMisses() {
  GMANRayHyperboloid hyperboloid = symmetricSegment();
  GMANRay ray(GMANPoint(0.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!hyperboloid.intersect(ray, hit), "coplanar: a ray lying in the object-space z == point1.z plane misses");
}

// A near-tangent direction drives t toward 1e25: x*x overflows, and the
// v-quadratic's c/q division produces NaN roots. Each flat-branch guard
// rejects unless its wanted range explicitly holds, so NaN -- which fails
// every comparison -- cannot slip through as an accepted candidate.
void testFlatNaNGuardRejects() {
  RtPoint p1 = {1.0, 0.0, 0.0};
  RtPoint p2 = {2.0, 0.0, 0.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(0.0, 0.0, -1.0), GMANVector(1.0, 0.0, 1e-25f));
  GMANHit hit;

  check(!hyperboloid.intersect(ray, hit), "NaN guard: a near-tangent ray overflowing x*x misses, not a NaN-filled hit");
}

// theta ~= 89.5 degrees sits just inside the 90-degree wedge and hits at
// v near 0.5; theta ~= 90.5 degrees sits just outside and misses.
void testWedgeBoundary() {
  GMANRayHyperboloid hyperboloid = wedgeSegment(90.0);

  GMANRay insideRay(GMANPoint(-1.48249, 2.01301, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit insideHit;
  check(hyperboloid.intersect(insideRay, insideHit) && near(insideHit.v, 0.5),
        "wedge boundary: theta ~= 89.5 degrees is just inside the 90-degree wedge and hits at v near 0.5");

  GMANRay outsideRay(GMANPoint(-1.51740, 1.98683, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit outsideHit;
  check(!hyperboloid.intersect(outsideRay, outsideHit),
        "wedge boundary: theta ~= 90.5 degrees is just outside the 90-degree wedge and misses");
}

// wedgeSegment's monotonic r(v) guarantees a unique root at the target's
// radius, so this cannot land on the wrong candidate. The origin sits at
// a different z than the target's (always point1.z on the flat plane):
// origin.z == target.getZ() would leave the ray in the plane and miss.
void testFlatRoundTrip() {
  GMANRayHyperboloid hyperboloid = wedgeSegment(90.0);
  GMANPoint const target = hyperboloid.getLocation(0.5, 0.5);
  GMANPoint const origin(target.getX(), target.getY(), -5.0);
  GMANVector const direction(origin, target);
  GMANRay ray(origin, direction);
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound, "flat round trip: the ray hits the flat wedge");

  GMANPoint const roundTrip = hyperboloid.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "flat round trip: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "flat round trip: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "flat round trip: 0 <= v <= 1");

  GMANVector expectedNormal = gman::transformNormal(GMANMatrix4(), hyperboloid.getNormal(hit.u, hit.v));
  expectedNormal.normalize();
  check(near(hit.normal.getX(), expectedNormal.getX()) && near(hit.normal.getY(), expectedNormal.getY()) &&
            near(hit.normal.getZ(), expectedNormal.getZ()),
        "flat round trip: hit.normal matches getNormal(hit.u, hit.v)");
}

// A 90-degree rotation about y swaps the object's x and z axes (up to
// sign), so a ray straight down camera z -- transversal to the object
// z == 0 plane before the rotation -- would run parallel to it after.
// Aimed along camera x instead, this ray crosses that plane transversally.
void testFlatRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  RtPoint p1 = {1.0, 0.0, 0.0};
  RtPoint p2 = {2.0, 0.0, 0.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-5.0, 0.0, -1.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0), "rotation: t == 5");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), -1.5),
        "rotation: point == (0, 0, -1.5)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "rotation: normal == (-1, 0, 0)");
}

// The shear leaves the ray's own z == 0 plane crossing where a straight
// z ray finds it -- unlike the rotation above, z here still reaches the
// object's flat plane transversally.
void testFlatShearTransform() {
  GMANMatrix4 matrix; // identity, then sheared
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  RtPoint p1 = {1.0, 0.0, 0.0};
  RtPoint p2 = {2.0, 0.0, 0.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(1.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0), "shear: t == 5");
  check(near(hit.point.getX(), 1.5) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "shear: point == (1.5, 0, 0)");
  // objNormal is purely along z here; the shear's cameraToObject leaves
  // that axis unchanged, so the correct normal stays (0, 0, -1).
  // transformDirection(objectToCamera, n) -- the naive substitute --
  // would instead give (-2, 0, -1)/sqrt(5) (~= -0.894, 0, -0.447).
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "shear: normal == (0, 0, -1)");
}

} // namespace

int main() {
  testPinnedHit();
  testRadialBandInnerEdge();
  testRadialBandOuterEdge();
  testAmbiguousRootPicksSmallerV();
  testWedgeFallsThroughToSecondRoot();
  testFlatSamePointMisses();
  testFlatIntervalRejects();
  testFlatCoplanarRayMisses();
  testFlatNaNGuardRejects();
  testWedgeBoundary();
  testFlatRoundTrip();
  testFlatRotatingTransform();
  testFlatShearTransform();

  return checkSummary("GMANRayHyperboloid::intersect handles a flat annulus (point1.z == point2.z)");
}
