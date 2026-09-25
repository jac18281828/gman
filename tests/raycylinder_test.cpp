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
 * GMANRayCylinder::intersect finds the object-space wall hit,
 * its z-band and thetamax wedge, honoring the ray's own [tmin, tmax]
 * interval, and fills a GMANHit that round trips through
 * GMANCylinder::getLocation.
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanraycylinder.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANRayCylinder fullCylinder() { return GMANRayCylinder(1.0, -1.0, 1.0, 360.0, GMANParameterList()); }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// ---- check 1: a ray aimed across the axis hits the wall. An unbounded
// cylinder has no axial hit at all -- a ray down its axis has no x or y
// component to solve for -- so this is aimed across the axis instead. ----
void testAcrossAxisHit() {
  GMANRayCylinder cylinder = fullCylinder();
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = cylinder.intersect(ray, hit);
  check(hitFound, "across axis: a ray toward the axis hits the unit cylinder");
  check(near(hit.t, 4.0), "across axis: t == 4");
  check(near(hit.point.getX(), -1.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "across axis: point == (-1, 0, 0)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "across axis: outward normal == (-1, 0, 0)");
  check(hit.primitive == &cylinder, "across axis: primitive points at the cylinder hit");
}

// ---- check 2: an off-axis hit on a partial wedge round trips ----
void testOffAxisRoundTrip() {
  GMANRayCylinder cylinder(2.0, 0.0, 4.0, 270.0, GMANParameterList());
  GMANRay ray(GMANPoint(-10.0, 1.0, 1.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = cylinder.intersect(ray, hit);
  check(hitFound, "off-axis: the ray hits the radius-2 cylinder");
  // theta == 150 degrees: sqrt(4 - 1) == sqrt(3), so the wall sits at
  // x == -sqrt(3), y == 1.
  RtFloat const sqrt3 = (RtFloat)std::sqrt(3.0);
  check(near(hit.point.getX(), -sqrt3) && near(hit.point.getY(), 1.0) && near(hit.point.getZ(), 1.0),
        "off-axis: point == (-sqrt(3), 1, 1)");

  GMANPoint const roundTrip = cylinder.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "off-axis: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "off-axis: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "off-axis: 0 <= v <= 1");
  check(near(hit.v, 0.25), "off-axis: v == 0.25, z == 1 a quarter of the way from zmin (0) to zmax (4)");
}

// ---- check 3: the z-band and the wedge each clip ----
void testBoundsBite() {
  GMANRayCylinder cylinder = fullCylinder();

  GMANRay aboveBand(GMANPoint(-5.0, 0.0, 1.5), GMANVector(1.0, 0.0, 0.0));
  GMANHit aboveHit;
  check(!cylinder.intersect(aboveBand, aboveHit), "z-band: z == 1.5 is above zmax and misses");

  GMANRay insideBand(GMANPoint(-5.0, 0.0, 0.99), GMANVector(1.0, 0.0, 0.0));
  GMANHit insideHit;
  check(cylinder.intersect(insideBand, insideHit) && near(insideHit.point.getZ(), 0.99),
        "z-band: z == 0.99 is just inside zmax and hits");

  // y == -0.5: both roots (theta == 210 and 330 degrees) fall outside the
  // 90-degree wedge, so the loop rejects the near one and falls through to
  // reject the far one too.
  GMANRayCylinder wedge(1.0, -1.0, 1.0, 90.0, GMANParameterList());
  GMANRay outsideWedge(GMANPoint(-5.0, -0.5, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit wedgeMiss;
  check(!wedge.intersect(outsideWedge, wedgeMiss), "wedge: theta == 210 and 330 degrees are outside a 90-degree wedge");

  // y == 0.5: the near root (theta == 150 degrees) is outside the wedge
  // and the far root (theta == 30 degrees) is inside it, so the loop
  // falls through to a hit.
  GMANRay insideWedge(GMANPoint(-5.0, 0.5, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit wedgeHit;
  check(wedge.intersect(insideWedge, wedgeHit), "wedge: the near root misses at 150 degrees, the far one hits at 30");
}

// ---- check 4: a ray from inside the bore, angled across the axis, hits
// the far wall. A ray straight down the axis would instead be the
// a == 0, b == 0 miss gman::solveRayQuadratic's own linear branch
// reports. ----
void testFarRootFromInside() {
  GMANRayCylinder cylinder = fullCylinder();
  GMANRay ray(GMANPoint(0.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = cylinder.intersect(ray, hit);
  check(hitFound && near(hit.t, 1.0), "far root: a ray from the bore's centre hits the far wall at t == 1");
  check(near(hit.point.getX(), 1.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "far root: point == (1, 0, 0)");
}

// ---- check 5a: a rotation places the cylinder's axis along camera x ----
void testRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayCylinder cylinder(1.0, -1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, -5.0, 0.0), GMANVector(0.0, 1.0, 0.0));
  GMANHit hit;

  bool const hitFound = cylinder.intersect(ray, hit);
  check(hitFound && near(hit.t, 4.0), "rotation: t == 4, the rotated cylinder's wall at camera y == -1");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), -1.0) && near(hit.point.getZ(), 0.0),
        "rotation: point == (0, -1, 0)");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), -1.0) && near(hit.normal.getZ(), 0.0),
        "rotation: normal == (0, -1, 0)");
}

// ---- check 5b: a shear tells transformNormal(cameraToObject, n) apart
// from transformDirection(objectToCamera, n), which the rotation above
// cannot -- tests/raydisk_test.cpp's own testShearTransform is the model.
// ----
void testShearTransform() {
  GMANMatrix4 matrix; // identity, then sheared
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  GMANRayCylinder cylinder(1.0, -2.0, 2.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-5.0, 0.0, 1.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = cylinder.intersect(ray, hit);
  check(hitFound && near(hit.t, 6.0), "shear: t == 6");
  check(near(hit.point.getX(), 1.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 1.0),
        "shear: point == (1, 0, 1)");
  // The correct inverse transpose gives (-1, 0, 2)/sqrt(5): the shear mixes
  // the object normal's x == -1 into the camera normal's z, through
  // cameraToObject's own [2][0] == -2. transformDirection(objectToCamera,
  // ...) -- the naive substitute -- would instead leave it (-1, 0, 0).
  RtFloat const sqrt5 = (RtFloat)std::sqrt(5.0);
  check(near(hit.normal.getX(), -1.0 / sqrt5) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 2.0 / sqrt5),
        "shear: normal == (-1, 0, 2)/sqrt(5), the inverse transpose, not transformDirection(objectToCamera, "
        "...)'s (-1, 0, 0)");
}

// ---- check 5c: a uniform scale leaves t a camera-space distance, pinning
// the un-normalized object-space direction GMANRaySphere::intersect
// documents. ----
void testUniformScale() {
  GMANMatrix4 matrix;
  matrix.scale(2.0, 2.0, 2.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayCylinder cylinder(1.0, -1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-10.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = cylinder.intersect(ray, hit);
  check(hitFound && near(hit.t, 8.0), "uniform scale: t == 8, the camera-space distance to the doubled radius-2 wall");
  check(near(hit.point.getX(), -2.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "uniform scale: point == (-2, 0, 0)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "uniform scale: normal == (-1, 0, 0)");
}

// ---- check 6: a singular transform never hits. The criterion is the
// mutation, not the matrix: this ray hits with the singular guard
// removed, unlike a ray straight down the axis under a scale that zeroes
// x or y, which the a == 0 guard would reject anyway. ----
void testSingularTransform() {
  GMANMatrix4 matrix;
  matrix.scale(1.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayCylinder cylinder(1.0, -1.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!cylinder.intersect(ray, hit), "singular transform: a cylinder with no invertible object space never hits");
}

// ---- check 7: the ray's interval rejects a hit before tmin and one past
// tmax ----
void testIntervalRejects() {
  GMANRayCylinder cylinder = fullCylinder();
  GMANPoint const origin(-5.0, 0.0, 0.0);
  GMANVector const direction(1.0, 0.0, 0.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 3.0);
  check(!cylinder.intersect(shortRay, hit), "interval: tmax below the wall hit (4) rejects it");

  GMANRay farRay(origin, direction, 7.0, RI_INFINITY);
  check(!cylinder.intersect(farRay, hit), "interval: tmin above both wall hits (4 and 6) rejects them");
}

// ---- degenerate cylinders miss instead of filling NaN ----
void testDegenerateCylindersMiss() {
  GMANPoint const origin(-5.0, 0.0, 0.0);
  GMANVector const direction(1.0, 0.0, 0.0);
  GMANHit hit;

  GMANRayCylinder nullBand(1.0, 0.0, 0.0, 360.0, GMANParameterList());
  check(!nullBand.intersect(GMANRay(origin, direction), hit), "degenerate: zmin == zmax misses");

  GMANRayCylinder nullWedge(1.0, -1.0, 1.0, 0.0, GMANParameterList());
  check(!nullWedge.intersect(GMANRay(origin, direction), hit), "degenerate: thetamax == 0 misses");

  GMANRayCylinder nullRadius(0.0, -1.0, 1.0, 360.0, GMANParameterList());
  check(!nullRadius.intersect(GMANRay(origin, direction), hit), "degenerate: radius == 0 misses");
}

} // namespace

int main() {
  testAcrossAxisHit();
  testOffAxisRoundTrip();
  testBoundsBite();
  testFarRootFromInside();
  testRotatingTransform();
  testShearTransform();
  testUniformScale();
  testSingularTransform();
  testIntervalRejects();
  testDegenerateCylindersMiss();

  return checkSummary("GMANRayCylinder::intersect hits, misses and clips correctly");
}
