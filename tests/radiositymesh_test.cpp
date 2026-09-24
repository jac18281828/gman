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
 * GMANRadiosityMesh: area (analytic and by convergence), and locate()'s
 * own round trip -- parameterization agreement, corner-weight
 * reconstruction, weight validity, seams and absent primitives.
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanradiositymesh.h"
#include "gmanray.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmantransform.h"
#include "gmanvector.h"

namespace {

// A tolerance stated as a named constant, per AGENTS.md's Tests section.

// An analytic area holds within 1%.
constexpr RtFloat kAreaRelTolerance = 0.01f;

// Halving the max edge length must cut the absolute area error by at
// least this factor; second order predicts 4x, so 3x leaves slack for a
// facet mesh's own approximation.
constexpr RtFloat kMinConvergenceRatio = 3.0f;

// A clipped polygon's own elements sum to its area within this
// relative tolerance -- exact geometry, so only float rounding to spend.
constexpr RtFloat kPolygonAreaRelTolerance = 1e-5f;

// hit.u/hit.v must match the (u, v) a ray was cast at.
constexpr RtFloat kParamAgreementTolerance = 1e-4f;

// The four corner weights sum to 1.
constexpr RtFloat kWeightSumTolerance = 1e-6f;

// checkEdgeLengths' own slack over the literal maxEdgeLength bound, split
// into its two sources rather than one unexplained pair of numbers: a
// relative factor for the float rounding the resolution search's own
// float-typed comparisons carry, and an absolute floor for an edge whose
// true length rounds to (near) zero, where a relative factor alone would
// demand exactness. Neither loosens the contract itself.
constexpr RtFloat kEdgeLengthRelativeSlack = 1.0001f;
constexpr RtFloat kEdgeLengthAbsoluteSlack = 1e-6f;

// locate() must reproduce a hit point within this fraction of the
// polygon's own extent -- an edge or vertex hit lands exactly on a cell
// boundary, so its round trip is exact up to float rounding, the same
// margin testPolygonLookup already uses for its own interior hits.
constexpr RtFloat kPolygonRoundTripTolerance = 1e-3f;

RtFloat pointDistance(GMANPoint const& a, GMANPoint const& b) {
  GMANVector const v(a, b);
  return (RtFloat)std::sqrt(v.dot(v));
}

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// Translate, rotate about an oblique axis (no coordinate axis special) and
// a uniform scale: realistic camera-space placement that still leaves
// every quadric's own exact shape (sphere stays a sphere, etc.), so the
// analytic parameterization and facet-sag reasoning below still applies.
GMANMatrix4 uniformPlacement(RtFloat scale) {
  GMANMatrix4 m;
  m.trans(2.0, -1.0, 8.0);
  GMANMatrix4 r;
  r.rot(GMANRadians(23.0), 0.3, 1.0, 0.2);
  m.concat(r);
  GMANMatrix4 s;
  s.scale(scale, scale, scale);
  m.concat(s);
  return m;
}

RtFloat sumElementAreas(GMANRadiosityMesh const& mesh) {
  RtFloat total = 0;
  for (std::size_t i = 0; i < mesh.getElementCount(); ++i) {
    total += mesh.getElement(i).area;
  }
  return total;
}

// Every element edge (perimeter, corners[k]-corners[k+1]) is at or under
// maxEdgeLength. A coincident corner pair (a pole) has ~0 length and so
// trivially holds regardless.
void checkEdgeLengths(GMANRadiosityMesh const& mesh, RtFloat maxEdgeLength, std::string const& what) {
  RtFloat const limit = maxEdgeLength * kEdgeLengthRelativeSlack + kEdgeLengthAbsoluteSlack;
  bool ok = true;
  RtFloat worst = 0;
  for (std::size_t e = 0; e < mesh.getElementCount(); ++e) {
    GMANRadiosityElement const& element = mesh.getElement(e);
    for (int k = 0; k < 4; ++k) {
      GMANPoint const& a = mesh.getNode(element.corners[k]).position;
      GMANPoint const& b = mesh.getNode(element.corners[(k + 1) % 4]).position;
      RtFloat const d = pointDistance(a, b);
      if (d > limit) {
        ok = false;
        worst = GMANMax(worst, d);
      }
    }
  }
  if (!ok) {
    std::printf("edge length violation: %s worst=%.6f limit=%.6f\n", what.c_str(), worst, limit);
  }
  check(ok, what + ": every element edge is at or under the maximum edge length");
}

// ---------------------------------------------------------------------
// Area
// ---------------------------------------------------------------------

void testSphereArea() {
  RtFloat const radius = 2.0f;
  RtFloat const maxEdgeLength = radius * 0.08f;

  {
    GMANLinearWorldManager worldManager;
    worldManager.add(new GMANRaySphere(radius, -radius, radius, 360.0, GMANParameterList()));
    GMANRadiosityMesh mesh;
    mesh.build(worldManager, maxEdgeLength);

    RtFloat const analytic = (RtFloat)(4.0 * PI * radius * radius);
    RtFloat const diced = sumElementAreas(mesh);
    RtFloat const relError = std::fabs(diced - analytic) / analytic;
    std::printf("area: sphere (full) maxEdge=%.4f elements=%zu analytic=%.6f diced=%.6f relError=%.6g\n", maxEdgeLength,
                mesh.getElementCount(), analytic, diced, relError);
    check(relError < kAreaRelTolerance, "sphere (full): diced area within 1% of 4*pi*r^2");
    checkEdgeLengths(mesh, maxEdgeLength, "sphere (full)");
  }

  {
    // zmin, zmax and thetamax all restricting.
    RtFloat const zmin = -1.0f, zmax = 1.5f, thetamax = 270.0f;
    GMANLinearWorldManager worldManager;
    worldManager.add(new GMANRaySphere(radius, zmin, zmax, thetamax, GMANParameterList()));
    GMANRadiosityMesh mesh;
    mesh.build(worldManager, maxEdgeLength);

    // A sphere's own lateral zone area is 2*pi*r*(zmax - zmin) regardless
    // of where the band sits (Archimedes' hat-box theorem), times the
    // wedge fraction.
    RtFloat const analytic = (RtFloat)(2.0 * PI * radius * (zmax - zmin) * (thetamax / 360.0));
    RtFloat const diced = sumElementAreas(mesh);
    RtFloat const relError = std::fabs(diced - analytic) / analytic;
    std::printf("area: sphere (partial) maxEdge=%.4f elements=%zu analytic=%.6f diced=%.6f relError=%.6g\n",
                maxEdgeLength, mesh.getElementCount(), analytic, diced, relError);
    check(relError < kAreaRelTolerance, "sphere (partial): diced area within 1% of its zone formula");
    checkEdgeLengths(mesh, maxEdgeLength, "sphere (partial)");
  }

  {
    // Transforms: Translate then uniform Scale 3 sums to 9x the unit
    // sphere's own area.
    GMANMatrix4 m;
    m.trans(5.0, -2.0, 11.0);
    GMANMatrix4 s;
    s.scale(3.0, 3.0, 3.0);
    m.concat(s);

    GMANLinearWorldManager worldManager;
    worldManager.add(new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), makeTransform(m)));
    GMANRadiosityMesh mesh;
    mesh.build(worldManager, 0.08f);

    RtFloat const unitArea = (RtFloat)(4.0 * PI);
    RtFloat const diced = sumElementAreas(mesh);
    RtFloat const relError = std::fabs(diced - 9.0f * unitArea) / (9.0f * unitArea);
    std::printf("area: sphere (Translate+Scale3) elements=%zu expected=%.6f diced=%.6f relError=%.6g\n",
                mesh.getElementCount(), 9.0f * unitArea, diced, relError);
    check(relError < kAreaRelTolerance, "sphere under Translate+Scale 3 sums to 9x its unit area");
  }

  {
    // A sphere under Scale 1 1 0 is skipped and counted, and build still
    // dices the other primitives beside it; combined with the
    // absent-primitives lookup check's own
    // "absent primitives" skipped-count requirement, using a second kind
    // of undiceable primitive (a bare GMANPrimitive) alongside it.
    GMANMatrix4 singular;
    singular.scale(1.0, 1.0, 0.0);

    GMANLinearWorldManager worldManager;
    worldManager.add(new GMANRaySphere(radius, -radius, radius, 360.0, GMANParameterList()));
    worldManager.add(new GMANRaySphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), makeTransform(singular)));
    worldManager.add(new GMANPrimitive());

    GMANRadiosityMesh mesh;
    mesh.build(worldManager, maxEdgeLength);

    check(mesh.getSkippedCount() == 2,
          "singular sphere + bare GMANPrimitive: skipped count is exactly the 2 undiceable primitives");
    RtFloat const analytic = (RtFloat)(4.0 * PI * radius * radius);
    RtFloat const diced = sumElementAreas(mesh);
    check(std::fabs(diced - analytic) / analytic < kAreaRelTolerance,
          "build still dices the good sphere beside the two undiceable primitives");

    // Absent primitives: a hit against a primitive never placed in this
    // world manager returns "none".
    GMANRaySphere elsewhere(1.0, -1.0, 1.0, 360.0, GMANParameterList());
    GMANHit strayHit;
    strayHit.primitive = &elsewhere;
    strayHit.u = 0.4f;
    strayHit.v = 0.4f;
    GMANRadiosityLocation location;
    check(!mesh.locate(strayHit, location), "a hit on a primitive the mesh does not hold returns none");
  }
}

void testCylinderArea() {
  RtFloat const radius = 1.5f, zmin = -2.0f, zmax = 3.0f;
  RtFloat const maxEdgeLength = radius * 0.08f;

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRayCylinder(radius, zmin, zmax, 360.0, GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);

  RtFloat const analytic = (RtFloat)(2.0 * PI * radius * (zmax - zmin));
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - analytic) / analytic;
  std::printf("area: cylinder maxEdge=%.4f elements=%zu analytic=%.6f diced=%.6f relError=%.6g\n", maxEdgeLength,
              mesh.getElementCount(), analytic, diced, relError);
  check(relError < kAreaRelTolerance, "cylinder: diced area within 1% of 2*pi*r*h");
  checkEdgeLengths(mesh, maxEdgeLength, "cylinder");
}

void testConeArea() {
  RtFloat const height = 3.0f, radius = 1.5f;
  RtFloat const maxEdgeLength = radius * 0.08f;

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRayCone(height, radius, 360.0, GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);

  RtFloat const slant = (RtFloat)std::sqrt((double)(radius * radius + height * height));
  RtFloat const analytic = (RtFloat)PI * radius * slant;
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - analytic) / analytic;
  std::printf("area: cone maxEdge=%.4f elements=%zu analytic=%.6f diced=%.6f relError=%.6g\n", maxEdgeLength,
              mesh.getElementCount(), analytic, diced, relError);
  check(relError < kAreaRelTolerance, "cone: diced area within 1% of pi*r*slant");
  checkEdgeLengths(mesh, maxEdgeLength, "cone");
}

void testDiskArea() {
  RtFloat const radius = 2.0f;
  RtFloat const maxEdgeLength = radius * 0.08f;

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRayDisk(1.0, radius, 360.0, GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);

  RtFloat const analytic = (RtFloat)PI * radius * radius;
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - analytic) / analytic;
  std::printf("area: disk maxEdge=%.4f elements=%zu analytic=%.6f diced=%.6f relError=%.6g\n", maxEdgeLength,
              mesh.getElementCount(), analytic, diced, relError);
  check(relError < kAreaRelTolerance, "disk: diced area within 1% of pi*r^2");
  checkEdgeLengths(mesh, maxEdgeLength, "disk");
}

void testTorusArea() {
  RtFloat const majorradius = 3.0f, minorradius = 1.0f;
  // The major circumference (2*pi*3 ~= 18.8) divided by a finer edge
  // length would approach kMaxDivisions; 0.15 keeps nu comfortably under
  // the cap while still well inside the 1% tolerance.
  RtFloat const maxEdgeLength = minorradius * 0.15f;

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRayTorus(majorradius, minorradius, -180.0, 180.0, 360.0, GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);

  RtFloat const analytic = (RtFloat)(4.0 * PI * PI * majorradius * minorradius);
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - analytic) / analytic;
  std::printf("area: torus maxEdge=%.4f elements=%zu analytic=%.6f diced=%.6f relError=%.6g\n", maxEdgeLength,
              mesh.getElementCount(), analytic, diced, relError);
  check(relError < kAreaRelTolerance, "torus: diced area within 1% of 4*pi^2*R*r");
  checkEdgeLengths(mesh, maxEdgeLength, "torus");
}

// Convergence, shared by every quadric (the two closed-form-free
// ones -- paraboloid, hyperboloid -- checked this way only): a reference
// area from a resolution at kMaxDivisions, then the absolute error at
// maxEdgeLength must fall by at least kMinConvergenceRatio when it halves.
template <class Factory>
void checkConvergence(char const* name, Factory makePrimitive, RtFloat maxEdgeLength, RtFloat referenceMaxEdgeLength) {
  GMANLinearWorldManager referenceWorld;
  referenceWorld.add(makePrimitive());
  GMANRadiosityMesh referenceMesh;
  referenceMesh.build(referenceWorld, referenceMaxEdgeLength);
  RtFloat const reference = sumElementAreas(referenceMesh);

  GMANLinearWorldManager coarseWorld;
  coarseWorld.add(makePrimitive());
  GMANRadiosityMesh coarseMesh;
  coarseMesh.build(coarseWorld, maxEdgeLength);
  RtFloat const coarseArea = sumElementAreas(coarseMesh);
  RtFloat const coarseError = std::fabs(coarseArea - reference);

  GMANLinearWorldManager fineWorld;
  fineWorld.add(makePrimitive());
  GMANRadiosityMesh fineMesh;
  fineMesh.build(fineWorld, maxEdgeLength * 0.5f);
  RtFloat const fineArea = sumElementAreas(fineMesh);
  RtFloat const fineError = std::fabs(fineArea - reference);

  RtFloat const ratio = fineError > 0 ? coarseError / fineError : (RtFloat)1e9;
  std::printf("convergence: %s reference=%.6f (elements=%zu) coarseErr=%.6g (elements=%zu) fineErr=%.6g (elements=%zu) "
              "ratio=%.3f\n",
              name, reference, referenceMesh.getElementCount(), coarseError, coarseMesh.getElementCount(), fineError,
              fineMesh.getElementCount(), ratio);
  check(ratio >= kMinConvergenceRatio,
        std::string(name) + ": halving the max edge length cuts the absolute area error by at least 3x");
}

void testConvergence() {
  checkConvergence(
      "sphere", []() { return new GMANRaySphere(2.0, -2.0, 2.0, 360.0, GMANParameterList()); }, 2.0f * 0.25f,
      2.0f / 300.0f);
  checkConvergence(
      "cylinder", []() { return new GMANRayCylinder(1.5, -2.0, 3.0, 360.0, GMANParameterList()); }, 1.5f * 0.25f,
      1.5f / 300.0f);
  checkConvergence(
      "cone", []() { return new GMANRayCone(3.0, 1.5, 360.0, GMANParameterList()); }, 1.5f * 0.25f, 1.5f / 300.0f);
  checkConvergence(
      "disk", []() { return new GMANRayDisk(1.0, 2.0, 360.0, GMANParameterList()); }, 2.0f * 0.25f, 2.0f / 300.0f);
  checkConvergence(
      "torus", []() { return new GMANRayTorus(3.0, 1.0, -180.0, 180.0, 360.0, GMANParameterList()); }, 1.0f * 0.25f,
      1.0f / 300.0f);
  checkConvergence(
      "paraboloid", []() { return new GMANRayParaboloid(2.0, 0.0, 3.0, 360.0, GMANParameterList()); }, 2.0f * 0.25f,
      2.0f / 300.0f);
  RtPoint point1 = {1.0, 0.0, -1.0};
  RtPoint point2 = {2.0, 0.0, 2.0};
  checkConvergence(
      "hyperboloid",
      [point1, point2]() mutable { return new GMANRayHyperboloid(point1, point2, 360.0, GMANParameterList()); },
      2.0f * 0.25f, 2.0f / 300.0f);
}

// A convex pentagon under a non-uniform scale and a rotation: its
// elements sum to its own area within 1e-5 relative, at a resolution
// coarse enough that most cells straddle an edge and get clipped.
void testPolygonArea() {
  std::vector<GMANPoint> local;
  double referenceArea = 0.0;
  {
    std::vector<std::pair<double, double>> localXY;
    for (int k = 0; k < 5; ++k) {
      double const angleDeg = 90.0 + 72.0 * (double)k;
      double const angle = angleDeg * PI / 180.0;
      localXY.push_back({2.0 * std::cos(angle), 2.0 * std::sin(angle)});
      local.push_back(GMANPoint((RtFloat)localXY.back().first, (RtFloat)localXY.back().second, 0.0));
    }
    for (std::size_t i = 0; i < localXY.size(); ++i) {
      auto const& a = localXY[i];
      auto const& b = localXY[(i + 1) % localXY.size()];
      referenceArea += a.first * b.second - b.first * a.second;
    }
    referenceArea = std::fabs(referenceArea) * 0.5;
  }

  // Scale first (against the pentagon's own local axes, so its area
  // scales by exactly sx*sy), then rotate and translate: GMANMatrix4's
  // own p*(A.concat(B)) applies A to a point before B, so the first call
  // here is the first one a local point meets.
  RtFloat const sx = 1.6f, sy = 0.7f, sz = 1.3f;
  GMANMatrix4 m;
  m.scale(sx, sy, sz);
  GMANMatrix4 r;
  r.rot(GMANRadians(35.0), 0.4, 1.0, 0.2);
  m.concat(r);
  GMANMatrix4 t;
  t.trans(2.0, -3.0, 10.0);
  m.concat(t);

  std::vector<GMANPoint> vertices;
  for (GMANPoint const& p : local) {
    vertices.push_back(gman::transformPoint(m, p));
  }

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRayPolygon(vertices, GMANParameterList()));
  GMANRadiosityMesh mesh;
  RtFloat const maxEdgeLength = 0.6f;
  mesh.build(worldManager, maxEdgeLength);

  RtFloat const expected = (RtFloat)(referenceArea * (double)sx * (double)sy);
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - expected) / expected;
  std::printf("area: polygon maxEdge=%.4f elements=%zu expected=%.6f diced=%.6f relError=%.6g\n", maxEdgeLength,
              mesh.getElementCount(), expected, diced, relError);
  check(relError < kPolygonAreaRelTolerance, "polygon: diced area within 1e-5 relative of its own scaled area");
  checkEdgeLengths(mesh, maxEdgeLength, "polygon");
}

// ---------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------

// Casts a camera-space ray at getLocation(u, v) through objectToCamera,
// along the outward normal reversed, and returns the resulting hit
// (primitive->intersect having already been asserted to succeed).
template <class RayPrimitive>
GMANHit castAt(RayPrimitive& primitive, GMANMatrix4 const& objectToCamera, GMANMatrix4 const& cameraToObject,
               RtFloat scale, double u, double v) {
  // A small fraction of the shape's own scale: far enough outside the
  // surface for a clean approach from outside, close enough that
  // curvature does not measurably shift the intersection's own (u, v)
  // from (u, v) itself.
  RtFloat const offset = (RtFloat)1e-4 * scale;

  GMANPoint const objPoint = primitive.getLocation(u, v);
  GMANVector objNormal = primitive.getNormal(u, v);
  GMANPoint const camPoint = gman::transformPoint(objectToCamera, objPoint);
  GMANVector camNormal = gman::transformNormal(cameraToObject, objNormal);
  camNormal.normalize();

  GMANPoint const origin(camPoint.getX() + camNormal.getX() * offset, camPoint.getY() + camNormal.getY() * offset,
                         camPoint.getZ() + camNormal.getZ() * offset);
  GMANVector const direction(-camNormal.getX(), -camNormal.getY(), -camNormal.getZ());
  GMANRay const ray(origin, direction);

  GMANHit hit;
  bool const hitOk = primitive.intersect(ray, hit);
  check(hitOk, "a ray cast at the surface along its own reversed normal hits");
  return hit;
}

// Parameterization agreement, round trip, weight validity and seams/edges
// for one already-built, already-diced quadric. primitive is owned by the
// caller's own world manager (already added there before mesh was built).
template <class RayPrimitive>
void checkQuadricLookup(char const* name, RayPrimitive& primitive, GMANRadiosityMesh const& mesh, RtFloat scale,
                        RtFloat sagBound) {
  GMANMatrix4 const objectToCamera = primitive.getObjectToCamera();
  GMANMatrix4 cameraToObject = objectToCamera;
  cameraToObject.invert();

  RtFloat maxUError = 0, maxVError = 0;
  bool allElementsFound = true;
  bool allSumOk = true;
  bool allWeightsOk = true;
  RtFloat maxPositionError = 0;

  struct Sample {
    double u, v;
    bool checkAgreement;
  };
  // The seam/edge samples sit a hair inside [0, 1] rather than exactly at
  // it: intersect()'s own wedge/band check rejects a hit whose
  // reconstructed theta or z lands a rounding error past its own
  // boundary, a pre-existing sensitivity of casting a ray at the exact
  // domain edge, not something this mesh introduces. 1e-6 is still deep
  // inside the grid's own first/last cell at any resolution this file
  // uses.
  constexpr double kEdgeEpsilon = 1e-6;
  Sample const samples[] = {
      {0.35, 0.62, true},         {0.2, 0.8, true},
      {0.65, 0.3, true},                                            // interior, away from seams/poles
      {kEdgeEpsilon, 0.5, false}, {1.0 - kEdgeEpsilon, 0.5, false}, // u seam/edge
      {0.5, kEdgeEpsilon, false}, {0.5, 1.0 - kEdgeEpsilon, false}, // v seam/edge
  };

  for (Sample const& sample : samples) {
    GMANHit const hit = castAt(primitive, objectToCamera, cameraToObject, scale, sample.u, sample.v);

    if (sample.checkAgreement) {
      maxUError = GMANMax(maxUError, (RtFloat)std::fabs(hit.u - (RtFloat)sample.u));
      maxVError = GMANMax(maxVError, (RtFloat)std::fabs(hit.v - (RtFloat)sample.v));
    }

    GMANRadiosityLocation location;
    bool const located = mesh.locate(hit, location);
    check(located, std::string(name) + ": locate() finds an element for a hit on its own primitive");
    if (!located) {
      allElementsFound = false;
      continue;
    }

    RtFloat const weightSum = location.weights[0] + location.weights[1] + location.weights[2] + location.weights[3];
    if (std::fabs(weightSum - 1.0f) >= kWeightSumTolerance) {
      allSumOk = false;
    }
    for (RtFloat w : location.weights) {
      if (w < -1e-6f || w > 1.0f + 1e-6f) {
        allWeightsOk = false;
      }
    }

    GMANPoint blended(0, 0, 0);
    for (int k = 0; k < 4; ++k) {
      GMANPoint const& corner = mesh.getNode(location.corners[k]).position;
      blended += corner * location.weights[k];
    }
    maxPositionError = GMANMax(maxPositionError, pointDistance(blended, hit.point));
  }

  std::printf("agreement: %s maxDu=%.6g maxDv=%.6g\n", name, maxUError, maxVError);
  check(maxUError < kParamAgreementTolerance && maxVError < kParamAgreementTolerance,
        std::string(name) + ": parameterization agreement within 1e-4");
  check(allElementsFound, std::string(name) + ": every sampled hit (interior, seams, edges) locates an element");
  check(allSumOk, std::string(name) + ": corner weights sum to 1 within tolerance");
  check(allWeightsOk, std::string(name) + ": corner weights each lie in [0, 1]");
  std::printf("round trip: %s maxPositionError=%.6g sagBound=%.6g\n", name, maxPositionError, sagBound);
  check(maxPositionError < sagBound, std::string(name) + ": round trip reproduces the hit point within facet sag");
}

// The sphere's own facet-sag bound, derived rather than guessed: an
// element edge of length e subtends angle e/R on a great circle of radius
// R, whose chord sags below the arc by R*(1 - cos((e/R)/2)) ~= e^2/(8R)
// for small e/R. A bilinear cell's own worst-case sag, along its
// diagonal, is at most the sum of both directions' own 1D sags, ~= e^2/
// (4R); doubling that again leaves comfortable room for the small-angle
// approximation itself.
RtFloat sphereSagBound(RtFloat maxEdgeLength, RtFloat radius) {
  return maxEdgeLength * maxEdgeLength / (2.0f * radius);
}

void testSphereLookup() {
  RtFloat const radius = 2.0f;
  RtFloat const maxEdgeLength = radius * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere(radius, -radius * 0.8f, radius * 0.8f, 300.0, GMANParameterList(),
                                   makeTransform(uniformPlacement(1.0f)));
  worldManager.add(sphere);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  checkQuadricLookup("sphere", *sphere, mesh, radius, sphereSagBound(maxEdgeLength, radius));
}

void testCylinderLookup() {
  RtFloat const radius = 1.5f;
  RtFloat const maxEdgeLength = radius * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* cylinder =
      new GMANRayCylinder(radius, -1.5, 2.0, 300.0, GMANParameterList(), makeTransform(uniformPlacement(1.0f)));
  worldManager.add(cylinder);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  checkQuadricLookup("cylinder", *cylinder, mesh, radius, sphereSagBound(maxEdgeLength, radius));
}

void testConeLookup() {
  RtFloat const radius = 1.5f;
  RtFloat const maxEdgeLength = radius * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* cone = new GMANRayCone(3.0, radius, 300.0, GMANParameterList(), makeTransform(uniformPlacement(1.0f)));
  worldManager.add(cone);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  checkQuadricLookup("cone", *cone, mesh, radius, sphereSagBound(maxEdgeLength, radius));
}

void testDiskLookup() {
  RtFloat const radius = 2.0f;
  RtFloat const maxEdgeLength = radius * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* disk = new GMANRayDisk(1.0, radius, 300.0, GMANParameterList(), makeTransform(uniformPlacement(1.0f)));
  worldManager.add(disk);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  // A disk's own surface is flat, but its (theta, radius) grid is still
  // curved within that plane: a constant-radius edge is an arc, so a
  // bilinear cell still sags off it by the same chord-vs-arc formula as
  // the sphere, using the disk's own outer radius.
  checkQuadricLookup("disk", *disk, mesh, radius, sphereSagBound(maxEdgeLength, radius));
}

void testTorusLookup() {
  RtFloat const majorradius = 3.0f, minorradius = 1.0f;
  RtFloat const maxEdgeLength = minorradius * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* torus = new GMANRayTorus(majorradius, minorradius, -150.0, 150.0, 300.0, GMANParameterList(),
                                 makeTransform(uniformPlacement(1.0f)));
  worldManager.add(torus);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  checkQuadricLookup("torus", *torus, mesh, minorradius, sphereSagBound(maxEdgeLength, minorradius));
}

void testParaboloidLookup() {
  RtFloat const rmax = 2.0f;
  RtFloat const maxEdgeLength = rmax * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* paraboloid =
      new GMANRayParaboloid(rmax, 0.3, 3.0, 300.0, GMANParameterList(), makeTransform(uniformPlacement(1.0f)));
  worldManager.add(paraboloid);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  checkQuadricLookup("paraboloid", *paraboloid, mesh, rmax, sphereSagBound(maxEdgeLength, rmax));
}

void testHyperboloidLookup() {
  RtPoint point1 = {1.0, 0.0, -1.0};
  RtPoint point2 = {2.0, 0.0, 2.0};
  RtFloat const scale = 2.0f;
  RtFloat const maxEdgeLength = scale * 0.06f;
  GMANLinearWorldManager worldManager;
  auto* hyperboloid =
      new GMANRayHyperboloid(point1, point2, 300.0, GMANParameterList(), makeTransform(uniformPlacement(1.0f)));
  worldManager.add(hyperboloid);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);
  checkQuadricLookup("hyperboloid", *hyperboloid, mesh, scale, sphereSagBound(maxEdgeLength, scale));
}

// The polygon included: locate()'s round trip is exact (up to float
// rounding), since the corner positions and the hit point are all affine
// functions of the same in-plane (s, t).
void testPolygonLookup() {
  std::vector<GMANPoint> const localVertices = {
      GMANPoint(0.0, 0.0, 0.0),
      GMANPoint(4.0, 0.0, 0.0),
      GMANPoint(5.0, 3.0, 0.0),
      GMANPoint(1.0, 4.0, 0.0),
  };
  GMANMatrix4 const placement = uniformPlacement(1.3f);
  std::vector<GMANPoint> vertices;
  for (GMANPoint const& p : localVertices) {
    vertices.push_back(gman::transformPoint(placement, p));
  }

  GMANLinearWorldManager worldManager;
  auto* polygon = new GMANRayPolygon(vertices, GMANParameterList());
  worldManager.add(polygon);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.5f);

  // A grid of interior points in the polygon's own vertex space,
  // barycentric-blended from its first three vertices (fan origin),
  // reprojected through the same placement -- an independent way to reach
  // points inside the quad without relying on the mesh's own basis.
  RtFloat maxPositionError = 0;
  bool allElementsFound = true;
  bool allSumOk = true;
  double const bary[][3] = {{0.5, 0.3, 0.2}, {0.25, 0.5, 0.25}, {0.4, 0.1, 0.5}};
  for (auto const& w : bary) {
    GMANPoint const local(vertices[0].getX() * 0 + (RtFloat)(w[0]) * localVertices[0].getX() +
                              (RtFloat)(w[1]) * localVertices[1].getX() + (RtFloat)(w[2]) * localVertices[2].getX(),
                          (RtFloat)(w[0]) * localVertices[0].getY() + (RtFloat)(w[1]) * localVertices[1].getY() +
                              (RtFloat)(w[2]) * localVertices[2].getY(),
                          0.0);
    GMANPoint const hitPoint = gman::transformPoint(placement, local);

    GMANHit hit;
    hit.primitive = polygon;
    hit.point = hitPoint;

    GMANRadiosityLocation location;
    bool const located = mesh.locate(hit, location);
    if (!located) {
      allElementsFound = false;
      continue;
    }
    RtFloat const weightSum = location.weights[0] + location.weights[1] + location.weights[2] + location.weights[3];
    if (std::fabs(weightSum - 1.0f) >= kWeightSumTolerance) {
      allSumOk = false;
    }
    GMANPoint blended(0, 0, 0);
    for (int k = 0; k < 4; ++k) {
      GMANPoint const& corner = mesh.getNode(location.corners[k]).position;
      blended += corner * location.weights[k];
    }
    maxPositionError = GMANMax(maxPositionError, pointDistance(blended, hitPoint));
  }

  check(allElementsFound, "polygon: every interior hit locates an element");
  check(allSumOk, "polygon: corner weights sum to 1 within tolerance");
  std::printf("round trip: polygon maxPositionError=%.6g\n", maxPositionError);
  check(maxPositionError < 1e-3f, "polygon: round trip reproduces the hit point exactly (flat, up to rounding)");
}

// Every vertex, then every edge midpoint, of the same quad: locate() must
// resolve each to an element, never "none", including where the owning
// cell is one clipping left partial -- a coarse maxEdgeLength here puts
// most boundary cells in that state.
void testPolygonEdgesAndVertices() {
  std::vector<GMANPoint> const localVertices = {
      GMANPoint(0.0, 0.0, 0.0),
      GMANPoint(4.0, 0.0, 0.0),
      GMANPoint(5.0, 3.0, 0.0),
      GMANPoint(1.0, 4.0, 0.0),
  };
  GMANMatrix4 const placement = uniformPlacement(1.3f);
  std::vector<GMANPoint> vertices;
  for (GMANPoint const& p : localVertices) {
    vertices.push_back(gman::transformPoint(placement, p));
  }

  GMANLinearWorldManager worldManager;
  auto* polygon = new GMANRayPolygon(vertices, GMANParameterList());
  worldManager.add(polygon);
  GMANRadiosityMesh mesh;
  RtFloat const maxEdgeLength = 0.6f;
  mesh.build(worldManager, maxEdgeLength);

  std::vector<GMANPoint> samples(vertices);
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    GMANPoint const& a = vertices[i];
    GMANPoint const& b = vertices[(i + 1) % vertices.size()];
    samples.push_back((a + b) * (RtFloat)0.5);
  }

  bool allFound = true;
  bool allSumOk = true;
  RtFloat maxPositionError = 0;
  for (GMANPoint const& point : samples) {
    GMANHit hit;
    hit.primitive = polygon;
    hit.point = point;

    GMANRadiosityLocation location;
    bool const located = mesh.locate(hit, location);
    if (!located) {
      allFound = false;
      continue;
    }

    RtFloat const weightSum = location.weights[0] + location.weights[1] + location.weights[2] + location.weights[3];
    if (std::fabs(weightSum - 1.0f) >= kWeightSumTolerance) {
      allSumOk = false;
    }
    GMANPoint blended(0, 0, 0);
    for (int k = 0; k < 4; ++k) {
      GMANPoint const& corner = mesh.getNode(location.corners[k]).position;
      blended += corner * location.weights[k];
    }
    maxPositionError = GMANMax(maxPositionError, pointDistance(blended, point));
  }

  check(allFound, "polygon: every vertex and edge-midpoint hit locates an element, never none");
  check(allSumOk, "polygon: edge/vertex corner weights sum to 1 within tolerance");
  std::printf("round trip: polygon edges/vertices maxPositionError=%.6g\n", maxPositionError);
  check(maxPositionError < kPolygonRoundTripTolerance,
        "polygon: edge/vertex round trip reproduces the hit point within tolerance");
}

// A full sphere's own pole (v = 0 and v = 1) and seam (u = 0 wrapping to
// u = 1) return an element, never "none".
void testSpherePoleSeam() {
  RtFloat const radius = 1.5f;
  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere(radius, -radius, radius, 360.0, GMANParameterList());
  worldManager.add(sphere);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, radius * 0.1f);

  GMANMatrix4 const identity;
  // A hair inside [0, 1]: see checkQuadricLookup's own comment on
  // intersect()'s exact-boundary sensitivity, sharper still at a pole
  // (atan2 of a point with x == y == 0 is not well-defined).
  constexpr double kEdgeEpsilon = 1e-6;
  double const uv[][2] = {
      {0.5, kEdgeEpsilon}, {0.5, 1.0 - kEdgeEpsilon}, {kEdgeEpsilon, 0.5}, {1.0 - kEdgeEpsilon, 0.5}};
  bool allFound = true;
  for (auto const& sample : uv) {
    GMANHit const hit = castAt(*sphere, identity, identity, radius, sample[0], sample[1]);
    GMANRadiosityLocation location;
    if (!mesh.locate(hit, location)) {
      allFound = false;
    }
  }
  check(allFound, "sphere: pole (v=0, v=1) and seam (u=0, u=1) hits each locate an element");
}

// A lookup addition: a normal check that a non-uniform scale can
// tell apart from a plain-matrix (rather than inverse-transpose) normal
// transform. Scale(2, 1, 1) turns the sphere into an ellipsoid whose two
// transforms diverge everywhere except its own principal axes.
void testNonUniformScaleNormal() {
  RtFloat const radius = 1.0f;
  GMANMatrix4 m;
  m.trans(1.0, 2.0, 9.0);
  GMANMatrix4 r;
  r.rot(GMANRadians(17.0), 0.2, 1.0, 0.4);
  m.concat(r);
  GMANMatrix4 s;
  s.scale(5.0, 1.0, 1.0);
  m.concat(s);

  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere(radius, -radius, radius, 360.0, GMANParameterList(), makeTransform(m));
  worldManager.add(sphere);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, radius * 0.05f);

  GMANMatrix4 const objectToCamera = sphere->getObjectToCamera();
  GMANMatrix4 cameraToObject = objectToCamera;
  cameraToObject.invert();

  double const u = 0.2, v = 0.35; // away from every principal axis
  GMANHit const hit = castAt(*sphere, objectToCamera, cameraToObject, radius, u, v);

  GMANRadiosityLocation location;
  bool const located = mesh.locate(hit, location);
  check(located, "non-uniform scale: locate() finds an element");
  if (!located) {
    return;
  }

  GMANVector blendedNormal(0, 0, 0);
  for (int k = 0; k < 4; ++k) {
    GMANVector const& n = mesh.getNode(location.corners[k]).normal;
    blendedNormal +=
        GMANVector(n.getX() * location.weights[k], n.getY() * location.weights[k], n.getZ() * location.weights[k]);
  }
  RtFloat const blendedLength = (RtFloat)std::sqrt(blendedNormal.dot(blendedNormal));
  if (blendedLength > 0) {
    blendedNormal = blendedNormal / blendedLength;
  }

  GMANVector objNormal = sphere->getNormal(u, v);
  GMANVector expectedNormal = gman::transformNormal(cameraToObject, objNormal);
  expectedNormal.normalize();

  RtFloat const dot = blendedNormal.dot(expectedNormal);
  std::printf("non-uniform scale normal: dot(blended, expected)=%.6g\n", dot);
  check(dot > 0.9999f, "non-uniform scale: node normals match the inverse-transpose transform, not the plain matrix");
}

} // namespace

int main() {
  testSphereArea();
  testCylinderArea();
  testConeArea();
  testDiskArea();
  testTorusArea();
  testConvergence();
  testPolygonArea();

  testSphereLookup();
  testCylinderLookup();
  testConeLookup();
  testDiskLookup();
  testTorusLookup();
  testParaboloidLookup();
  testHyperboloidLookup();
  testPolygonLookup();
  testPolygonEdgesAndVertices();
  testSpherePoleSeam();
  testNonUniformScaleNormal();

  return checkSummary("GMANRadiosityMesh dices every supported primitive within tolerance and converges at h^2");
}
