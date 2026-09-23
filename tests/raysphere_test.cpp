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
 * R2 proof: GMANRaySphere::intersect finds the nearest surviving root of
 * the sphere quadratic, honoring the ray's own [tmin, tmax] interval, the
 * closed [zmin, zmax] band and thetamax, and fills a GMANHit that round
 * trips through GMANSphere::getLocation.
 */

#include <cmath>

#include "check.h"
#include "gmanray.h"
#include "gmanraysphere.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANRaySphere fullSphere() { return GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList()); }

// The off-axis ray check 4's round trip and check 7's thetamax rejection
// both need: aimed at the origin from a point whose hit has u and v both
// strictly interior, unlike the axial ray's polar hit.
GMANRay offAxisRayTowardOrigin() {
  GMANPoint const origin(2.0, 1.0, 0.5);
  GMANVector const direction(origin, GMANPoint(0.0, 0.0, 0.0));
  return GMANRay(origin, direction);
}

// ---- check 4: a hit is right in every field ----
void testAxialHitFields() {
  GMANRaySphere sphere = fullSphere();
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = sphere.intersect(ray, hit);
  check(hitFound, "axial hit: a ray down +z hits the full unit sphere");
  check(near(hit.t, 4.0), "axial hit: t == 4");
  check(near(hit.point.getX(), 0.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), -1.0),
        "axial hit: point == (0, 0, -1), exactly on z == zmin");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "axial hit: outward normal == (0, 0, -1)");
  check(hit.primitive == &sphere, "axial hit: primitive points at the sphere hit");
}

// ---- check 4 continued: the off-axis round trip ----
void testOffAxisRoundTrip() {
  GMANRaySphere sphere = fullSphere();
  GMANRay ray = offAxisRayTowardOrigin();
  GMANHit hit;

  bool const hitFound = sphere.intersect(ray, hit);
  check(hitFound, "off-axis hit: a ray aimed at the origin hits the full unit sphere");

  GMANPoint const roundTrip = sphere.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "off-axis hit: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "off-axis hit: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "off-axis hit: 0 <= v <= 1");
}

// ---- check 5: the ray's interval rejects, and falls through ----
void testIntervalRejectsAndFallsThrough() {
  GMANRaySphere sphere = fullSphere();
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 3.0);
  check(!sphere.intersect(shortRay, hit), "interval: tmax below the near root (4) rejects both roots");

  GMANRay farRay(origin, direction, 7.0, RI_INFINITY);
  check(!sphere.intersect(farRay, hit), "interval: tmin above the far root (6) rejects both roots");

  GMANRay straddlingRay(origin, direction, 5.0, RI_INFINITY);
  bool const hitFound = sphere.intersect(straddlingRay, hit);
  check(hitFound && near(hit.t, 6.0),
        "interval: tmin between the roots rejects the near one and falls through to t == 6");
}

// ---- check 6: tangent, inside, and behind ----
void testTangentInsideAndBehind() {
  GMANRaySphere sphere = fullSphere();

  GMANRay tangentRay(GMANPoint(1.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit tangentHit;
  check(sphere.intersect(tangentRay, tangentHit) && near(tangentHit.t, 5.0) && near(tangentHit.point.getX(), 1.0) &&
            near(tangentHit.point.getY(), 0.0) && near(tangentHit.point.getZ(), 0.0),
        "tangent: a grazing ray returns the single tangent hit");

  GMANRay awayRay(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, -1.0));
  GMANHit awayHit;
  check(!sphere.intersect(awayRay, awayHit), "behind: a ray pointing away from the sphere misses");

  // Fired from the centre, not merely from somewhere inside: b is zero
  // only at closest approach, and the centre is the one origin that
  // exercises sign(0) and the c/q division GMANSign would poison.
  GMANRay centreRay(GMANPoint(0.0, 0.0, 0.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit centreHit;
  check(sphere.intersect(centreRay, centreHit) && centreHit.t > 0.0 && near(centreHit.t, 1.0),
        "inside: a ray fired from the centre returns the far intersection with t positive");
}

// ---- check 7: the partial sphere clips ----
void testPartialSphereClips() {
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);

  // zmin raised above the full sphere's near-root z (-1): the near root
  // clips and the far root at (0, 0, 1) survives the fall-through.
  GMANRaySphere clippedNear(1.0, -0.5, 1.0, 360.0, GMANParameterList());
  GMANRay ray(origin, direction);
  GMANHit hit;
  check(clippedNear.intersect(ray, hit) && near(hit.t, 6.0) && near(hit.point.getZ(), 1.0),
        "zmin clip: the near root is clipped and the far root survives at t == 6");

  // Both caps now exclude the sphere's two roots outright: a miss.
  GMANRaySphere clippedBoth(1.0, -0.5, 0.5, 360.0, GMANParameterList());
  GMANHit missHit;
  check(!clippedBoth.intersect(ray, missHit), "zmin/zmax clip: both roots fall outside the band");

  // thetamax, on the off-axis ray so the polar atan2(0, 0) ambiguity does
  // not confound the check: a wedge that excludes its longitude misses.
  GMANRaySphere wedge(1.0, -1.0, 1.0, 20.0, GMANParameterList());
  GMANRay offAxis = offAxisRayTowardOrigin();
  GMANHit wedgeHit;
  check(!wedge.intersect(offAxis, wedgeHit), "thetamax: a wedge excluding the hit's longitude misses");
}

// Check 8, the plugin links, is proven by the $GMAN_BUILD_RAYTRACER gate
// build in CMakeLists.txt, not by anything this executable can assert.

// ---- normal.normalize() pins on a non-unit sphere ----
// Every sphere above is radius 1, where the raw radial vector is already
// unit length and dropping normalize() would change nothing. A radius-2
// sphere makes the call observable.
void testNormalIsNormalized() {
  GMANRaySphere sphere(2.0, -2.0, 2.0, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(0.0, 0.0, -10.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = sphere.intersect(ray, hit);
  check(hitFound, "normalized normal: a ray hits the radius-2 sphere");
  RtFloat const normalLength = (RtFloat)std::sqrt(hit.normal.dot(hit.normal));
  check(near(normalLength, 1.0), "normalized normal: |n| == 1 on a radius-2 sphere");
}

// ---- hit.u's thetamax scaling pins on a partial wedge ----
// The full-sphere round trip above runs only at thetamax 360, where
// theta/thetamaxRad and theta/(2*PI) agree, and the wedge check runs only
// a miss, so u is never read there. A thetamax-90 sphere, hit by the same
// off-axis ray, survives the wedge and rounds trip through a scaling that
// depends on thetamax.
void testPartialThetamaxRoundTrip() {
  GMANRaySphere sphere(1.0, -1.0, 1.0, 90.0, GMANParameterList());
  GMANRay ray = offAxisRayTowardOrigin();
  GMANHit hit;

  bool const hitFound = sphere.intersect(ray, hit);
  check(hitFound, "partial thetamax: the off-axis ray survives a 90-degree wedge");

  GMANPoint const roundTrip = sphere.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "partial thetamax: getLocation(hit.u, hit.v) reproduces hit.point");
  check(hit.u >= 0.0 && hit.u <= 1.0, "partial thetamax: 0 <= u <= 1");
  check(hit.v >= 0.0 && hit.v <= 1.0, "partial thetamax: 0 <= v <= 1");
}

// ---- a negative radius keeps the normal outward ----
// x^2+y^2+z^2 == radius^2 is the same unit sphere whether radius is 1 or
// -1, so this ray hits the same point (0, 0, -1) as testAxialHitFields;
// only the normal's sign is at stake, dividing the point by radius would
// flip it inward.
void testNegativeRadiusNormalOutward() {
  GMANRaySphere sphere(-1.0, -1.0, 1.0, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = sphere.intersect(ray, hit);
  check(hitFound, "negative radius: a ray down +z hits the radius == -1 sphere");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "negative radius: outward normal == (0, 0, -1)");
}

// ---- dirSq's own guard pins on a zero-length direction ----
// GMANVector::normalize leaves a sub-RI_EPSILON vector unchanged, so a
// zero-length direction survives GMANRay's constructor and must miss at
// gman::shiftIntoBoundingSphere's own dirSq == 0.0 check, before
// boundingSphereShift's divide by it, rather than reach a spurious a == 1
// solve.
void testZeroLengthDirectionMisses() {
  GMANRaySphere sphere = fullSphere();
  GMANRay zeroRay(GMANPoint(0.0, 0.0, 0.0), GMANVector(0.0, 0.0, 0.0));
  GMANHit hit;

  check(!sphere.intersect(zeroRay, hit), "zero direction: a direction-less ray fired from inside the sphere misses");
}

// ---- degenerate spheres miss instead of filling NaN ----
// zmin == zmax collapses phimax - phimin to zero, thetamax == 0 collapses
// thetamaxRad to zero, and radius == 0 collapses the z/radius divisions:
// each is a real division by zero reachable from four floats a RIB Sphere
// request passes straight through, not a theoretical one.
void testDegenerateSpheresMiss() {
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  // The guard runs before any root is examined, on any ray. The test
  // still fires an equatorial ray rather than an axial one: the axial
  // ray's roots sit at z == -1 and z == 1, outside the collapsed band,
  // so the ordinary z < zmin || z > zmax check would reject them too.
  // The equatorial ray's z == 0 lies inside the collapsed band's single
  // point and would pass that check, so only the guard stops the
  // resulting 0/0.
  GMANRaySphere nullBand(1.0, 0.0, 0.0, 360.0, GMANParameterList());
  GMANRay const equatorialRay(GMANPoint(5.0, 0.0, 0.0), GMANVector(-1.0, 0.0, 0.0));
  check(!nullBand.intersect(equatorialRay, hit), "degenerate: zmin == zmax misses");

  GMANRaySphere nullWedge(1.0, -1.0, 1.0, 0.0, GMANParameterList());
  check(!nullWedge.intersect(GMANRay(origin, direction), hit), "degenerate: thetamax == 0 misses");

  GMANRaySphere nullRadius(0.0, -1.0, 1.0, 360.0, GMANParameterList());
  check(!nullRadius.intersect(GMANRay(origin, direction), hit), "degenerate: radius == 0 misses");
}

} // namespace

int main() {
  testAxialHitFields();
  testOffAxisRoundTrip();
  testIntervalRejectsAndFallsThrough();
  testTangentInsideAndBehind();
  testPartialSphereClips();
  testNormalIsNormalized();
  testPartialThetamaxRoundTrip();
  testNegativeRadiusNormalOutward();
  testZeroLengthDirectionMisses();
  testDegenerateSpheresMiss();

  return checkSummary("GMANRaySphere::intersect hits, misses and clips correctly");
}
