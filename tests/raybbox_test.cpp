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
 * axis. Every placement here uses thetamax at or near 360 and each
 * primitive's own full z range, so the 2x check applies without the
 * wedge/clipped-band exemption the prompt carves out for a narrower one.
 * Check 3 (singular transform) and check 4 (degenerate, non-singular
 * parameter) follow, one per primitive.
 */

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanray.h"
#include "gmanraybboxbuilder.h"
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

// A per-axis tolerance scaled to the coordinate's own magnitude, matching
// the pad's own scale (kBBoxPadScale in gmanraybboxbuilder.h) rather than
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

  check(boxExtentX <= 2.0f * sampledExtentX, std::string(name) + ": box x extent is at most 2x the sampled extent");
  check(boxExtentY <= 2.0f * sampledExtentY, std::string(name) + ": box y extent is at most 2x the sampled extent");
  check(boxExtentZ <= 2.0f * sampledExtentZ, std::string(name) + ": box z extent is at most 2x the sampled extent");
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

  GMANMatrix4 singular;
  singular.scale(1.0, 1.0, 0.0);
  GMANRaySphere singularSphere(2.0, -2.0, 2.0, 360.0, GMANParameterList(), makeTransform(singular));
  checkDefaultBox("sphere singular", singularSphere.getBBox());

  GMANRaySphere zeroRadius(0.0, -1.0, 1.0, 360.0, GMANParameterList(), makeTransform(GMANMatrix4()));
  checkFiniteWellOrdered("sphere degenerate (zero radius)", zeroRadius.getBBox());
}

// ---- cone ----
void testCone() {
  GMANMatrix4 const placement = representativePlacement();
  GMANRayCone cone(3.0, 1.5, 360.0, GMANParameterList(), makeTransform(placement));
  checkContainmentAndTightness("cone", cone, placement);

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
  std::vector<GMANPoint> const verts = {
      GMANPoint(1.0, 0.0, 0.0),
      GMANPoint(5.0, 0.0, 2.0),
      GMANPoint(5.0, 3.0, 2.0),
      GMANPoint(1.0, 3.0, 0.0),
  };
  GMANRayPolygon polygon(verts, GMANParameterList());
  GMANBBox const box = polygon.getBBox();
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();

  GMANPoint const vertMin(1.0, 0.0, 0.0);
  GMANPoint const vertMax(5.0, 3.0, 2.0);
  RtFloat const magnitude = 5.0f; // the largest coordinate magnitude among vertMin/vertMax
  RtFloat const pad = GMANMax(gman::kBBoxPadScale * magnitude, gman::kBBoxPadFloor);
  constexpr RtFloat kEpsilon = 1e-5f;

  check(std::fabs(boxMin.getX() - (vertMin.getX() - pad)) < kEpsilon &&
            std::fabs(boxMin.getY() - (vertMin.getY() - pad)) < kEpsilon &&
            std::fabs(boxMin.getZ() - (vertMin.getZ() - pad)) < kEpsilon,
        "polygon: getBBox().getMin() == the vertices' own min, within the shared pad");
  check(std::fabs(boxMax.getX() - (vertMax.getX() + pad)) < kEpsilon &&
            std::fabs(boxMax.getY() - (vertMax.getY() + pad)) < kEpsilon &&
            std::fabs(boxMax.getZ() - (vertMax.getZ() + pad)) < kEpsilon,
        "polygon: getBBox().getMax() == the vertices' own max, within the shared pad");
}

} // namespace

int main() {
  testSphere();
  testCone();
  testCylinder();
  testDisk();
  testHyperboloid();
  testParaboloid();
  testTorus();
  testPolygon();

  return checkSummary("R6: every ray primitive's camera-space bbox contains, tightly bounds and degrades correctly");
}
