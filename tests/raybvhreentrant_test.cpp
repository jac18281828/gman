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
 * returns, and so never finds it.
 */

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

GMANTransform translated(RtFloat dx, RtFloat dy, RtFloat dz) {
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  return makeTransform(m);
}

GMANRaySphere* sphereAt(RtFloat x, RtFloat y, RtFloat z) {
  return new GMANRaySphere(0.5, -0.5, 0.5, 360.0, GMANParameterList(), translated(x, y, z));
}

// ---- check A: GMANRayBVH::nearestHit ----
//
// A probe primitive that, when armed, casts a second nearestHit against
// the same bvh for a fixed, unrelated ray before delegating to a real
// sphere for its own answer. Owns delegate (a raw pointer, so all five
// special members are accounted for): every other primitive in these
// fixtures is heap-allocated the same way, held by a
// GMANLinearWorldManager that deletes what it holds.
class NestingProbe : public GMANRayInterface {
public:
  NestingProbe(GMANRaySphere* delegatePrimitive, GMANRayBVH const& bvhToNest, GMANRay const& fixedInnerRay)
      : delegate(delegatePrimitive), bvh(bvhToNest), innerRay(fixedInnerRay) {
    bbox = delegate->getBBox();
  }
  NestingProbe(NestingProbe const&) = delete;
  NestingProbe& operator=(NestingProbe const&) = delete;
  NestingProbe(NestingProbe&&) = delete;
  NestingProbe& operator=(NestingProbe&&) = delete;
  ~NestingProbe() override { delete delegate; }

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
  mutable GMANRayInterface const* nestedPrimitive = nullptr;

private:
  GMANRaySphere* delegate;
  GMANRayBVH const& bvh;
  GMANRay innerRay;
};

bool nearestHitsAgree(bool foundA, GMANRayInterface const* primA, RtFloat tA, bool foundB,
                      GMANRayInterface const* primB, RtFloat tB) {
  if (foundA != foundB) {
    return false;
  }
  return !foundA || (primA == primB && tA == tB);
}

void testNearestHitReentrant() {
  GMANLinearWorldManager worldManager;

  // The inner ray's own target: off the outer ray's line (y == 0.6), so
  // the outer ray never reaches it directly; the far leaf's own union box
  // still spans it either way.
  GMANRay const innerRay(GMANPoint(11.0, 0.6, -50.0), GMANVector(0.0, 0.0, 1.0));

  GMANRayBVH bvh;

  // Near leaf: four spheres straddling the outer ray's line (y == 0) in
  // their union box (two at y == +0.6, two at y == -0.6) but each
  // individually clear of it, so every one of them, the probe included,
  // genuinely misses the outer ray.
  auto* realProbe = new NestingProbe(sphereAt(2.0, 0.6, 0.0), bvh, innerRay);
  worldManager.add(sphereAt(0.0, 0.6, 0.0));
  worldManager.add(sphereAt(1.0, -0.6, 0.0));
  worldManager.add(realProbe);
  worldManager.add(sphereAt(3.0, -0.6, 0.0));

  // Far leaf: the outer ray's only real hit (trueTarget, on its exact
  // line), the inner ray's own real target (innerTarget, off the outer
  // ray's line but on the inner ray's), and two more off-line fillers.
  GMANRayInterface* trueTarget = sphereAt(10.0, 0.0, 0.0);
  GMANRayInterface* innerTarget = sphereAt(11.0, 0.6, 0.0);
  worldManager.add(trueTarget);
  worldManager.add(innerTarget);
  worldManager.add(sphereAt(12.0, -0.6, 0.0));
  worldManager.add(sphereAt(13.0, 0.6, 0.0));

  bvh.build(worldManager);

  GMANRay const outerRay(GMANPoint(-100.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));

  // Standalone baselines, nesting off, before the nested traversal runs.
  GMANHit innerBaselineHit;
  GMANRayInterface const* innerBaselinePrim = nullptr;
  bool const innerBaselineFound = bvh.nearestHit(innerRay, innerBaselineHit, innerBaselinePrim);
  check(innerBaselineFound && innerBaselinePrim == innerTarget,
        "check A: the inner ray's own standalone call hits its real target");

  GMANHit outerBaselineHit;
  GMANRayInterface const* outerBaselinePrim = nullptr;
  bool const outerBaselineFound = bvh.nearestHit(outerRay, outerBaselineHit, outerBaselinePrim);
  check(outerBaselineFound && outerBaselinePrim == trueTarget,
        "check A: the outer ray's own standalone call hits the far leaf's real target");

  // The real run: the outer traversal, with the probe's nesting armed.
  realProbe->nestingEnabled = true;
  GMANHit outerHit;
  GMANRayInterface const* outerPrim = nullptr;
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
class OccluderNestingProbe : public GMANRayInterface {
public:
  OccluderNestingProbe(GMANRaySphere* delegatePrimitive, GMANRayOccluder const& occluderToNest,
                       GMANLight const& fixedInnerLight, GMANPoint const& fixedInnerP,
                       GMANVector const& fixedInnerTowardLight, RtFloat fixedInnerDistance)
      : delegate(delegatePrimitive), occluder(occluderToNest), innerLight(fixedInnerLight), innerP(fixedInnerP),
        innerTowardLight(fixedInnerTowardLight), innerDistance(fixedInnerDistance) {
    bbox = delegate->getBBox();
  }
  OccluderNestingProbe(OccluderNestingProbe const&) = delete;
  OccluderNestingProbe& operator=(OccluderNestingProbe const&) = delete;
  OccluderNestingProbe(OccluderNestingProbe&&) = delete;
  OccluderNestingProbe& operator=(OccluderNestingProbe&&) = delete;
  ~OccluderNestingProbe() override { delete delegate; }

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
  GMANRaySphere* delegate;
  GMANRayOccluder const& occluder;
  GMANLight const& innerLight;
  GMANPoint innerP;
  GMANVector innerTowardLight;
  RtFloat innerDistance;
};

bool colorsAgree(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

void setOpaque(GMANRayInterface* primitive) {
  gman::Appearance opaque;
  opaque.Os = GMANColor(1.0f, 1.0f, 1.0f);
  primitive->setAppearance(opaque);
}

void testOccluderReentrant() {
  GMANLinearWorldManager worldManager;

  GMANLight const innerLight(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0, 0.0, -1.0));
  GMANPoint const innerP(11.0, 0.6, -50.0);
  GMANVector const innerTowardLight(0.0, 0.0, 1.0);
  RtFloat const innerDistance = 100.0;

  GMANRayBVH bvh;
  GMANRayOccluder occluder(bvh);

  auto* probeDelegate = sphereAt(2.0, 0.6, 0.0);
  setOpaque(probeDelegate);
  auto* probe = new OccluderNestingProbe(probeDelegate, occluder, innerLight, innerP, innerTowardLight, innerDistance);

  GMANRayInterface* nearA = sphereAt(0.0, 0.6, 0.0);
  GMANRayInterface* nearB = sphereAt(1.0, -0.6, 0.0);
  GMANRayInterface* nearC = sphereAt(3.0, -0.6, 0.0);
  setOpaque(nearA);
  setOpaque(nearB);
  setOpaque(nearC);
  worldManager.add(nearA);
  worldManager.add(nearB);
  worldManager.add(probe);
  worldManager.add(nearC);

  GMANRayInterface* trueBlocker = sphereAt(10.0, 0.0, 0.0);
  GMANRayInterface* innerBlocker = sphereAt(11.0, 0.6, 0.0);
  GMANRayInterface* farB = sphereAt(12.0, -0.6, 0.0);
  GMANRayInterface* farC = sphereAt(13.0, 0.6, 0.0);
  setOpaque(trueBlocker);
  setOpaque(innerBlocker);
  setOpaque(farB);
  setOpaque(farC);
  worldManager.add(trueBlocker);
  worldManager.add(innerBlocker);
  worldManager.add(farB);
  worldManager.add(farC);

  bvh.build(worldManager);

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
