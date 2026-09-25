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
 * GMANRayHyperboloid::intersect finds the object-space wall
 * hit of a segment spanning z, its v-band and thetamax wedge, honors the
 * ray's own [tmin, tmax] interval, and fills a GMANHit that round trips
 * through GMANHyperboloid::getLocation. The segment here (point1 == (1,
 * 0, -1), point2 == (0, 1, 1)) never crosses the axis, so it has no
 * axial hit at all; checks are aimed across the axis instead. Its own
 * azimuth phi(v) varies with v (0 degrees at point1, 90 at point2) --
 * unlike a segment held at a fixed y, which would make measuring theta
 * from phi(v) indistinguishable from measuring it from the x-axis.
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanrayhyperboloid.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANRayHyperboloid segmentHyperboloid(RtFloat thetamax) {
  RtPoint p1 = {1.0, 0.0, -1.0};
  RtPoint p2 = {0.0, 1.0, 1.0};
  return GMANRayHyperboloid(p1, p2, thetamax, GMANParameterList());
}

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// ---- check 1: aimed across the axis, at the segment's own midpoint
// height (z == 0, v == 0.5, r == 1/sqrt(2)) ----
void testAcrossAxisHit() {
  GMANRayHyperboloid hyperboloid = segmentHyperboloid(360.0);
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound, "across axis: a ray toward the axis hits the segment's midpoint radius");
  check(near(hit.t, 5.0 - invSqrt2), "across axis: t == 5 - 1/sqrt(2)");
  check(near(hit.point.getX(), -invSqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "across axis: point == (-1/sqrt(2), 0, 0)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "across axis: normal == (-1, 0, 0)");
  check(hit.primitive == &hyperboloid, "across axis: primitive points at the hyperboloid hit");
}

// ---- check 2: an off-axis hit on a partial wedge round trips ----
void testOffAxisRoundTrip() {
  GMANRayHyperboloid hyperboloid = segmentHyperboloid(90.0);
  GMANPoint const target = hyperboloid.getLocation(0.5, 0.5);
  GMANPoint const origin(5.0, 5.0, target.getZ());
  GMANVector const direction(origin, target);
  GMANRay ray(origin, direction);
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound, "off-axis: the ray hits the partial hyperboloid");

  GMANPoint const roundTrip = hyperboloid.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "off-axis: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "off-axis: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "off-axis: 0 <= v <= 1");

  GMANVector expectedNormal = gman::transformNormal(GMANMatrix4(), hyperboloid.getNormal(hit.u, hit.v));
  expectedNormal.normalize();
  check(near(hit.normal.getX(), expectedNormal.getX()) && near(hit.normal.getY(), expectedNormal.getY()) &&
            near(hit.normal.getZ(), expectedNormal.getZ()),
        "off-axis: hit.normal matches getNormal(hit.u, hit.v)");
}

// ---- check 3: the v-band and the wedge each clip. The v-band checks use
// a full (thetamax 360) hyperboloid: phi(v) here approaches 90 degrees as
// v approaches 1, so pairing a near-v-1 hit with a partial wedge would
// couple the two bounds together instead of pinning the v-band alone. ----
void testBoundsBite() {
  GMANRayHyperboloid full = segmentHyperboloid(360.0);

  // z == 1.5 is v == 1.25, past the point2 end of the segment.
  GMANRay aboveBand(GMANPoint(-5.0, 0.0, 1.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit aboveHit;
  check(!full.intersect(aboveBand, aboveHit), "v-band: z == 1.5 is past point2 (v == 1.25) and misses");

  // z == 0.98 is v == 0.99, just inside the point2 end.
  GMANRay insideBand(GMANPoint(-5.0, 0.0, 0.98), GMANVector(1.0, 0.0, 0.0));
  GMANHit insideHit;
  check(full.intersect(insideBand, insideHit) && near(insideHit.point.getZ(), 0.98),
        "v-band: z == 0.98 is just inside point2 (v == 0.99) and hits");

  // getLocation does not itself clamp u to [0, 1]; theta == u*thetamax
  // reaches 135 degrees at u == 1.5, outside the 90-degree wedge, and 45
  // degrees at u == 0.5, inside it. v == 0.5 keeps phi(v) away from the
  // v-band's own edge case above.
  GMANRayHyperboloid wedge = segmentHyperboloid(90.0);
  GMANPoint const insideWedgeTarget = wedge.getLocation(0.5, 0.5);
  GMANPoint const outsideWedgeTarget = wedge.getLocation(1.5, 0.5);

  GMANPoint const wedgeOrigin(-5.0, 5.0, outsideWedgeTarget.getZ());
  GMANRay outsideWedge(wedgeOrigin, GMANVector(wedgeOrigin, outsideWedgeTarget));
  GMANHit wedgeMiss;
  check(!wedge.intersect(outsideWedge, wedgeMiss), "wedge: theta == 135 degrees is outside a 90-degree wedge");

  GMANPoint const insideOrigin(5.0, 5.0, insideWedgeTarget.getZ());
  GMANRay insideWedgeRay(insideOrigin, GMANVector(insideOrigin, insideWedgeTarget));
  GMANHit wedgeHit;
  check(wedge.intersect(insideWedgeRay, wedgeHit), "wedge: theta == 45 degrees is inside a 90-degree wedge");
}

// ---- check 4: a ray from inside the segment's radius, angled across the
// axis, hits the far wall ----
void testFarRootFromInside() {
  GMANRayHyperboloid hyperboloid = segmentHyperboloid(360.0);
  GMANRay ray(GMANPoint(0.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, invSqrt2), "far root: a ray from inside the segment's radius hits the far wall");
  check(near(hit.point.getX(), invSqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "far root: point == (1/sqrt(2), 0, 0)");
}

// ---- a segment descending in z (point1.z > point2.z) is an ordinary
// surface, not an inverted band: the same physical wall, hit at the same
// t and point, but its normal follows getNormal's cross product, which
// reverses sign with dz. ----
void testDescendingSegment() {
  RtPoint p1 = {0.0, 1.0, 1.0};
  RtPoint p2 = {1.0, 0.0, -1.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0 - invSqrt2), "descending: t == 5 - 1/sqrt(2), the same physical wall");
  check(near(hit.point.getX(), -invSqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "descending: point == (-1/sqrt(2), 0, 0)");
  check(near(hit.normal.getX(), 1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "descending: normal == (1, 0, 0), the ascending segment's normal negated");
}

// ---- check 5a: a rotation places the segment's axis along camera x ----
void testRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  RtPoint p1 = {1.0, 0.0, -1.0};
  RtPoint p2 = {0.0, 1.0, 1.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, 5.0), GMANVector(0.0, 0.0, -1.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0 - invSqrt2), "rotation: t == 5 - 1/sqrt(2)");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), invSqrt2),
        "rotation: point == (0, 0, 1/sqrt(2))");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 1.0),
        "rotation: normal == (0, 0, 1)");
}

// ---- check 5b: a shear tells transformNormal(cameraToObject, n) apart
// from transformDirection(objectToCamera, n). The ray's own geometry sits
// entirely at object z == 0, so the shear leaves the hit point and t
// unchanged; only the normal, which carries a nonzero z component after
// the shear, exposes it. ----
void testShearTransform() {
  GMANMatrix4 matrix; // identity, then sheared
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  RtPoint p1 = {1.0, 0.0, -1.0};
  RtPoint p2 = {0.0, 1.0, 1.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0 - invSqrt2), "shear: t == 5 - 1/sqrt(2)");
  check(near(hit.point.getX(), -invSqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "shear: point == (-1/sqrt(2), 0, 0)");
  // The object normal here is (-sqrt(2), 0, 0) (unnormalized); the inverse
  // transpose gives (-sqrt(2), 0, 2*sqrt(2)), normalized below.
  // transformDirection(objectToCamera, ...) -- the naive substitute --
  // would instead leave the object normal's own (-1, 0, 0) unchanged.
  check(near(hit.normal.getX(), -0.447214) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.894427),
        "shear: normal == (-sqrt(2), 0, 2*sqrt(2)), normalized");
}

// ---- check 5c: a uniform scale leaves t a camera-space distance ----
void testUniformScale() {
  GMANMatrix4 matrix;
  matrix.scale(2.0, 2.0, 2.0);
  GMANTransform transform = makeTransform(matrix);

  RtPoint p1 = {1.0, 0.0, -1.0};
  RtPoint p2 = {0.0, 1.0, 1.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-10.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const sqrt2 = (RtFloat)std::sqrt(2.0);
  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 10.0 - sqrt2), "uniform scale: t == 10 - sqrt(2), the camera-space distance");
  check(near(hit.point.getX(), -sqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "uniform scale: point == (-sqrt(2), 0, 0)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "uniform scale: normal == (-1, 0, 0), unchanged by a uniform scale");
}

// ---- check 6: a singular transform never hits ----
void testSingularTransform() {
  GMANMatrix4 matrix;
  matrix.scale(1.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  RtPoint p1 = {1.0, 0.0, -1.0};
  RtPoint p2 = {0.0, 1.0, 1.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!hyperboloid.intersect(ray, hit),
        "singular transform: a hyperboloid with no invertible object space never hits");
}

// ---- check 7: the ray's interval rejects a hit before tmin and one past
// tmax ----
void testIntervalRejects() {
  GMANRayHyperboloid hyperboloid = segmentHyperboloid(360.0);
  GMANPoint const origin(-5.0, 0.0, 0.0);
  GMANVector const direction(1.0, 0.0, 0.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 4.0);
  check(!hyperboloid.intersect(shortRay, hit), "interval: tmax below the wall hit (~4.29) rejects it");

  GMANRay farRay(origin, direction, 6.0, RI_INFINITY);
  check(!hyperboloid.intersect(farRay, hit), "interval: tmin above both wall hits (~4.29 and ~5.71) rejects them");
}

// ---- a null wedge misses instead of filling NaN ----
void testDegenerateHyperboloidMisses() {
  GMANRayHyperboloid nullWedge = segmentHyperboloid(0.0);
  GMANHit hit;
  check(!nullWedge.intersect(GMANRay(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0)), hit),
        "degenerate: thetamax == 0 misses");
}

} // namespace

int main() {
  testAcrossAxisHit();
  testOffAxisRoundTrip();
  testBoundsBite();
  testFarRootFromInside();
  testDescendingSegment();
  testRotatingTransform();
  testShearTransform();
  testUniformScale();
  testSingularTransform();
  testIntervalRejects();
  testDegenerateHyperboloidMisses();

  return checkSummary("GMANRayHyperboloid::intersect hits, misses and clips correctly");
}
