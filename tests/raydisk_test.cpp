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
 * GMANRayDisk::intersect finds the object-space plane hit, its
 * radius and thetamax wedge, honoring the ray's own [tmin, tmax] interval,
 * and fills a GMANHit that round trips through GMANDisk::getLocation.
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanraydisk.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANRayDisk fullDisk() { return GMANRayDisk(0.0, 1.0, 360.0, GMANParameterList()); }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// ---- check 1: a ray through the centre hits at an exact t ----
void testCentreHit() {
  GMANRayDisk disk = fullDisk();
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = disk.intersect(ray, hit);
  check(hitFound, "centre: a ray down +z hits the unit disk at height 0");
  check(near(hit.t, 5.0), "centre: t == 5");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "centre: point == (0, 0, 0)");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "centre: normal == (0, 0, -1)");
  check(hit.primitive == &disk, "centre: primitive points at the disk hit");
}

// ---- check 2: inside the rim hits, just outside misses ----
void testRimBoundary() {
  GMANRayDisk disk = fullDisk();

  GMANRay insideRay(GMANPoint(0.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit insideHit;
  check(disk.intersect(insideRay, insideHit) && near(insideHit.point.getX(), 0.5),
        "rim: a ray inside the rim (r == 0.5) hits");

  // r == radius + 1e-3, not radius + 0.5: a rim comparison loosened to
  // e.g. radius * 1.4 would still (wrongly) call this a hit if the ray sat
  // well outside the true edge, so the miss has to be pinned close to it.
  GMANRay outsideRay(GMANPoint(1.001, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit outsideHit;
  check(!disk.intersect(outsideRay, outsideHit), "rim: a ray just outside the rim (r == radius + 1e-3) misses");
}

// ---- check 3: a ray parallel to the plane misses ----
void testParallelRayMisses() {
  GMANRayDisk disk = fullDisk();
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!disk.intersect(ray, hit), "parallel: a ray with no z component never reaches the z == height plane");
}

// ---- check 4: a partial thetamax wedge hits inside, misses outside ----
void testPartialThetamaxWedge() {
  GMANRayDisk disk(0.0, 1.0, 90.0, GMANParameterList());

  // theta == 45 degrees: inside the 90-degree wedge.
  GMANRay insideRay(GMANPoint(0.5, 0.5, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit insideHit;
  check(disk.intersect(insideRay, insideHit), "wedge: theta == 45 degrees is inside a 90-degree wedge");

  // theta == 180 degrees: outside the 90-degree wedge.
  GMANRay outsideRay(GMANPoint(-0.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit outsideHit;
  check(!disk.intersect(outsideRay, outsideHit), "wedge: theta == 180 degrees is outside a 90-degree wedge");
}

// ---- check 5: a hit's u, v round trip through getLocation ----
void testUVRoundTrip() {
  GMANRayDisk disk(1.0, 2.0, 270.0, GMANParameterList());
  GMANRay ray(GMANPoint(0.6, 0.9, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = disk.intersect(ray, hit);
  check(hitFound, "uv round trip: the off-axis ray hits the partial disk");

  GMANPoint const roundTrip = disk.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "uv round trip: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "uv round trip: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "uv round trip: 0 <= v <= 1");
}

// ---- check 6: a rotation exercises the transform's placement of the
// disk and its hit normal -- but not the inverse-transpose itself, since a
// rotation matrix is orthogonal (its inverse is its own transpose), so
// cameraToObject's inverse-transpose and a naive forward transform give
// the same answer here. testShearTransform below is the one that can tell
// them apart. ----
void testRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  // Rotating the disk (object-space normal (0, 0, -1), plane z == 0) 90
  // degrees about the y axis swings its plane to camera-space x == 0,
  // facing -x.
  GMANRayDisk disk(0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  bool const hitFound = disk.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0), "rotation: t == 5, the disk placed at camera-space x == 0");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "rotation: point == (0, 0, 0)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "rotation: normal == (-1, 0, 0), the object normal carried by the inverse transpose");
}

// ---- a shear is the one transform here that is not its own transpose
// under inversion, so it is the one that can tell
// transformNormal(cameraToObject, ...) (correct) apart from
// transformDirection(objectToCamera, ...) (the naive substitute) ----
void testShearTransform() {
  GMANMatrix4 matrix; // identity, then sheared
  // m[2][0] = k mixes object z into camera x (p' = p * m: x' = x + k*z)
  // while leaving z' = z, so the object's z == 0 plane still maps to
  // camera z == 0 -- the shear changes only which normal computation is
  // right, not where the plane itself sits.
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  GMANRayDisk disk(0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = disk.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0), "shear: t == 5, the axial ray's own z geometry is unaffected by the shear");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "shear: point == (0, 0, 0)");
  // The correct inverse-transpose gives (0, 0, -1) here (cameraToObject's
  // z row is untouched by the shear); transformDirection(objectToCamera,
  // (0, 0, -1)) -- the naive substitute -- would instead give (-2, 0, -1)'s
  // direction, so this tells the two apart.
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "shear: normal == (0, 0, -1), the inverse transpose, not transformDirection(objectToCamera, ...)'s "
        "(-2, 0, -1) direction");
}

// ---- check 7: the ray's interval rejects a hit before tmin and one past
// tmax ----
void testIntervalRejects() {
  GMANRayDisk disk = fullDisk();
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 3.0);
  check(!disk.intersect(shortRay, hit), "interval: tmax below the plane hit (5) rejects it");

  GMANRay farRay(origin, direction, 7.0, RI_INFINITY);
  check(!disk.intersect(farRay, hit), "interval: tmin above the plane hit (5) rejects it");
}

// ---- a singular transform (e.g. Scale 0 1 1) has no invertible object
// space to intersect in, and never hits. Scaling z instead of x would
// leave this axial ray's own direction transformed to (0, 0, 0), which
// the "parallel to the plane" guard above already rejects on its own --
// vacuously passing this check whether or not the singular guard ran at
// all. Scaling x keeps the z row (and so this ray's z component) intact,
// so only the singular guard stands between this ray and a false hit. ----
void testSingularTransform() {
  GMANMatrix4 matrix;
  matrix.scale(0.0, 1.0, 1.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayDisk disk(0.0, 1.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  check(!disk.intersect(ray, hit), "singular transform: a disk with no invertible object space never hits");
}

// ---- degenerate disks miss instead of filling NaN ----
void testDegenerateDisksMiss() {
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRayDisk nullWedge(0.0, 1.0, 0.0, GMANParameterList());
  check(!nullWedge.intersect(GMANRay(origin, direction), hit), "degenerate: thetamax == 0 misses");

  GMANRayDisk nullRadius(0.0, 0.0, 360.0, GMANParameterList());
  check(!nullRadius.intersect(GMANRay(origin, direction), hit), "degenerate: radius == 0 misses");
}

} // namespace

int main() {
  testCentreHit();
  testRimBoundary();
  testParallelRayMisses();
  testPartialThetamaxWedge();
  testUVRoundTrip();
  testRotatingTransform();
  testShearTransform();
  testIntervalRejects();
  testSingularTransform();
  testDegenerateDisksMiss();

  return checkSummary("GMANRayDisk::intersect hits, misses and clips correctly");
}
