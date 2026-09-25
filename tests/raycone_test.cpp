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
 * GMANRayCone::intersect finds the object-space wall hit, its
 * height and thetamax wedge, honors the ray's own [tmin, tmax] interval,
 * and fills a GMANHit that round trips through GMANCone::getLocation. A
 * ray parallel to a generatrix hits through solveRayQuadratic's own a == 0
 * linear branch: computed in double, a's two squared terms cancel exactly
 * (see tests/rayquadratic_test.cpp for the branch itself).
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanraycone.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANRayCone unitCone() { return GMANRayCone(1.0, 1.0, 360.0, GMANParameterList()); }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// ---- check 1a: a ray down the axis grazes the apex ----
void testAxialHit() {
  GMANRayCone cone = unitCone();
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound, "axial: a ray down +z grazes the unit cone's apex");
  check(near(hit.t, 6.0), "axial: t == 6");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 1.0),
        "axial: point == (0, 0, 1), the apex");
  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  check(near(hit.normal.getX(), invSqrt2) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), invSqrt2),
        "axial: normal == (1, 0, 1)/sqrt(2)");
}

// ---- check 1b: a ray parallel to a generatrix. a vanishes here, leaving
// only a linear equation: computed in double from this ray's own origin
// (inside the cone's bounding sphere, so unshifted), a's two squared
// terms are bit-identical and cancel exactly, so this hits through
// solveRayQuadratic's own a == 0 branch, not its stable q form. ----
void testGeneratrixParallelHit() {
  GMANRayCone cone = unitCone();
  GMANRay ray(GMANPoint(0.0, 0.0, 0.0), GMANVector(-1.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound, "generatrix-parallel: a ray parallel to the theta == 0 generatrix hits");
  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  check(near(hit.t, invSqrt2), "generatrix-parallel: t == 1/sqrt(2)");
  check(near(hit.point.getX(), -0.5) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.5),
        "generatrix-parallel: point == (-0.5, 0, 0.5)");
  check(near(hit.normal.getX(), -invSqrt2) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), invSqrt2),
        "generatrix-parallel: normal == (-1, 0, 1)/sqrt(2)");
}

// ---- check 2: an off-axis hit on a partial wedge round trips ----
void testOffAxisRoundTrip() {
  GMANRayCone cone(2.0, 1.0, 90.0, GMANParameterList());
  GMANPoint const target = cone.getLocation(0.5, 0.5);
  GMANPoint const origin(5.0, 5.0, target.getZ());
  GMANVector const direction(origin, target);
  GMANRay ray(origin, direction);
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound, "off-axis: the ray hits the partial cone");

  GMANPoint const roundTrip = cone.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "off-axis: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "off-axis: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "off-axis: 0 <= v <= 1");

  GMANVector expectedNormal = gman::transformNormal(GMANMatrix4(), cone.getNormal(hit.u, hit.v));
  expectedNormal.normalize();
  check(near(hit.normal.getX(), expectedNormal.getX()) && near(hit.normal.getY(), expectedNormal.getY()) &&
            near(hit.normal.getZ(), expectedNormal.getZ()),
        "off-axis: hit.normal matches getNormal(hit.u, hit.v)");
}

// ---- check 3: the height bound and the wedge each clip ----
void testBoundsBite() {
  GMANRayCone cone(2.0, 1.0, 90.0, GMANParameterList());

  GMANRay belowHeight(GMANPoint(5.0, 0.0, -0.5), GMANVector(-1.0, 0.0, 0.0));
  GMANHit belowHit;
  check(!cone.intersect(belowHeight, belowHit), "height: z == -0.5 is below 0 and misses");

  GMANRay insideHeight(GMANPoint(5.0, 0.0, 0.1), GMANVector(-1.0, 0.0, 0.0));
  GMANHit insideHit;
  check(cone.intersect(insideHeight, insideHit) && near(insideHit.point.getZ(), 0.1),
        "height: z == 0.1 is just inside 0 and hits");

  GMANPoint const insideWedgeTarget = cone.getLocation(0.5, 0.5); // theta == 45 degrees
  GMANPoint const outsideWedgeTarget(-insideWedgeTarget.getX(), insideWedgeTarget.getY(), insideWedgeTarget.getZ());
  GMANPoint const wedgeOrigin(-5.0, 5.0, insideWedgeTarget.getZ());

  GMANRay outsideWedge(wedgeOrigin, GMANVector(wedgeOrigin, outsideWedgeTarget));
  GMANHit wedgeMiss;
  check(!cone.intersect(outsideWedge, wedgeMiss), "wedge: theta == 135 degrees is outside a 90-degree wedge");

  GMANPoint const insideOrigin(5.0, 5.0, insideWedgeTarget.getZ());
  GMANRay insideWedgeRay(insideOrigin, GMANVector(insideOrigin, insideWedgeTarget));
  GMANHit wedgeHit;
  check(cone.intersect(insideWedgeRay, wedgeHit), "wedge: theta == 45 degrees is inside a 90-degree wedge");
}

// ---- check 4: a ray from inside the cone, angled across the axis, hits
// the far wall. A ray straight down the axis is check 1a's tangent apex
// hit, not a far-wall hit. ----
void testFarRootFromInside() {
  GMANRayCone cone = unitCone();
  GMANRay ray(GMANPoint(0.0, 0.0, 0.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound && near(hit.t, 0.5), "far root: a ray from inside the cone hits the far wall at t == 0.5");
  check(near(hit.point.getX(), 0.5) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.5),
        "far root: point == (0.5, 0, 0.5)");
}

// ---- check 5a: a rotation places the cone's axis along camera x. Aimed
// at the wall away from the apex: the apex's r == 0 makes its theta -- and
// so its normal -- too sensitive to rot()'s own floating-point noise to
// predict by hand. ----
void testRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayCone cone(1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.5, 0.0, 5.0), GMANVector(0.0, 0.0, -1.0));
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound && near(hit.t, 4.5), "rotation: t == 4.5");
  check(near(hit.point.getX(), 0.5) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.5),
        "rotation: point == (0.5, 0, 0.5)");
  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  check(near(hit.normal.getX(), invSqrt2) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), invSqrt2),
        "rotation: normal == (1, 0, 1)/sqrt(2)");
}

// ---- check 5b: a shear tells transformNormal(cameraToObject, n) apart
// from transformDirection(objectToCamera, n) ----
void testShearTransform() {
  GMANMatrix4 matrix; // identity, then sheared
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  GMANRayCone cone(1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound && near(hit.t, 16.0 / 3.0), "shear: t == 16/3");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 1.0 / 3.0),
        "shear: point == (0, 0, 1/3)");
  // The correct inverse transpose gives (-1, 0, 3)/sqrt(10) here; the naive
  // transformDirection(objectToCamera, ...) substitute would instead leave
  // the object normal's own (-1, 0, 1)/sqrt(2) direction unchanged.
  RtFloat const sqrt10 = (RtFloat)std::sqrt(10.0);
  check(near(hit.normal.getX(), -1.0 / sqrt10) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 3.0 / sqrt10),
        "shear: normal == (-1, 0, 3)/sqrt(10), the inverse transpose");
}

// ---- check 5c: a uniform scale leaves t a camera-space distance ----
void testUniformScale() {
  GMANMatrix4 matrix;
  matrix.scale(2.0, 2.0, 2.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayCone cone(1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, -10.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = cone.intersect(ray, hit);
  check(hitFound && near(hit.t, 12.0), "uniform scale: t == 12, the camera-space distance to the doubled apex");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 2.0),
        "uniform scale: point == (0, 0, 2)");
  RtFloat const invSqrt2 = (RtFloat)(1.0 / std::sqrt(2.0));
  check(near(hit.normal.getX(), invSqrt2) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), invSqrt2),
        "uniform scale: normal == (1, 0, 1)/sqrt(2)");
}

// ---- check 6: a singular transform never hits ----
void testSingularTransform() {
  GMANMatrix4 matrix;
  matrix.scale(1.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayCone cone(1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, 0.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!cone.intersect(ray, hit), "singular transform: a cone with no invertible object space never hits");
}

// ---- check 7: the ray's interval rejects a hit before tmin and one past
// tmax ----
void testIntervalRejects() {
  GMANRayCone cone = unitCone();
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 5.0);
  check(!cone.intersect(shortRay, hit), "interval: tmax below the apex hit (6) rejects it");

  GMANRay farRay(origin, direction, 7.0, RI_INFINITY);
  check(!cone.intersect(farRay, hit), "interval: tmin above the apex hit (6) rejects it");
}

// ---- degenerate cones miss instead of filling NaN ----
void testDegenerateConesMiss() {
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRayCone nullWedge(1.0, 1.0, 0.0, GMANParameterList());
  check(!nullWedge.intersect(GMANRay(origin, direction), hit), "degenerate: thetamax == 0 misses");

  GMANRayCone nullRadius(1.0, 0.0, 360.0, GMANParameterList());
  check(!nullRadius.intersect(GMANRay(origin, direction), hit), "degenerate: radius == 0 misses");

  GMANRayCone nullHeight(0.0, 1.0, 360.0, GMANParameterList());
  check(!nullHeight.intersect(GMANRay(origin, direction), hit), "degenerate: height == 0 misses");
}

} // namespace

int main() {
  testAxialHit();
  testGeneratrixParallelHit();
  testOffAxisRoundTrip();
  testBoundsBite();
  testFarRootFromInside();
  testRotatingTransform();
  testShearTransform();
  testUniformScale();
  testSingularTransform();
  testIntervalRejects();
  testDegenerateConesMiss();

  return checkSummary("GMANRayCone::intersect hits, misses and clips correctly");
}
