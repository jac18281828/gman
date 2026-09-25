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
 * GMANRayPolygon::intersect finds the ray's hit on the
 * polygon's own plane, then tests it against the ring's projection with an
 * even-odd rule that handles a concave shape correctly, honoring the ray's
 * own [tmin, tmax] interval, and never hits a degenerate ring.
 */

#include <cmath>

#include "check.h"
#include "gmanpolygon.h"
#include "gmanray.h"
#include "gmanraypolygon.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

// A unit square in the z == 0 plane, vertices already in camera space --
// GMANRayPolygon's own construction contract.
GMANRayPolygon unitSquare() {
  std::vector<GMANPoint> verts = {GMANPoint(-1.0, -1.0, 0.0), GMANPoint(1.0, -1.0, 0.0), GMANPoint(1.0, 1.0, 0.0),
                                  GMANPoint(-1.0, 1.0, 0.0)};
  return GMANRayPolygon(verts, GMANParameterList());
}

// An L-shape: the unit-square-ish region [0,2]x[0,2] with its top-right
// [1,2]x[1,2] corner missing, so a ray into the notch must miss while one
// into either arm hits -- the concave case a 3-vertex cross product cannot
// tell from its own reflex corner, but Newell's method (summed over every
// edge) can.
GMANRayPolygon lShape() {
  std::vector<GMANPoint> verts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(2.0, 0.0, 0.0), GMANPoint(2.0, 1.0, 0.0),
                                  GMANPoint(1.0, 1.0, 0.0), GMANPoint(1.0, 2.0, 0.0), GMANPoint(0.0, 2.0, 0.0)};
  return GMANRayPolygon(verts, GMANParameterList());
}

// ---- check 1: a square hit at its centre, missed just past an edge ----
void testSquareCentreAndEdge() {
  GMANRayPolygon square = unitSquare();

  GMANRay centreRay(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit centreHit;
  bool const centreHitFound = square.intersect(centreRay, centreHit);
  check(centreHitFound && near(centreHit.t, 5.0), "square: a ray through the centre hits at t == 5");
  check(near(centreHit.point.getX(), 0.0) && near(centreHit.point.getY(), 0.0) && near(centreHit.point.getZ(), 0.0),
        "square: point == (0, 0, 0)");
  check(near(centreHit.u, 0.0) && near(centreHit.v, 0.0), "square: no \"P\" in pl, u == v == 0 (the fallback)");
  check(centreHit.primitive == &square, "square: primitive points at the square hit");

  // x == edge + 1e-3, not edge + 0.5: an inside test loosened to accept a
  // point well past the true edge would still (wrongly) call a ray this
  // far out a hit, so the miss has to be pinned close to the edge itself.
  GMANRay pastEdgeRay(GMANPoint(1.001, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit pastEdgeHit;
  check(!square.intersect(pastEdgeRay, pastEdgeHit), "square: a ray just past the x == 1 edge (x == 1 + 1e-3) misses");
}

// ---- check 2: the L-shape's notch misses, its arms hit ----
void testLShapeNotchAndArms() {
  GMANRayPolygon shape = lShape();

  GMANRay notchRay(GMANPoint(1.5, 1.5, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit notchHit;
  check(!shape.intersect(notchRay, notchHit), "L-shape: a ray into the missing corner (1.5, 1.5) misses");

  GMANRay leftArmRay(GMANPoint(0.5, 1.5, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit leftArmHit;
  check(shape.intersect(leftArmRay, leftArmHit), "L-shape: a ray into the left arm (0.5, 1.5) hits");

  GMANRay bottomArmRay(GMANPoint(1.5, 0.5, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit bottomArmHit;
  check(shape.intersect(bottomArmRay, bottomArmHit), "L-shape: a ray into the bottom arm (1.5, 0.5) hits");
}

// ---- check 3: a ray parallel to the plane misses ----
void testParallelRayMisses() {
  GMANRayPolygon square = unitSquare();
  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;

  check(!square.intersect(ray, hit), "parallel: a ray with no z component never reaches the z == 0 plane");
}

// ---- check 4: a polygon behind the ray origin misses ----
void testPolygonBehindOriginMisses() {
  GMANRayPolygon square = unitSquare();
  GMANRay ray(GMANPoint(0.0, 0.0, 5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  check(!square.intersect(ray, hit), "behind: a ray moving away from the plane at z == 0 misses");
}

// ---- check 5: degenerate rings never hit ----
void testDegenerateRingsNeverHit() {
  GMANRay ray(GMANPoint(0.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  std::vector<GMANPoint> twoVerts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(1.0, 0.0, 0.0)};
  GMANRayPolygon tooFewVertices(twoVerts, GMANParameterList());
  check(!tooFewVertices.intersect(ray, hit), "degenerate: fewer than three vertices never hits");

  std::vector<GMANPoint> collinearVerts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(1.0, 0.0, 0.0),
                                           GMANPoint(2.0, 0.0, 0.0)};
  GMANRayPolygon collinear(collinearVerts, GMANParameterList());
  check(!collinear.intersect(ray, hit), "degenerate: three collinear vertices never hits");

  std::vector<GMANPoint> sliverVerts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(1.0, 0.0, 0.0),
                                        GMANPoint(0.5, 1.0e-8, 0.0)};
  GMANRayPolygon sliver(sliverVerts, GMANParameterList());
  check(!sliver.intersect(ray, hit), "degenerate: a sliver isDegeneratePolygon rejects never hits");
}

// ---- check 6: the hit normal is the Newell normal's direction ----
void testHitNormalIsNewellDirection() {
  // A right triangle: Newell's method and a two-edge cross product agree
  // exactly for a triangle, so this pins the sign and axis independently
  // of GMANRayPolygon's own construction.
  std::vector<GMANPoint> verts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(1.0, 0.0, 0.0), GMANPoint(0.0, 1.0, 0.0)};
  GMANRayPolygon triangle(verts, GMANParameterList());

  GMANRay ray(GMANPoint(0.2, 0.2, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  bool const hitFound = triangle.intersect(ray, hit);
  check(hitFound, "normal: a ray into the triangle's interior hits");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 1.0),
        "normal: (1, 0, 0) cross (0, 1, 0) == (0, 0, 1)");

  // The L-shape's own Newell normal, computed the same way
  // GMANRayPolygon's constructor does, as an independent check that a
  // concave ring's hit normal still matches it.
  std::vector<GMANPoint> lVerts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(2.0, 0.0, 0.0), GMANPoint(2.0, 1.0, 0.0),
                                   GMANPoint(1.0, 1.0, 0.0), GMANPoint(1.0, 2.0, 0.0), GMANPoint(0.0, 2.0, 0.0)};
  GMANVector expected = gman::newellNormal(lVerts);
  expected /= expected.magnitude();

  GMANRayPolygon shape(lVerts, GMANParameterList());
  GMANRay armRay(GMANPoint(0.5, 1.5, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit armHit;
  check(shape.intersect(armRay, armHit), "normal: the L-shape's left arm hits");
  check(near(armHit.normal.getX(), expected.getX()) && near(armHit.normal.getY(), expected.getY()) &&
            near(armHit.normal.getZ(), expected.getZ()),
        "normal: the L-shape's hit normal matches its own Newell normal");
}

// ---- check 7: the ray's interval rejects a hit before tmin and one past
// tmax ----
void testIntervalRejects() {
  GMANRayPolygon square = unitSquare();
  GMANPoint const origin(0.0, 0.0, -5.0);
  GMANVector const direction(0.0, 0.0, 1.0);
  GMANHit hit;

  GMANRay shortRay(origin, direction, RI_EPSILON, 3.0);
  check(!square.intersect(shortRay, hit), "interval: tmax below the plane hit (5) rejects it");

  GMANRay farRay(origin, direction, 7.0, RI_INFINITY);
  check(!square.intersect(farRay, hit), "interval: tmin above the plane hit (5) rejects it");
}

// A GMANRayPolygon built the way RiPolygonV itself builds one
// (gmanrendermanimpl.cpp:784: GMANParameterList(dictionary, n, tokens,
// parms, nverts, nverts, 1, 1)), so its pl carries a real "P" -- and, per
// case, "st"/"s" -- rather than raypolygon_test.cpp's own default-
// constructed, "P"-less GMANParameterList(). vertices and "P" are the
// identical unit-square corners (an implicit identity CTM).
GMANRayPolygon squareWithP(RtToken* tokens, RtPointer* parms, RtInt n) {
  std::vector<GMANPoint> verts = {GMANPoint(-1.0, -1.0, 0.0), GMANPoint(1.0, -1.0, 0.0), GMANPoint(1.0, 1.0, 0.0),
                                  GMANPoint(-1.0, 1.0, 0.0)};
  GMANParameterList pl(gman::standardDictionary(), n, tokens, parms, 4, 4, 1, 1);
  return GMANRayPolygon(verts, pl);
}

// A ray through object point (ox, oy, 0), the same object-space plane
// vertices and "P" share here.
GMANRay rayThroughObject(RtFloat ox, RtFloat oy) { return GMANRay(GMANPoint(ox, oy, -5.0), GMANVector(0.0, 0.0, 1.0)); }

// ---- check 8: default (s, t) is each vertex's own object-space P ----
void testDefaultStIsObjectSpaceP() {
  RtFloat p[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  RtToken tokens[] = {RI_P};
  RtPointer parms[] = {(RtPointer)p};
  GMANRayPolygon square = squareWithP(tokens, parms, 1);

  GMANRay ray = rayThroughObject(0.5, 0.25);
  GMANHit hit;
  check(square.intersect(ray, hit), "default st: a ray through object (0.5, 0.25) hits");
  check(near(hit.u, 0.5) && near(hit.v, 0.25), "default st: interpolated (u, v) == the hit's own object-space (x, y)");
}

// ---- check 9: "st" overrides both components, transposed here ----
void testStOverride() {
  RtFloat p[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  // Each vertex's own default (x, y) transposed to (y, x).
  RtFloat st[] = {-1, -1, -1, 1, 1, 1, 1, -1};
  RtToken tokens[] = {RI_P, RI_ST};
  RtPointer parms[] = {(RtPointer)p, (RtPointer)st};
  GMANRayPolygon square = squareWithP(tokens, parms, 2);

  GMANRay ray = rayThroughObject(0.5, 0.25);
  GMANHit hit;
  check(square.intersect(ray, hit), "\"st\" override: a ray through object (0.5, 0.25) hits");
  check(near(hit.u, 0.25) && near(hit.v, 0.5), "\"st\" override: (u, v) swapped from the default (0.5, 0.25)");
}

// ---- check 10: "s" alone overrides only s, "t" stays the default ----
void testSAloneOverride() {
  RtFloat p[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  // (objX + 1) / 2 per vertex: 0 at x == -1, 1 at x == 1.
  RtFloat s[] = {0, 1, 1, 0};
  RtToken tokens[] = {RI_P, RI_S};
  RtPointer parms[] = {(RtPointer)p, (RtPointer)s};
  GMANRayPolygon square = squareWithP(tokens, parms, 2);

  GMANRay ray = rayThroughObject(0.5, 0.25);
  GMANHit hit;
  check(square.intersect(ray, hit), "\"s\" alone: a ray through object (0.5, 0.25) hits");
  check(near(hit.u, 0.75) && near(hit.v, 0.25), "\"s\" alone: u from \"s\", v stays the default object-space y");
}

// ---- check: "s" overrides "st"'s own s, and "t" stays "st"'s own t --
// applying "st" after "s"/"t" instead would restore "st"'s s here too ----
void testSOverridesSt() {
  RtFloat p[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  // Each vertex's own default (x, y) transposed to (y, x).
  RtFloat st[] = {-1, -1, -1, 1, 1, 1, 1, -1};
  // (objX + 1) / 2 per vertex: 0 at x == -1, 1 at x == 1.
  RtFloat s[] = {0, 1, 1, 0};
  RtToken tokens[] = {RI_P, RI_ST, RI_S};
  RtPointer parms[] = {(RtPointer)p, (RtPointer)st, (RtPointer)s};
  GMANRayPolygon square = squareWithP(tokens, parms, 3);

  GMANRay ray = rayThroughObject(0.5, 0.25);
  GMANHit hit;
  check(square.intersect(ray, hit), "\"s\" over \"st\": a ray through object (0.5, 0.25) hits");
  check(near(hit.u, 0.75) && near(hit.v, 0.5), "\"s\" over \"st\": u comes from \"s\", v stays \"st\"'s own t");
}

// ---- check 11: a fan triangle degenerate at vertex 0 is skipped, not
// read as containing the hit ----
void testDegenerateFanTriangleSkipped() {
  // v0, v1, v2 collinear (all on y == 0): interpolateTexCoord's own fan
  // triangulation from vertex 0 makes (v0, v1, v2) its first triangle,
  // degenerate (zero area) and so never containing (2, 2) -- the point has
  // to fall through to the real triangle (v0, v2, v3) instead.
  std::vector<GMANPoint> verts = {GMANPoint(0.0, 0.0, 0.0), GMANPoint(2.0, 0.0, 0.0), GMANPoint(4.0, 0.0, 0.0),
                                  GMANPoint(4.0, 4.0, 0.0), GMANPoint(0.0, 4.0, 0.0)};
  RtFloat p[] = {0, 0, 0, 2, 0, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0};
  RtToken tokens[] = {RI_P};
  RtPointer parms[] = {(RtPointer)p};
  GMANParameterList pl(gman::standardDictionary(), 1, tokens, parms, 5, 5, 1, 1);
  GMANRayPolygon pentagon(verts, pl);

  GMANRay ray = rayThroughObject(2.0, 2.0);
  GMANHit hit;
  check(pentagon.intersect(ray, hit), "collinear fan vertex: a ray through object (2, 2) hits");
  check(near(hit.u, 2.0) && near(hit.v, 2.0),
        "collinear fan vertex: (u, v) == (2, 2), not (0, 0) from the degenerate first fan triangle");
}

// ---- check 12: the loop constructor's own hole cuts what the outer loop
// covers, tested on the constructor directly rather than through any
// object-manager request ----
void testLoopConstructorHole() {
  std::vector<GMANPoint> outer = {GMANPoint(-1.0, -1.0, 0.0), GMANPoint(1.0, -1.0, 0.0), GMANPoint(1.0, 1.0, 0.0),
                                  GMANPoint(-1.0, 1.0, 0.0)};
  std::vector<GMANPoint> hole = {GMANPoint(-0.5, -0.5, 0.0), GMANPoint(0.5, -0.5, 0.0), GMANPoint(0.5, 0.5, 0.0),
                                 GMANPoint(-0.5, 0.5, 0.0)};
  GMANRayPolygon square(outer, {hole}, GMANParameterList());

  GMANRay centreRay(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit centreHit;
  check(!square.intersect(centreRay, centreHit), "loop constructor: a ray through the hole's centre misses");

  GMANRay ringRay(GMANPoint(0.75, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit ringHit;
  check(square.intersect(ringRay, ringHit) && near(ringHit.t, 5.0),
        "loop constructor: a ray through the ring between hole and edge hits at t == 5");

  GMANRay outsideRay(GMANPoint(1.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit outsideHit;
  check(!square.intersect(outsideRay, outsideHit), "loop constructor: a ray outside the outer loop misses");

  // An empty holes list, on the same (outer, holes, pl) constructor above,
  // still hits at the centre ray a hole would have rejected.
  GMANRayPolygon noHole(outer, {}, GMANParameterList());
  GMANHit noHoleHit;
  check(noHole.intersect(centreRay, noHoleHit), "loop constructor: an empty holes list hits at the same centre ray");
}

} // namespace

int main() {
  testSquareCentreAndEdge();
  testLShapeNotchAndArms();
  testParallelRayMisses();
  testPolygonBehindOriginMisses();
  testDegenerateRingsNeverHit();
  testHitNormalIsNewellDirection();
  testIntervalRejects();
  testDefaultStIsObjectSpaceP();
  testStOverride();
  testSAloneOverride();
  testSOverridesSt();
  testDegenerateFanTriangleSkipped();
  testLoopConstructorHole();

  return checkSummary("GMANRayPolygon::intersect hits, misses and clips correctly");
}
