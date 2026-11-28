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
 * GMANRayBVH::nearestHit and GMANRayOccluder::transmission are const and
 * touch no shared mutable state, so a call nested inside another, on the
 * same tree, must answer exactly as a standalone call would. Each check
 * below builds a two-leaf tree (eight primitives, past the leaf size) with
 * one probe primitive whose own intersect() issues a second, nested call
 * on a different, fixed target before answering its own intersection
 * honestly. The near leaf's primitives all miss the outer ray (offset off
 * its line, though their combined box still spans it, so the leaf is
 * still visited); the far leaf holds the outer ray's only real hit. A
 * traversal that shares mutable state between the outer and nested call
 * loses track of the outer's own pending far leaf once the nested call
 * returns, and so misses it.
 */

#include <utility>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanowned.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanworld.h"
#include "maketransform.h"
#include "ri.h"

namespace {

GMANTransform translated(RtFloat dx, RtFloat dy, RtFloat dz) {
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  return makeTransform(m);
}

gman::Owned<GMANRaySphere> sphereAt(RtFloat x, RtFloat y, RtFloat z) {
  return gman::make<GMANRaySphere>(0.5, -0.5, 0.5, 360.0, GMANParameterList(), translated(x, y, z));
}

// ---- check A: GMANRayBVH::nearestHit ----
//
// A probe primitive that, when armed, casts a second nearestHit against
// the same bvh for a fixed, unrelated ray before delegating to a real
// sphere for its own answer. Owns delegate: every other primitive in these
// fixtures is built the same way, held by a GMANWorld that
// destroys what it holds.
class NestingProbe : public GMANRayPrimitive {
public:
  NestingProbe(gman::Owned<GMANRaySphere> delegatePrimitive, GMANRayBVH const& bvhToNest, GMANRay const& fixedInnerRay)
      : delegate(std::move(delegatePrimitive)), bvh(bvhToNest), innerRay(fixedInnerRay) {
    bbox = delegate->getBBox();
  }

  bool intersect(GMANRay const& ray, GMANHit& hit) const override {
    ++callCount;
    if (nestingEnabled) {
      nestedFound = bvh.nearestHit(innerRay, nestedHit, nestedPrimitive);
    }
    return delegate->intersect(ray, hit);
  }

  bool nestingEnabled = false;
  mutable int callCount = 0;
  mutable bool nestedFound = false;
  mutable GMANHit nestedHit;
  mutable GMANRayPrimitive const* nestedPrimitive = nullptr;

private:
  gman::Owned<GMANRaySphere> delegate;
  GMANRayBVH const& bvh;
  GMANRay innerRay;
};

bool nearestHitsAgree(bool foundA, GMANRayPrimitive const* primA, RtFloat tA, bool foundB,
                      GMANRayPrimitive const* primB, RtFloat tB) {
  if (foundA != foundB) {
    return false;
  }
  return !foundA || (primA == primB && tA == tB);
}

void testNearestHitReentrant() {
  GMANWorld world;

  // The inner ray's own target: off the outer ray's line (y == 0.6), so
  // the outer ray cannot hit it directly; the far leaf's own union box
  // still spans it either way.
  GMANRay const innerRay(GMANPoint(11.0, 0.6, -50.0), GMANVector(0.0, 0.0, 1.0));

  GMANRayBVH bvh;

  // Near leaf: four spheres straddling the outer ray's line (y == 0) in
  // their union box (two at y == +0.6, two at y == -0.6) but each
  // individually clear of it, so every one of them, the probe included,
  // genuinely misses the outer ray.
  auto realProbeOwner = gman::make<NestingProbe>(sphereAt(2.0, 0.6, 0.0), bvh, innerRay);
  auto* const realProbe = realProbeOwner.get();
  world.add(sphereAt(0.0, 0.6, 0.0));
  world.add(sphereAt(1.0, -0.6, 0.0));
  world.add(std::move(realProbeOwner));
  world.add(sphereAt(3.0, -0.6, 0.0));

  // Far leaf: the outer ray's only real hit (trueTarget, on its exact
  // line), the inner ray's own real target (innerTarget, off the outer
  // ray's line but on the inner ray's), and two more off-line fillers.
  auto trueTargetOwner = sphereAt(10.0, 0.0, 0.0);
  auto* const trueTarget = trueTargetOwner.get();
  auto innerTargetOwner = sphereAt(11.0, 0.6, 0.0);
  auto* const innerTarget = innerTargetOwner.get();
  world.add(std::move(trueTargetOwner));
  world.add(std::move(innerTargetOwner));
  world.add(sphereAt(12.0, -0.6, 0.0));
  world.add(sphereAt(13.0, 0.6, 0.0));

  bvh.build(world);

  GMANRay const outerRay(GMANPoint(-100.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));

  // Standalone baselines, nesting off, before the nested traversal runs.
  GMANHit innerBaselineHit;
  GMANRayPrimitive const* innerBaselinePrim = nullptr;
  bool const innerBaselineFound = bvh.nearestHit(innerRay, innerBaselineHit, innerBaselinePrim);
  check(innerBaselineFound && innerBaselinePrim == innerTarget,
        "check A: the inner ray's own standalone call hits its real target");

  GMANHit outerBaselineHit;
  GMANRayPrimitive const* outerBaselinePrim = nullptr;
  bool const outerBaselineFound = bvh.nearestHit(outerRay, outerBaselineHit, outerBaselinePrim);
  check(outerBaselineFound && outerBaselinePrim == trueTarget,
        "check A: the outer ray's own standalone call hits the far leaf's real target");

  // The real run: the outer traversal, with the probe's nesting armed.
  realProbe->nestingEnabled = true;
  GMANHit outerHit;
  GMANRayPrimitive const* outerPrim = nullptr;
  bool const outerFound = bvh.nearestHit(outerRay, outerHit, outerPrim);

  check(realProbe->callCount >= 1, "check A: the probe was actually entered by the outer traversal");
  check(nearestHitsAgree(realProbe->nestedFound, realProbe->nestedPrimitive, realProbe->nestedHit.t, innerBaselineFound,
                         innerBaselinePrim, innerBaselineHit.t),
        "check A: the nested call, made mid-traversal, answers exactly as the inner ray's own standalone call did");
  check(nearestHitsAgree(outerFound, outerPrim, outerHit.t, outerBaselineFound, outerBaselinePrim, outerBaselineHit.t),
        "check A: the outer traversal's own result is unchanged by the nested call inside it");
}

// ---- check B: GMANRayOccluder::transmission ----
//
// The same fixture shape, with an occluder probe that casts a second
// transmission() against the same occluder for a fixed, unrelated shadow
// instead of a second nearestHit.
class OccluderNestingProbe : public GMANRayPrimitive {
public:
  OccluderNestingProbe(gman::Owned<GMANRaySphere> delegatePrimitive, GMANRayOccluder const& occluderToNest,
                       GMANLight const& fixedInnerLight, GMANPoint const& fixedInnerP,
                       GMANVector const& fixedInnerTowardLight, RtFloat fixedInnerDistance)
      : delegate(std::move(delegatePrimitive)), occluder(occluderToNest), innerLight(fixedInnerLight),
        innerP(fixedInnerP), innerTowardLight(fixedInnerTowardLight), innerDistance(fixedInnerDistance) {
    bbox = delegate->getBBox();
  }

  bool intersect(GMANRay const& ray, GMANHit& hit) const override {
    ++callCount;
    if (nestingEnabled) {
      nestedResult = occluder.transmission(innerLight, innerP, innerTowardLight, GMANVector(), innerDistance, 0.0f);
      nestedCalled = true;
    }
    return delegate->intersect(ray, hit);
  }

  bool nestingEnabled = false;
  mutable int callCount = 0;
  mutable bool nestedCalled = false;
  mutable GMANColor nestedResult;

private:
  gman::Owned<GMANRaySphere> delegate;
  GMANRayOccluder const& occluder;
  GMANLight const& innerLight;
  GMANPoint innerP;
  GMANVector innerTowardLight;
  RtFloat innerDistance;
};

bool colorsAgree(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

void setOpaque(GMANRayPrimitive* primitive) {
  gman::Appearance opaque;
  opaque.Os = GMANColor(1.0f, 1.0f, 1.0f);
  primitive->setAppearance(opaque);
}

void testOccluderReentrant() {
  GMANWorld world;

  GMANLight const innerLight(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0, 0.0, -1.0));
  GMANPoint const innerP(11.0, 0.6, -50.0);
  GMANVector const innerTowardLight(0.0, 0.0, 1.0);
  RtFloat const innerDistance = 100.0;

  GMANRayBVH bvh;
  GMANRayOccluder occluder(bvh);

  auto probeDelegate = sphereAt(2.0, 0.6, 0.0);
  setOpaque(probeDelegate.get());
  auto probeOwner = gman::make<OccluderNestingProbe>(std::move(probeDelegate), occluder, innerLight, innerP,
                                                     innerTowardLight, innerDistance);
  auto* const probe = probeOwner.get();

  auto nearA = sphereAt(0.0, 0.6, 0.0);
  auto nearB = sphereAt(1.0, -0.6, 0.0);
  auto nearC = sphereAt(3.0, -0.6, 0.0);
  setOpaque(nearA.get());
  setOpaque(nearB.get());
  setOpaque(nearC.get());
  world.add(std::move(nearA));
  world.add(std::move(nearB));
  world.add(std::move(probeOwner));
  world.add(std::move(nearC));

  auto trueBlocker = sphereAt(10.0, 0.0, 0.0);
  auto innerBlocker = sphereAt(11.0, 0.6, 0.0);
  auto farB = sphereAt(12.0, -0.6, 0.0);
  auto farC = sphereAt(13.0, 0.6, 0.0);
  setOpaque(trueBlocker.get());
  setOpaque(innerBlocker.get());
  setOpaque(farB.get());
  setOpaque(farC.get());
  world.add(std::move(trueBlocker));
  world.add(std::move(innerBlocker));
  world.add(std::move(farB));
  world.add(std::move(farC));

  bvh.build(world);

  GMANPoint const outerP(-100.0, 0.0, 0.0);
  GMANVector const outerTowardLight(1.0, 0.0, 0.0);
  RtFloat const outerDistance = 300.0;

  // Standalone baselines, nesting off, before the nested traversal runs.
  // Each shadow ray crosses exactly one opaque surface, so transmission
  // cuts to black; a free point (surfaceMagnitude 0) needs no offset.
  GMANColor const innerBaseline =
      occluder.transmission(innerLight, innerP, innerTowardLight, GMANVector(), innerDistance, 0.0f);
  check(colorsAgree(innerBaseline, GMANColor(0.0f, 0.0f, 0.0f)),
        "check B: the inner shadow's own standalone call transmits black through its real blocker");

  GMANLight const outerLight(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(-1.0, 0.0, 0.0));
  GMANColor const outerBaseline =
      occluder.transmission(outerLight, outerP, outerTowardLight, GMANVector(), outerDistance, 0.0f);
  check(colorsAgree(outerBaseline, GMANColor(0.0f, 0.0f, 0.0f)),
        "check B: the outer shadow's own standalone call transmits black through the far leaf's real blocker");

  // The real run: the outer transmission walk, with the probe's nesting
  // armed.
  probe->nestingEnabled = true;
  GMANColor const outerResult =
      occluder.transmission(outerLight, outerP, outerTowardLight, GMANVector(), outerDistance, 0.0f);

  check(probe->callCount >= 1, "check B: the probe was actually entered by the outer transmission walk");
  check(
      probe->nestedCalled && colorsAgree(probe->nestedResult, innerBaseline),
      "check B: the nested transmission, made mid-walk, answers exactly as the inner shadow's own standalone call did");
  check(colorsAgree(outerResult, outerBaseline),
        "check B: the outer transmission's own result is unchanged by the nested call inside it");
}

} // namespace

int main() {
  testNearestHitReentrant();
  testOccluderReentrant();

  return checkSummary("GMANRayBVH::nearestHit and GMANRayOccluder::transmission: a nested call, mid-traversal, "
                      "answers exactly as a standalone call would");
}
