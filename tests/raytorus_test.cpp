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
 * R5c proof, parts B and C: GMANRayTorus::intersect finds the object-space
 * hit of a ray against the implicit torus (sqrt(x^2+y^2)-majorradius)^2 +
 * z^2 == minorradius^2, honors thetamax's wedge and the [phimin, phimax]
 * band (including a negative phimin, a descending phimin > phimax, and a
 * band offset by any number of turns), tests the ray's own [tmin, tmax]
 * interval, and fills a GMANHit that round trips through
 * GMANTorus::getLocation/getNormal. Part C sweeps 5120 rays at a solid
 * torus from five viewpoints, from a camera 5 major radii away to one
 * 10,000 away, to catch the speckle a careless quartic solve shows as a
 * miss on a ray aimed straight at the surface; the edge-on viewpoint aims
 * every ray at a z == 0 target, so its own rays carry object-space dz == 0
 * throughout, the quartic's q ~= 0 trigger for a near-zero resolvent root.
 * A pinned ray and a further sweep of horizontal (dz == 0) rays regress
 * that same defect directly.
 */

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "check.h"
#include "gmanmath.h"
#include "gmanray.h"
#include "gmanraytorus.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// The same parametrization GMANTorus::getLocation uses, addressed directly
// by (theta, phi) in degrees rather than by (u, v): lets a test aim at a
// point outside a particular instance's own wedge or band.
GMANPoint torusPointAt(double majorradius, double minorradius, double thetaDeg, double phiDeg) {
  double const theta = thetaDeg * DEGTORAD, phi = phiDeg * DEGTORAD;
  double const factor = majorradius + minorradius * std::cos(phi);
  return GMANPoint((RtFloat)(factor * std::cos(theta)), (RtFloat)(factor * std::sin(theta)),
                   (RtFloat)(minorradius * std::sin(phi)));
}

GMANVector torusNormalAt(double thetaDeg, double phiDeg) {
  double const theta = thetaDeg * DEGTORAD, phi = phiDeg * DEGTORAD;
  double const cosPhi = std::cos(phi);
  GMANVector n((RtFloat)(cosPhi * std::cos(theta)), (RtFloat)(cosPhi * std::sin(theta)), (RtFloat)std::sin(phi));
  n.normalize();
  return n;
}

// A ray aimed at target from 5 units out along its own outward normal: for
// a point on a convex local patch of the tube, the nearest crossing back
// in is target itself. tmax bounds how far past target the ray can keep
// travelling -- unrestricted, it would carry on through the tube's local
// circular cross-section and hit the far wall too, near the diametrically
// opposite phi, which a "this target's wedge or band excludes it" case
// needs to rule out rather than accidentally re-admit.
GMANRay rayAtSurfacePoint(GMANPoint const& target, GMANVector const& normal, RtFloat tmax = RI_INFINITY) {
  GMANPoint const origin(target.getX() + 5.0f * normal.getX(), target.getY() + 5.0f * normal.getY(),
                         target.getZ() + 5.0f * normal.getZ());
  return GMANRay(origin, GMANVector(origin, target), RI_EPSILON, tmax);
}

// ---- B1: axis-aligned hits, verified by hand ----
void testAxisAlignedHits() {
  GMANRayTorus torus(2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList());

  GMANPoint const origin(10.0, 0.0, 0.0);
  GMANRay outerEquator(origin, GMANVector(origin, GMANPoint(0.0, 0.0, 0.0)));
  GMANHit hit;
  bool const hitFound = torus.intersect(outerEquator, hit);
  check(hitFound, "axis-aligned: a ray along x toward the origin hits the outer equator");
  check(hitFound && near(hit.t, 7.5f), "axis-aligned: t == 7.5 (10 - (majorradius + minorradius))");
  check(hitFound && near(hit.point.getX(), 2.5f) && near(hit.point.getY(), 0.0f) && near(hit.point.getZ(), 0.0f),
        "axis-aligned: point == (majorradius + minorradius, 0, 0)");

  GMANRay throughHole(GMANPoint(0.0, 0.0, 10.0), GMANVector(0.0, 0.0, -1.0));
  GMANHit holeHit;
  check(!torus.intersect(throughHole, holeHit), "axis-aligned: a ray down z through the hole misses");
}

// ---- B2: a round trip, point and normal, on a partial wedge and a band
// with a negative phimin ----
void testRoundTrip() {
  GMANRayTorus torus(2.0, 0.5, -140.0, 100.0, 200.0, GMANParameterList());

  GMANPoint const target = torus.getLocation(0.3, 0.4);
  GMANVector const normal = torus.getNormal(0.3, 0.4);
  GMANRay ray = rayAtSurfacePoint(target, normal);
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound, "round trip: the ray hits the partial torus");
  if (!hitFound)
    return;

  GMANPoint const roundTrip = torus.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "round trip: getLocation(hit.u, hit.v) reproduces hit.point");

  GMANVector expectedNormal = gman::transformNormal(GMANMatrix4(), torus.getNormal(hit.u, hit.v));
  expectedNormal.normalize();
  check(near(hit.normal.getX(), expectedNormal.getX()) && near(hit.normal.getY(), expectedNormal.getY()) &&
            near(hit.normal.getZ(), expectedNormal.getZ()),
        "round trip: hit.normal matches getNormal(hit.u, hit.v)");
  check(hit.u >= 0.0f && hit.u <= 1.0f, "round trip: 0 <= u <= 1");
  check(hit.v >= 0.0f && hit.v <= 1.0f, "round trip: 0 <= v <= 1");
}

// ---- B3: the wedge and the band each clip, and a ray just inside each
// bound hits ----
void testBoundsBite() {
  GMANRayTorus torus(2.0, 0.5, -140.0, 100.0, 200.0, GMANParameterList());

  // tmax == 5.5 keeps each "outside" ray from also punching through to the
  // tube's far wall (near the diametrically opposite phi, around 5 + 2 *
  // minorradius units along the same line) and finding a second crossing
  // there that the wedge or band might not exclude.
  GMANPoint const outsideWedgeTarget = torusPointAt(2.0, 0.5, 250.0, 30.0);
  GMANVector const outsideWedgeNormal = torusNormalAt(250.0, 30.0);
  GMANHit outsideWedgeHit;
  check(!torus.intersect(rayAtSurfacePoint(outsideWedgeTarget, outsideWedgeNormal, 5.5f), outsideWedgeHit),
        "wedge: theta == 250 degrees is outside the 200-degree wedge");

  GMANPoint const insideWedgeTarget = torusPointAt(2.0, 0.5, 199.0, 30.0);
  GMANVector const insideWedgeNormal = torusNormalAt(199.0, 30.0);
  GMANHit insideWedgeHit;
  check(torus.intersect(rayAtSurfacePoint(insideWedgeTarget, insideWedgeNormal), insideWedgeHit),
        "wedge: theta == 199 degrees, just inside the 200-degree wedge, hits");

  GMANPoint const outsideBandTarget = torusPointAt(2.0, 0.5, 90.0, 150.0);
  GMANVector const outsideBandNormal = torusNormalAt(90.0, 150.0);
  GMANHit outsideBandHit;
  check(!torus.intersect(rayAtSurfacePoint(outsideBandTarget, outsideBandNormal, 5.5f), outsideBandHit),
        "band: phi == 150 degrees is outside the [-140, 100] band");

  GMANPoint const insideBandTarget = torusPointAt(2.0, 0.5, 90.0, 99.0);
  GMANVector const insideBandNormal = torusNormalAt(90.0, 99.0);
  GMANHit insideBandHit;
  check(torus.intersect(rayAtSurfacePoint(insideBandTarget, insideBandNormal), insideBandHit),
        "band: phi == 99 degrees, just inside the [-140, 100] band, hits");
}

// ---- B4: later roots -- a ray starting inside the tube hits its far
// wall, and a ray through the hole whose near side the wedge cuts away
// hits on its third root ----
void testLaterRoots() {
  GMANRayTorus full(2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList());

  // (0, majorradius, 0) is the deepest interior point of the tube at
  // theta == 90 degrees (distance 0 from the tube's own centre circle).
  GMANRay fromInside(GMANPoint(0.0, 2.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit insideHit;
  bool const insideHitFound = full.intersect(fromInside, insideHit);
  check(insideHitFound, "later roots: a ray starting inside the tube hits its far wall");
  check(insideHitFound && near(insideHit.t, 1.5f), "later roots: t == 1.5");

  // thetamax == 170 excludes the theta == 180 crossings (the near side, at
  // x == -2.5 and x == -1.5); the ray's first two roots there fall through,
  // hitting on its third, at x == majorradius - minorradius == 1.5
  // (theta == 0, phi == 180).
  GMANRayTorus wedged(2.0, 0.5, 0.0, 360.0, 170.0, GMANParameterList());
  GMANRay throughHole(GMANPoint(-10.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit holeHit;
  bool const holeHitFound = wedged.intersect(throughHole, holeHit);
  check(holeHitFound, "later roots: a ray through the hole, near side cut away, hits on its third root");
  check(holeHitFound && near(holeHit.point.getX(), 1.5f) && near(holeHit.point.getY(), 0.0f) &&
            near(holeHit.point.getZ(), 0.0f),
        "later roots: that hit lands at x == majorradius - minorradius");
}

// ---- B5: a descending band (phimin > phimax) hits where getLocation puts
// it, round trip included ----
void testDescendingBand() {
  GMANRayTorus torus(2.0, 0.5, 100.0, -140.0, 360.0, GMANParameterList());

  GMANPoint const target = torus.getLocation(0.3, 0.4);
  GMANVector const normal = torus.getNormal(0.3, 0.4);
  GMANRay ray = rayAtSurfacePoint(target, normal);
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound, "descending band: the ray hits the torus");
  if (!hitFound)
    return;

  GMANPoint const roundTrip = torus.getLocation(hit.u, hit.v);
  check(near(roundTrip.getX(), hit.point.getX()) && near(roundTrip.getY(), hit.point.getY()) &&
            near(roundTrip.getZ(), hit.point.getZ()),
        "descending band: getLocation(hit.u, hit.v) reproduces hit.point");
}

// ---- B1 regression: a horizontal ray (object-space dz == 0, the
// quartic's own q ~= 0 trigger for the near-zero-resolvent-root defect)
// at origin (0, -5, 0.1), direction (1, 5, 0), R == 1, r == 0.3. The true
// hit is t == 4.0758, at (0.7993, -1.0034, 0.1). ----
void testPinnedHorizontalRay() {
  GMANRayTorus torus(1.0, 0.3, 0.0, 360.0, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(0.0, -5.0, 0.1), GMANVector(1.0, 5.0, 0.0));
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound, "pinned horizontal ray: the ray hits");
  check(hitFound && near(hit.t, 4.0758f), "pinned horizontal ray: t == 4.0758");
  check(hitFound && near(hit.point.getX(), 0.7993f) && near(hit.point.getY(), -1.0034f) && near(hit.point.getZ(), 0.1f),
        "pinned horizontal ray: point == (0.7993, -1.0034, 0.1)");
}

// ---- B1 regression, round 2: origin (3.35775137, 3.91563964,
// 0.027229961), direction (-0.444452167, -0.895802617, 0), R == 1, r ==
// 0.3, a horizontal ray whose resolvent root's linear-term estimate
// itself is small (q ~= 1e-31), where reading a noisy seed against
// kResolventSeedFloor instead of reading that estimate directly missed
// the reseed and lost the root. The true hit is t == 4.71706. ----
void testPinnedReviewerHorizontalRay() {
  GMANRayTorus torus(1.0, 0.3, 0.0, 360.0, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(3.35775137f, 3.91563964f, 0.027229961f), GMANVector(-0.444452167f, -0.895802617f, 0.0f));
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound, "pinned reviewer horizontal ray: the ray hits");
  check(hitFound && near(hit.t, 4.71706f), "pinned reviewer horizontal ray: t == 4.71706");
  if (!hitFound)
    return;

  double const px = hit.point.getX(), py = hit.point.getY(), pz = hit.point.getZ();
  double const radial = std::sqrt(px * px + py * py) - 1.0;
  double const residual = std::fabs(radial * radial + pz * pz - 0.3 * 0.3);
  check(residual <= 1e-4, "pinned reviewer horizontal ray: implicit residual within 1e-4 of zero");
}

// ---- B1 regression: a sweep of horizontal rays (dz == 0) at z == 0.1,
// the pinned ray's own direction (1, 5, 0), laterally offset in x across
// [-0.48, 0.28] -- verified, both before and after the fix, to be exactly
// where this direction's rays cross the tube at that height. A direction
// with dx == 0 (straight down y, the naive reading of "offset from the
// axis") turns out not to trigger the defect at all: with the ray's own x
// fixed, Q1 in gmanraytorus.cpp's coefficient build loses the odd-degree
// term that makes q small but nonzero in the first place. Every ray must
// hit, at a point whose implicit residual is within 1e-4 of zero. ----
void testHorizontalRaySweep() {
  double const R = 1.0, r = 0.3, z = 0.1;
  GMANRayTorus torus(R, r, 0.0, 360.0, 360.0, GMANParameterList());

  constexpr int kSamples = 25;
  constexpr double kLo = -0.48, kHi = 0.28;
  for (int i = 0; i < kSamples; ++i) {
    double const frac = (i + 0.5) / kSamples;
    double const xoffset = kLo + frac * (kHi - kLo);

    GMANRay ray(GMANPoint((RtFloat)xoffset, -5.0, (RtFloat)z), GMANVector(1.0, 5.0, 0.0));
    GMANHit hit;
    bool const hitFound = torus.intersect(ray, hit);
    check(hitFound, "horizontal ray sweep: x offset " + std::to_string(xoffset) + " hits");
    if (!hitFound)
      continue;

    double const px = hit.point.getX(), py = hit.point.getY(), pz = hit.point.getZ();
    double const radial = std::sqrt(px * px + py * py) - R;
    double const residual = std::fabs(radial * radial + pz * pz - r * r);
    check(residual <= 1e-4, "horizontal ray sweep: x offset " + std::to_string(xoffset) + " residual " +
                                std::to_string(residual) + " <= 1e-4");
  }
}

// ---- A1: the band search's representative works for a band offset by
// any number of turns -- [1400, 1500] hits wherever [-40, 60] does, with
// the same v ----
void testBandOffsetByManyTurns() {
  GMANRayTorus nearBand(2.0, 0.5, -40.0, 60.0, 360.0, GMANParameterList());
  GMANRayTorus farBand(2.0, 0.5, 1400.0, 1500.0, 360.0, GMANParameterList());

  GMANPoint const target = nearBand.getLocation(0.3, 0.4);
  GMANVector const normal = nearBand.getNormal(0.3, 0.4);
  GMANRay ray = rayAtSurfacePoint(target, normal);

  GMANHit nearHit, farHit;
  bool const nearHitFound = nearBand.intersect(ray, nearHit);
  bool const farHitFound = farBand.intersect(ray, farHit);
  check(nearHitFound, "band offset by many turns: the [-40, 60] band hits");
  check(farHitFound, "band offset by many turns: the [1400, 1500] band, the same physical band, hits too");
  check(nearHitFound && farHitFound && near(nearHit.v, farHit.v),
        "band offset by many turns: both bands report the same v");
}

// ---- B6a: a rotation places the torus's axis along camera x ----
void testRotatingTransform() {
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayTorus torus(2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList(), transform);
  GMANRay ray(GMANPoint(0.0, 0.0, -10.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound && near(hit.t, 7.5f), "rotation: t == 7.5");
  check(hitFound && near(hit.point.getX(), 0.0f) && near(hit.point.getY(), 0.0f) && near(hit.point.getZ(), -2.5f),
        "rotation: point == (0, 0, -2.5)");
  check(hitFound && near(hit.normal.getX(), 0.0f) && near(hit.normal.getY(), 0.0f) && near(hit.normal.getZ(), -1.0f),
        "rotation: normal == (0, 0, -1)");
}

// ---- B6b: a shear tells transformNormal(cameraToObject, n) apart from
// transformDirection(objectToCamera, n). The ray stays at object z == 0
// throughout, where this shear (mtrx[2][0] == 2) leaves the point and t
// unchanged; only the normal, which picks up a z component, exposes it. ----
void testShearTransform() {
  GMANMatrix4 matrix;
  matrix[2][0] = 2.0;
  GMANTransform transform = makeTransform(matrix);

  GMANRayTorus torus(2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList(), transform);
  GMANPoint const origin(10.0, 0.0, 0.0);
  GMANRay ray(origin, GMANVector(origin, GMANPoint(0.0, 0.0, 0.0)));
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound && near(hit.t, 7.5f), "shear: t == 7.5, unchanged (the ray stays at object z == 0)");
  check(hitFound && near(hit.point.getX(), 2.5f) && near(hit.point.getY(), 0.0f) && near(hit.point.getZ(), 0.0f),
        "shear: point == (2.5, 0, 0), unchanged");
  // The object normal is (1, 0, 0); transformNormal(cameraToObject, ...)
  // gives (1, 0, -2), normalized below. transformDirection(objectToCamera,
  // ...) -- the naive substitute -- would instead leave (1, 0, 0)
  // unchanged, since the shear only modifies the z row.
  check(hitFound && near(hit.normal.getX(), 0.447214f) && near(hit.normal.getY(), 0.0f) &&
            near(hit.normal.getZ(), -0.894427f),
        "shear: normal == (1, 0, -2), normalized");
}

// ---- B6c: a uniform scale leaves t a camera-space distance ----
void testUniformScale() {
  GMANMatrix4 matrix;
  matrix.scale(2.0, 2.0, 2.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayTorus torus(2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList(), transform);
  GMANPoint const origin(10.0, 0.0, 0.0);
  GMANRay ray(origin, GMANVector(origin, GMANPoint(0.0, 0.0, 0.0)));
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0f), "uniform scale: t == 5, the camera-space distance");
  check(hitFound && near(hit.point.getX(), 5.0f) && near(hit.point.getY(), 0.0f) && near(hit.point.getZ(), 0.0f),
        "uniform scale: point == (5, 0, 0), twice the object-space hit");
  check(hitFound && near(hit.normal.getX(), 1.0f) && near(hit.normal.getY(), 0.0f) && near(hit.normal.getZ(), 0.0f),
        "uniform scale: normal == (1, 0, 0), unchanged by a uniform scale");
}

// ---- B7: a singular transform never hits. Deleting the singular guard
// must turn this red: the ray below is the same one testAxisAlignedHits
// finds a clean hit with, unsheared. ----
void testSingularTransform() {
  GMANMatrix4 matrix;
  matrix.scale(1.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRayTorus torus(2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList(), transform);
  GMANPoint const origin(10.0, 0.0, 0.0);
  GMANRay ray(origin, GMANVector(origin, GMANPoint(0.0, 0.0, 0.0)));
  GMANHit hit;

  check(!torus.intersect(ray, hit), "singular transform: a torus with no invertible object space never hits");
}

// ---- B8: the ray's interval rejects a hit before tmin and one past
// tmax. A wedge (theta in [0, 170]) and a band (phi in [90, 270]) leave
// exactly one valid crossing for this ray, at t == 11.5, x ==
// majorradius - minorradius. ----
void testIntervalRejects() {
  GMANRayTorus torus(2.0, 0.5, 90.0, 270.0, 170.0, GMANParameterList());
  GMANPoint const origin(-10.0, 0.0, 0.0);
  GMANVector const direction(1.0, 0.0, 0.0);
  GMANHit hit;

  GMANRay control(origin, direction);
  check(torus.intersect(control, hit) && near(hit.t, 11.5f), "interval: the control ray hits at t == 11.5");

  GMANRay shortRay(origin, direction, RI_EPSILON, 11.0f);
  check(!torus.intersect(shortRay, hit), "interval: tmax below the hit (11.5) rejects it");

  GMANRay farRay(origin, direction, 12.0f, RI_INFINITY);
  check(!torus.intersect(farRay, hit), "interval: tmin above the hit (11.5) rejects it");
}

// ---- B9: degenerate parameters each miss, on a ray chosen to hit with
// the guard removed ----
void testDegenerateParametersMiss() {
  GMANPoint const origin(10.0, 0.0, 0.0);
  GMANRay ray(origin, GMANVector(origin, GMANPoint(0.0, 0.0, 0.0)));
  GMANHit hit;

  // Zero minorradius: the ray along x in the plane z == 0 hits the
  // centre circle (radius majorradius) as a double root, with the guard
  // removed.
  GMANRayTorus zeroMinor(2.0, 0.0, 0.0, 360.0, 360.0, GMANParameterList());
  check(!zeroMinor.intersect(ray, hit), "degenerate: zero minorradius misses");

  // Zero thetamax: the ray crosses theta == 0 with the guard removed.
  GMANRayTorus zeroTheta(2.0, 0.5, 0.0, 360.0, 0.0, GMANParameterList());
  check(!zeroTheta.intersect(ray, hit), "degenerate: zero thetamax misses");

  // phimin == phimax == 0: the ray crosses phi == 0 with the guard
  // removed.
  GMANRayTorus nullBand(2.0, 0.5, 0.0, 0.0, 360.0, GMANParameterList());
  check(!nullBand.intersect(ray, hit), "degenerate: phimin == phimax misses");

  // A negative minorradius, chosen the same way.
  GMANRayTorus negMinor(2.0, -0.5, 0.0, 360.0, 360.0, GMANParameterList());
  check(!negMinor.intersect(ray, hit), "degenerate: negative minorradius misses");

  // A negative majorradius also trips the spindle guard (minorradius >=
  // majorradius), so this pins the miss without a guard-removal proof.
  GMANRayTorus negMajor(-2.0, 0.5, 0.0, 360.0, 360.0, GMANParameterList());
  check(!negMajor.intersect(ray, hit), "degenerate: negative majorradius misses");
}

// ---- C: no speckle. A 32x32 grid over (u, v) on a full, untransformed
// torus, each target moved 1% of minorradius toward the tube's centre
// circle so it lies inside the solid tube. Every ray from five viewpoints
// must hit, at t no greater than the distance to its target, at a point
// whose implicit residual is within 1e-4 of zero. ----
struct SweepResult {
  int misses = 0;
  int tExceeded = 0;
  double largestResidual = 0.0;
};

// GMANTorus::getLocation and getNormal are non-const, so this takes torus
// by mutable reference even though the sweep itself never modifies it.
// One target: getLocation(u, v) moved 1% of minorradius toward the
// tube's centre circle, so it lies inside the solid tube, then a ray from
// viewpoint aimed at it, folded into result.
void sweepOneTarget(GMANRayTorus& torus, GMANPoint const& viewpoint, double majorradius, double minorradius, double u,
                    double v, SweepResult& result) {
  GMANPoint const surface = torus.getLocation(u, v);
  GMANVector const normal = torus.getNormal(u, v);
  GMANPoint const target(surface.getX() - (RtFloat)(0.01 * minorradius) * normal.getX(),
                         surface.getY() - (RtFloat)(0.01 * minorradius) * normal.getY(),
                         surface.getZ() - (RtFloat)(0.01 * minorradius) * normal.getZ());

  double const ddx = target.getX() - viewpoint.getX();
  double const ddy = target.getY() - viewpoint.getY();
  double const ddz = target.getZ() - viewpoint.getZ();
  double const targetDistance = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);

  GMANRay ray(viewpoint, GMANVector(viewpoint, target));
  GMANHit hit;
  if (!torus.intersect(ray, hit)) {
    ++result.misses;
    return;
  }
  if (hit.t > targetDistance + 1e-3f)
    ++result.tExceeded;

  double const px = hit.point.getX(), py = hit.point.getY(), pz = hit.point.getZ();
  double const radial = std::sqrt(px * px + py * py) - majorradius;
  double const residual = std::fabs(radial * radial + pz * pz - minorradius * minorradius);
  result.largestResidual = std::max(result.largestResidual, residual);
}

// A 32x32 grid over (u, v): 1024 targets spanning the whole tube.
SweepResult sweepFromViewpoint(GMANRayTorus& torus, GMANPoint const& viewpoint, double majorradius,
                               double minorradius) {
  SweepResult result;
  constexpr int kGrid = 32;
  for (int i = 0; i < kGrid; ++i)
    for (int j = 0; j < kGrid; ++j)
      sweepOneTarget(torus, viewpoint, majorradius, minorradius, (i + 0.5) / kGrid, (j + 0.5) / kGrid, result);
  return result;
}

// B1 regression: every target sits on a z == 0 ring (v == 0, phi == 0;
// v == 0.5, phi == 180), so a ray from a viewpoint that is itself at
// z == 0 has object-space dz == 0 exactly -- the quartic's own q ~= 0
// trigger for the near-zero-resolvent-root defect. 1024 rays, the same
// budget as the general sweep, split across the two rings.
SweepResult sweepEdgeOnAtZeroZ(GMANRayTorus& torus, GMANPoint const& viewpoint, double majorradius,
                               double minorradius) {
  SweepResult result;
  constexpr int kSamplesPerRing = 512;
  double const zeroZRings[2] = {0.0, 0.5};
  for (double const v : zeroZRings)
    for (int i = 0; i < kSamplesPerRing; ++i)
      sweepOneTarget(torus, viewpoint, majorradius, minorradius, (i + 0.5) / kSamplesPerRing, v, result);
  return result;
}

void testNoSpeckle() {
  double const majorradius = 1.0, minorradius = 0.3;
  GMANRayTorus torus(majorradius, minorradius, 0.0, 360.0, 360.0, GMANParameterList());

  double const invSqrt3 = 1.0 / std::sqrt(3.0);
  struct Viewpoint {
    char const* name;
    GMANPoint point;
    bool zeroZTargets = false;
  };
  Viewpoint const viewpoints[] = {
      {"face on, 5 major radii along z", GMANPoint(0.0, 0.0, 5.0), false},
      {"edge on, 5 major radii in the xy-plane, aimed at z == 0 targets", GMANPoint(5.0, 0.0, 0.0), true},
      {"level with the tube's top, 5 major radii out", GMANPoint(5.0, 0.0, (RtFloat)minorradius), false},
      {"300 major radii away, off-axis",
       GMANPoint((RtFloat)(300.0 * invSqrt3), (RtFloat)(300.0 * invSqrt3), (RtFloat)(300.0 * invSqrt3)), false},
      {"10,000 major radii away, off-axis",
       GMANPoint((RtFloat)(10000.0 * invSqrt3), (RtFloat)(10000.0 * invSqrt3), (RtFloat)(10000.0 * invSqrt3)), false},
  };

  for (auto const& vp : viewpoints) {
    SweepResult const result = vp.zeroZTargets ? sweepEdgeOnAtZeroZ(torus, vp.point, majorradius, minorradius)
                                               : sweepFromViewpoint(torus, vp.point, majorradius, minorradius);
    check(result.misses == 0,
          std::string(vp.name) + ": 0/1024 rays missed the torus (got " + std::to_string(result.misses) + ")");
    check(result.tExceeded == 0, std::string(vp.name) + ": 0/1024 rays exceeded the target's distance (got " +
                                     std::to_string(result.tExceeded) + ")");
    check(result.largestResidual <= 1e-4, std::string(vp.name) + ": largest implicit residual " +
                                              std::format("{:.3e}", result.largestResidual) +
                                              " is within 1e-4 of zero");
  }
}

} // namespace

int main() {
  testAxisAlignedHits();
  testRoundTrip();
  testBoundsBite();
  testLaterRoots();
  testDescendingBand();
  testPinnedHorizontalRay();
  testPinnedReviewerHorizontalRay();
  testHorizontalRaySweep();
  testBandOffsetByManyTurns();
  testRotatingTransform();
  testShearTransform();
  testUniformScale();
  testSingularTransform();
  testIntervalRejects();
  testDegenerateParametersMiss();
  testNoSpeckle();

  return checkSummary("GMANRayTorus::intersect hits, misses and clips correctly, without speckle");
}
