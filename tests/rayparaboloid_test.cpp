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
 * R5b proof: GMANRayParaboloid::intersect finds the object-space wall
 * hit, its z-band and thetamax wedge, solves the a == 0 linear case for
 * the axial ray, honors the ray's own [tmin, tmax] interval, and fills a
 * GMANHit that round trips through GMANParaboloid::getLocation.
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanrayparaboloid.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANRayParaboloid unitParaboloid() { return GMANRayParaboloid(1.0, 0.0, 1.0, 360.0, GMANParameterList()); }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// ---- check 1: the axial ray, the paraboloid's own a == 0 case: no
// quadratic term is left to solve, only a linear one, and it hits the
// apex. ----
void testAxialHit() {
  GMANRayParaboloid paraboloid = unitParaboloid();
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = paraboloid.intersect(ray, hit);
  check(hitFound, "axial: a ray down +z hits the unit paraboloid's apex");
  check(near(hit.t, 5.0), "axial: t == 5");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "axial: point == (0, 0, 0), the apex at zmin");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "axial: normal == (0, 0, -1)");
}

// ---- check 2: an off-axis hit on a partial wedge round trips ----
void testOffAxisRoundTrip() {
  GMANRayParaboloid paraboloid(2.0, 0.0, 2.0, 90.0, GMANParameterList());
  GMANPoint const target = paraboloid.getLocation(0.5, 0.5);
  GMANPoint const origin(5.0, 5.0, target.getZ());
  GMANVector const direction(origin, target);
  GMANRay ray(origin, direction);
  GMANHit hit;

  bool const hitFound = paraboloid.intersect(ray, hit);
  check(hitFound, "off-axis: the ray hits the partial paraboloid");

  GMANPoint const roundTrip = paraboloid.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "off-axis: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "off-axis: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "off-axis: 0 <= v <= 1");

  GMANVector expectedNormal = gman::transformNormal(GMANMatrix4(), paraboloid.getNormal(hit.u, hit.v));
  expectedNormal.normalize();
  check(near(hit.normal.getX(), expectedNormal.getX()) && near(hit.normal.getY(), expectedNormal.getY()) &&
            near(hit.normal.getZ(), expectedNormal.getZ()),
        "off-axis: hit.normal matches getNormal(hit.u, hit.v)");
}

// ---- check 3: the z-band and the wedge each clip ----
void testBoundsBite() {
  GMANRayParaboloid paraboloid(2.0, 0.0, 2.0, 90.0, GMANParameterList());

  GMANRay aboveBand(GMANPoint(5.0, 0.0, 3.0), GMANVector(-1.0, 0.0, 0.0));
  GMANHit aboveHit;
  check(!paraboloid.intersect(aboveBand, aboveHit), "z-band: z == 3 is above zmax and misses");

  GMANRay insideBand(GMANPoint(5.0, 0.0, 1.9), GMANVector(-1.0, 0.0, 0.0));
  GMANHit insideHit;
  check(paraboloid.intersect(insideBand, insideHit) && near(insideHit.point.getZ(), 1.9),
        "z-band: z == 1.9 is just inside zmax and hits");

  GMANPoint const insideWedgeTarget = paraboloid.getLocation(0.5, 0.5); // theta == 45 degrees
  GMANPoint const outsideWedgeTarget(-insideWedgeTarget.getX(), insideWedgeTarget.getY(), insideWedgeTarget.getZ());

  GMANPoint const wedgeOrigin(-5.0, 5.0, insideWedgeTarget.getZ());
  GMANRay outsideWedge(wedgeOrigin, GMANVector(wedgeOrigin, outsideWedgeTarget));
  GMANHit wedgeMiss;
  check(!paraboloid.intersect(outsideWedge, wedgeMiss), "wedge: theta == 135 degrees is outside a 90-degree wedge");

  GMANPoint const insideOrigin(5.0, 5.0, insideWedgeTarget.getZ());
  GMANRay insideWedgeRay(insideOrigin, GMANVector(insideOrigin, insideWedgeTarget));
  GMANHit wedgeHit;
  check(paraboloid.intersect(insideWedgeRay, wedgeHit), "wedge: theta == 45 degrees is inside a 90-degree wedge");
}

// ---- check 4: a ray from inside the paraboloid, angled across the axis,
// hits the far wall. ----
void testFarRootFromInside() {
  GMANRayParaboloid paraboloid = unitParaboloid();
  GMANRay ray(GMANPoint(0.0, 0.0, 0.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = paraboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, invSqrt2), "far root: a ray from inside the paraboloid hits the far wall");
  check(near(hit.point.getX(), invSqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.5),
        "far root: point == (1/sqrt(2), 0, 0.5)");
}

// ---- check 5a: a rotation places the paraboloid's axis along camera x.
// Aimed away from the apex: the apex's r == 0 makes its theta too
// sensitive to rot()'s own floating-point noise to predict by hand. ----
void testRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayParaboloid paraboloid(1.0, 0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.5, 0.0, 0.0), GMANVector(0.0, 0.0, -1.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = paraboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, invSqrt2), "rotation: t == 1/sqrt(2)");
  check(near(hit.point.getX(), 0.5) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), -invSqrt2),
        "rotation: point == (0.5, 0, -1/sqrt(2))");
  RtFloat const sqrt3 = (RtFloat)std::sqrt(3.0);
  RtFloat const sqrt2 = (RtFloat)std::sqrt(2.0);
  check(near(hit.normal.getX(), -1.0 / sqrt3) && near(hit.normal.getY(), 0.0) &&
            near(hit.normal.getZ(), -sqrt2 / sqrt3),
        "rotation: normal == (-1, 0, -sqrt(2))/sqrt(3)");
}

// ---- check 5b: a shear tells transformNormal(cameraToObject, n) apart
// from transformDirection(objectToCamera, n) ----
void testShearTransform() {
  GMANMatrix4 matrix; // identity, then sheared
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  GMANRayParaboloid paraboloid(1.0, 0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(1.0, 0.0, 0.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  bool const hitFound = paraboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, invSqrt2), "shear: t == 1/sqrt(2)");
  check(near(hit.point.getX(), 1.0 + invSqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.5),
        "shear: point == (1 + 1/sqrt(2), 0, 0.5)");
  // The object normal here is (sqrt(2), 0, -1) (unnormalized); the inverse
  // transpose gives (sqrt(2), 0, -1 - 2*sqrt(2)), normalized below. The
  // naive transformDirection(objectToCamera, ...) substitute would instead
  // leave the object normal's own direction unchanged.
  check(near(hit.normal.getX(), 0.346512) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -0.938045),
        "shear: normal == (sqrt(2), 0, -1 - 2*sqrt(2)), normalized");
}

// ---- check 5c: a uniform scale leaves t a camera-space distance ----
void testUniformScale() {
  GMANMatrix4 matrix;
  matrix.scale(2.0, 2.0, 2.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayParaboloid paraboloid(1.0, 0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, -10.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = paraboloid.intersect(ray, hit);
  check(hitFound && near(hit.t, 10.0), "uniform scale: t == 10, the camera-space distance to the doubled apex");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "uniform scale: point == (0, 0, 0)");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "uniform scale: normal == (0, 0, -1)");
}

// ---- check 6: a singular transform never hits ----
void testSingularTransform() {
  GMANMatrix4 matrix;
  matrix.scale(1.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayParaboloid paraboloid(1.0, 0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!paraboloid.intersect(ray, hit), "singular transform: a paraboloid with no invertible object space never hits");
}

// ---- check 7: the ray's interval rejects a hit before tmin and one past
// tmax ----
void testIntervalRejects() {
  GMANRayParaboloid paraboloid = unitParaboloid();
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 4.0);
  check(!paraboloid.intersect(shortRay, hit), "interval: tmax below the apex hit (5) rejects it");

  GMANRay farRay(origin, direction, 6.0, RI_INFINITY);
  check(!paraboloid.intersect(farRay, hit), "interval: tmin above the apex hit (5) rejects it");
}

// ---- degenerate paraboloids miss instead of filling NaN ----
void testDegenerateParaboloidsMiss() {
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRayParaboloid nullWedge(1.0, 0.0, 1.0, 0.0, GMANParameterList());
  check(!nullWedge.intersect(GMANRay(origin, direction), hit), "degenerate: thetamax == 0 misses");

  GMANRayParaboloid nullRmax(0.0, 0.0, 1.0, 360.0, GMANParameterList());
  check(!nullRmax.intersect(GMANRay(origin, direction), hit), "degenerate: rmax == 0 misses");

  GMANRayParaboloid nullZmax(1.0, 0.0, 0.0, 360.0, GMANParameterList());
  check(!nullZmax.intersect(GMANRay(origin, direction), hit), "degenerate: zmax == 0 misses");

  GMANRayParaboloid nullBand(1.0, 1.0, 1.0, 360.0, GMANParameterList());
  check(!nullBand.intersect(GMANRay(origin, direction), hit), "degenerate: zmin == zmax misses");
}

} // namespace

int main() {
  testAxialHit();
  testOffAxisRoundTrip();
  testBoundsBite();
  testFarRootFromInside();
  testRotatingTransform();
  testShearTransform();
  testUniformScale();
  testSingularTransform();
  testIntervalRejects();
  testDegenerateParaboloidsMiss();

  return checkSummary("GMANRayParaboloid::intersect hits, misses and clips correctly");
}
