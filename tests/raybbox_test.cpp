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
 * R6 proof, §8 check B: every ray primitive's camera-space bbox.
 *
 * Check 1 (the seven surfaces of revolution): a representative placement
 * -- translate, rotate and a non-uniform scale together, tilting the
 * shape so no sampled extent lands exactly on a coordinate axis --
 * samples getLocation across a fine (u, v) grid, transforms each sample
 * through the same matrix the primitive was built with, and asserts
 * getBBox() contains every sample (within a relative tolerance for float
 * rounding) and is not more than 2x looser than the sampled extent on any
 * axis. A wedge narrower than 360 degrees, or a z-clipped band, loosens a
 * conservative full-revolution bound without bounding how much; every
 * placement here instead uses thetamax at or near 360 and each
 * primitive's own full z range, where the bound is tight and the 2x
 * check is meaningful. Check 3 (singular transform) and check 4
 * (degenerate, non-singular parameter) follow, one per primitive.
 */

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"

namespace {

constexpr RtFloat kRelTolerance = 1e-5f;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// translate, rotate (about two axes, so the tilt lands off every
// coordinate axis) and a non-uniform scale, composed together.
GMANMatrix4 representativePlacement() {
  GMANMatrix4 m;
  m.trans(3.0, -2.0, 7.0);
  GMANMatrix4 rx;
  rx.rot(GMANRadians(25.0), 1.0, 0.0, 0.0);
  m.concat(rx);
  GMANMatrix4 ry;
  ry.rot(GMANRadians(35.0), 0.0, 1.0, 0.0);
  m.concat(ry);
  GMANMatrix4 s;
  s.scale(1.3, 0.7, 2.1);
  m.concat(s);
  return m;
}

// translate and a non-uniform scale, no rotation: an axis-aligned
// object-space box transforms to an exact axis-aligned camera-space box
// under this placement, with no rotation slack for an undersized
// object-space bound to hide behind. representativePlacement's own
// rotation can let a too-small box's corners still happen to cover the
// true rotated extent; this placement cannot.
GMANMatrix4 unrotatedPlacement() {
  GMANMatrix4 m;
  m.trans(3.0, -2.0, 7.0);
  GMANMatrix4 s;
  s.scale(1.3, 0.7, 2.1);
  m.concat(s);
  return m;
}

// A per-axis tolerance scaled to the coordinate's own magnitude, matching
// the pad's own scale (kBBoxPadScale in gmanraybbox.h) rather than
// a fixed absolute one.
RtFloat tolerance(RtFloat coord) { return kRelTolerance * GMANMax((RtFloat)1.0, (RtFloat)std::fabs(coord)); }

// Samples a primitive's own object-space getLocation across an (n+1)x(n+1)
// (u, v) grid, transforms every sample through matrix, and asserts
// getBBox() contains each one (check 1's containment half) while
// accumulating the samples' own componentwise min/max for the tightness
// half below.
template <class Primitive>
void checkContainmentAndTightness(char const* name, Primitive& primitive, GMANMatrix4 const& matrix) {
  GMANBBox const box = primitive.getBBox();
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();

  constexpr int kSteps = 40;
  GMANPoint sampledMin, sampledMax;
  bool first = true;

  for (int iu = 0; iu <= kSteps; ++iu) {
    double const u = (double)iu / (double)kSteps;
    for (int iv = 0; iv <= kSteps; ++iv) {
      double const v = (double)iv / (double)kSteps;
      GMANPoint const objPoint = primitive.getLocation(u, v);
      GMANPoint const p = gman::transformPoint(matrix, objPoint);

      check(p.getX() >= boxMin.getX() - tolerance(p.getX()) && p.getX() <= boxMax.getX() + tolerance(p.getX()) &&
                p.getY() >= boxMin.getY() - tolerance(p.getY()) && p.getY() <= boxMax.getY() + tolerance(p.getY()) &&
                p.getZ() >= boxMin.getZ() - tolerance(p.getZ()) && p.getZ() <= boxMax.getZ() + tolerance(p.getZ()),
            std::string(name) + ": getBBox() contains a sampled surface point");

      if (first) {
        sampledMin = p;
        sampledMax = p;
        first = false;
      } else {
        sampledMin = GMANPoint(GMANMin(sampledMin.getX(), p.getX()), GMANMin(sampledMin.getY(), p.getY()),
                               GMANMin(sampledMin.getZ(), p.getZ()));
        sampledMax = GMANPoint(GMANMax(sampledMax.getX(), p.getX()), GMANMax(sampledMax.getY(), p.getY()),
                               GMANMax(sampledMax.getZ(), p.getZ()));
      }
    }
  }

  RtFloat const boxExtentX = boxMax.getX() - boxMin.getX();
  RtFloat const boxExtentY = boxMax.getY() - boxMin.getY();
  RtFloat const boxExtentZ = boxMax.getZ() - boxMin.getZ();
  RtFloat const sampledExtentX = sampledMax.getX() - sampledMin.getX();
  RtFloat const sampledExtentY = sampledMax.getY() - sampledMin.getY();
  RtFloat const sampledExtentZ = sampledMax.getZ() - sampledMin.getZ();

  // A genuinely flat axis (the unrotated disk's own z, sampledExtent ~ 0)
  // makes the 2x ratio meaningless: any pad at all would fail it. Below
  // this floor, only the pad itself bounds boxExtent, already covered by
  // the containment check above.
  constexpr RtFloat kFlatFloor = 1e-3f;
  if (sampledExtentX > kFlatFloor) {
    check(boxExtentX <= 2.0f * sampledExtentX, std::string(name) + ": box x extent is at most 2x the sampled extent");
  }
  if (sampledExtentY > kFlatFloor) {
    check(boxExtentY <= 2.0f * sampledExtentY, std::string(name) + ": box y extent is at most 2x the sampled extent");
  }
  if (sampledExtentZ > kFlatFloor) {
    check(boxExtentZ <= 2.0f * sampledExtentZ, std::string(name) + ": box z extent is at most 2x the sampled extent");
  }
}

void checkFiniteWellOrdered(char const* name, GMANBBox const& box) {
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();
  check(boxMin.getX() <= boxMax.getX() && boxMin.getY() <= boxMax.getY() && boxMin.getZ() <= boxMax.getZ(),
        std::string(name) + ": degenerate parameter still yields a well-ordered box");
  check(std::fabs(boxMin.getX()) < RI_INFINITY && std::fabs(boxMax.getX()) < RI_INFINITY &&
            std::fabs(boxMin.getY()) < RI_INFINITY && std::fabs(boxMax.getY()) < RI_INFINITY &&
            std::fabs(boxMin.getZ()) < RI_INFINITY && std::fabs(boxMax.getZ()) < RI_INFINITY,
        std::string(name) + ": degenerate parameter still yields a finite box");
}

void checkDefaultBox(char const* name, GMANBBox const& box) {
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();
  check(boxMin.getX() == -RI_INFINITY && boxMin.getY() == -RI_INFINITY && boxMin.getZ() == -RI_INFINITY &&
            boxMax.getX() == RI_INFINITY && boxMax.getY() == RI_INFINITY && boxMax.getZ() == RI_INFINITY,
        std::string(name) + ": a singular transform leaves the default, RI_INFINITY-bounded box");
}

// ---- sphere ----
void testSphere() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRaySphere sphere(2.0, -2.0, 2.0, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("sphere", sphere, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRaySphere sphereUnrotated(2.0, -2.0, 2.0, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("sphere unrotated", sphereUnrotated, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRaySphere singularSphere(2.0, -2.0, 2.0, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("sphere singular", singularSphere.getBBox());

  GMANRaySphere zeroRadius(0.0, -1.0, 1.0, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("sphere degenerate (zero radius)", zeroRadius.getBBox());
}

// padBBox's own contribution on the revolution-primitive path
// (cameraSpaceBBox -> padBBox) is untested by containment/tightness
// alone -- both already carry a tolerance well above the pad's own
// few-ULP scale, so a mutant returning GMANBBox(camMin, camMax)
// unpadded still passes them. Assert a literal expected max instead,
// not one read back from padBBox's own constants: an unrotated sphere's
// exact camera-space corner (computed by hand from unrotatedPlacement's
// translate-then-scale, not sampled), offset by the pad those constants
// compute at that corner's own magnitude.
void testRevolutionPad() {
  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRaySphere sphere(2.0, -2.0, 2.0, 360.0, GMANParameterList(), makeTransform(unrotated));
  GMANPoint const boxMax = sphere.getBBox().getMax();

  // unrotatedPlacement composes trans(3, -2, 7) then concat(scale(1.3,
  // 0.7, 2.1)), so a point's translation is itself scaled: object corner
  // (2, 2, 2) maps to (2*1.3 + 3*1.3, 2*0.7 + -2*0.7, 2*2.1 + 7*2.1) ==
  // (6.5, 0.0, 18.9).
  constexpr RtFloat kUnpaddedMaxX = 6.5f;
  constexpr RtFloat kUnpaddedMaxY = 0.0f;
  constexpr RtFloat kUnpaddedMaxZ = 18.9f;
  // The largest coordinate magnitude among this box's own six corners is
  // 18.9 (the z max); 3e-5 * 18.9 == 5.67e-4, above the 1e-9 floor.
  constexpr RtFloat kExpectedPad = 5.67e-4f;
  constexpr RtFloat kEpsilon = 1e-4f;

  check(std::fabs(boxMax.getX() - (kUnpaddedMaxX + kExpectedPad)) < kEpsilon &&
            std::fabs(boxMax.getY() - (kUnpaddedMaxY + kExpectedPad)) < kEpsilon &&
            std::fabs(boxMax.getZ() - (kUnpaddedMaxZ + kExpectedPad)) < kEpsilon,
        "sphere unrotated: getBBox().getMax() == the exact corner, offset by the literal expected pad");
}

// ---- cone ----
void testCone() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRayCone cone(3.0, 1.5, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("cone", cone, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRayCone coneUnrotated(3.0, 1.5, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("cone unrotated", coneUnrotated, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRayCone singularCone(3.0, 1.5, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("cone singular", singularCone.getBBox());

  GMANRayCone zeroRadius(3.0, 0.0, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("cone degenerate (zero radius)", zeroRadius.getBBox());
}

// ---- cylinder ----
void testCylinder() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRayCylinder cylinder(1.2, -1.0, 2.5, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("cylinder", cylinder, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRayCylinder cylinderUnrotated(1.2, -1.0, 2.5, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("cylinder unrotated", cylinderUnrotated, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRayCylinder singularCylinder(1.2, -1.0, 2.5, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("cylinder singular", singularCylinder.getBBox());

  GMANRayCylinder zeroRadius(0.0, -1.0, 2.5, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("cylinder degenerate (zero radius)", zeroRadius.getBBox());
}

// ---- disk ----
void testDisk() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRayDisk disk(1.0, 1.0, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("disk", disk, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRayDisk diskUnrotated(1.0, 1.0, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("disk unrotated", diskUnrotated, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRayDisk singularDisk(1.0, 1.0, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("disk singular", singularDisk.getBBox());

  GMANRayDisk zeroRadius(1.0, 0.0, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("disk degenerate (zero radius)", zeroRadius.getBBox());
}

// ---- hyperboloid ----
void testHyperboloid() {
  GMANMatrix4 const placement = representativePlacement();
  RtPoint point1 = {0.5, 0.0, -1.0};
  RtPoint point2 = {1.2, 0.0, 2.0};
  GMANRayHyperboloid hyperboloid(point1, point2, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("hyperboloid", hyperboloid, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRayHyperboloid hyperboloidUnrotated(point1, point2, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("hyperboloid unrotated", hyperboloidUnrotated, unrotated);

  // |point1| > |point2|: a bound taken from point2's own radius alone
  // (ignoring point1's, the larger one) would undersize the box. Reusing
  // only point2's radius here would happen to equal the correct max in
  // the checks above (point2's radius already exceeds point1's there),
  // so a point2-only bug needs its own, reversed case to fail.
  RtPoint point1Larger = {1.8, 0.0, -1.0};
  RtPoint point2Smaller = {0.4, 0.0, 2.0};
  GMANRayHyperboloid reversedRadii(point1Larger, point2Smaller, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("hyperboloid |point1| > |point2|", reversedRadii, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRayHyperboloid singularHyperboloid(point1, point2, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("hyperboloid singular", singularHyperboloid.getBBox());

  RtPoint originA = {0.0, 0.0, 0.0};
  RtPoint originB = {0.0, 0.0, 0.0};
  GMANRayHyperboloid degenerate(originA, originB, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("hyperboloid degenerate (point1 == point2)", degenerate.getBBox());
}

// ---- paraboloid: zmax < 1 so the correct bound (|rmax| / sqrt(zmax))
// and the naive one (rmax) disagree ----
void testParaboloid() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRayParaboloid paraboloid(1.0, 0.0, 0.5, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("paraboloid", paraboloid, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRayParaboloid paraboloidUnrotated(1.0, 0.0, 0.5, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("paraboloid unrotated", paraboloidUnrotated, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRayParaboloid singularParaboloid(1.0, 0.0, 0.5, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("paraboloid singular", singularParaboloid.getBBox());

  GMANRayParaboloid zeroRmax(0.0, 0.0, 0.5, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("paraboloid degenerate (zero rmax)", zeroRmax.getBBox());
}

// ---- torus: majorradius/minorradius are the shape's own named extrema ----
void testTorus() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRayTorus torus(2.0, 0.5, -180.0, 180.0, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("torus", torus, placement);

  GMANMatrix4 const unrotated = unrotatedPlacement();
  GMANRayTorus torusUnrotated(2.0, 0.5, -180.0, 180.0, 360.0, GMANParameterList(), makeTransform(unrotated));
  checkContainmentAndTightness("torus unrotated", torusUnrotated, unrotated);

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRayTorus singularTorus(2.0, 0.5, -180.0, 180.0, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("torus singular", singularTorus.getBBox());

  GMANRayTorus zeroMinor(2.0, 0.0, -180.0, 180.0, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("torus degenerate (zero minorradius)", zeroMinor.getBBox());
}

// ---- polygon: componentwise min/max of vertices, already camera space
// (no transform step, unlike every other ray primitive here) -- within
// the shared pad every assigned box carries, so not an exact-equality
// check ----
void testPolygon() {
  // No single vertex is the box's min or max corner -- each of the six
  // componentwise extrema comes from a different vertex -- so a bbox
  // computed from one wrong vertex instead of all four still fails.
  std::vector<GMANPoint> const verts = {
      GMANPoint(0.0, 5.0, 1.0),
      GMANPoint(5.0, 0.0, 3.0),
      GMANPoint(2.0, 2.0, 0.0),
      GMANPoint(3.0, 3.0, 6.0),
  };
  GMANRayPolygon polygon(verts, GMANParameterList());
  GMANBBox const box = polygon.getBBox();
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();

  GMANPoint const vertMin(0.0, 0.0, 0.0);
  GMANPoint const vertMax(5.0, 5.0, 6.0);
  // A literal expected pad, not one read back from the helper's own
  // constants: at magnitude 6 (the largest coordinate magnitude among
  // vertMin/vertMax), 3e-5 * 6 == 1.8e-4, well above the 1e-9 floor.
  // Computing this from gman::kBBoxPadScale/kBBoxPadFloor instead would
  // pass a pad-set-to-0 mutation vacuously, since the test's own expected
  // value would fall to 0 right along with the code's.
  constexpr RtFloat kExpectedPad = 1.8e-4f;
  constexpr RtFloat kEpsilon = 1e-6f;

  check(std::fabs(boxMin.getX() - (vertMin.getX() - kExpectedPad)) < kEpsilon &&
            std::fabs(boxMin.getY() - (vertMin.getY() - kExpectedPad)) < kEpsilon &&
            std::fabs(boxMin.getZ() - (vertMin.getZ() - kExpectedPad)) < kEpsilon,
        "polygon: getBBox().getMin() == the vertices' own min, offset by the literal expected pad");
  check(std::fabs(boxMax.getX() - (vertMax.getX() + kExpectedPad)) < kEpsilon &&
            std::fabs(boxMax.getY() - (vertMax.getY() + kExpectedPad)) < kEpsilon &&
            std::fabs(boxMax.getZ() - (vertMax.getZ() + kExpectedPad)) < kEpsilon,
        "polygon: getBBox().getMax() == the vertices' own max, offset by the literal expected pad");
}

} // namespace

int main() {
  testSphere();
  testRevolutionPad();
  testCone();
  testCylinder();
  testDisk();
  testHyperboloid();
  testParaboloid();
  testTorus();
  testPolygon();

  return checkSummary("R6: every ray primitive's camera-space bbox contains, tightly bounds and degrades correctly");
}
