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
 * R9 proof, §8 B: the contact gap. A small receiver (M ~ max|P|) shadows
 * at a gap the old, unkeyed 3e-5*max|P| offset hid; a large receiver
 * (M >> max|P|, a radius-1000 sphere) keeps today's own contact shadow,
 * since kSelfShadowOffsetCeiling caps the magnitude-keyed offset at what
 * the old constant already gave. Both fixtures use an open, single-sided
 * polygon blocker: a closed solid would put even the old offset's origin
 * inside it, where the exit wall still blocks, hiding the defect this
 * file exists to catch.
 *
 * kUnitRoundoff/kDerivedC/kCeiling below mirror
 * gmanraytracerenderer.cpp's own kSelfShadowOffsetScale/
 * kSelfShadowOffsetCeiling literally, not read back from that
 * file-local anonymous namespace: this file's own choice of g depends on
 * their value, stated here as the fixtures' own literal expectation, the
 * way raybbox_test.cpp's own literal expected pads do.
 */

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtFloat kUnitRoundoff = std::numeric_limits<RtFloat>::epsilon() / (RtFloat)2.0;
constexpr RtFloat kDerivedC = (RtFloat)16.0;
constexpr RtFloat kScale = kDerivedC * kUnitRoundoff;
constexpr RtFloat kCeiling = (RtFloat)3.0e-5;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// A radius-scale sphere centred at (0, 0, centreZ), opaque white --
// rayoccluder_test.cpp's own sphereAt convention.
GMANRaySphere* sphereAt(RtFloat radius, RtFloat centreZ) {
  GMANMatrix4 place;
  place.trans(0.0, 0.0, (double)centreZ);
  GMANRaySphere* sphere = new GMANRaySphere(radius, -radius, radius, 360.0f, GMANParameterList(), makeTransform(place));
  gman::Appearance opaque;
  opaque.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(opaque);
  return sphere;
}

// An open, single-sided square facing -z, centred at (0, 0, z) -- a flat
// sheet contributes exactly one hit per crossing, unlike a closed solid's
// two, so an offset that carries an origin past its own thin gap finds no
// second wall to still catch it.
GMANRayPolygon* squareAt(RtFloat z, RtFloat halfWidth) {
  std::vector<GMANPoint> const verts = {
      GMANPoint(-halfWidth, -halfWidth, z),
      GMANPoint(-halfWidth, halfWidth, z),
      GMANPoint(halfWidth, halfWidth, z),
      GMANPoint(halfWidth, -halfWidth, z),
  };
  GMANRayPolygon* polygon = new GMANRayPolygon(verts, GMANParameterList());
  gman::Appearance opaque;
  opaque.Os = GMANColor(1.0f, 1.0f, 1.0f);
  polygon->setAppearance(opaque);
  return polygon;
}

bool isWhite(GMANColor const& c) { return c.getRed() > 0.99f && c.getGreen() > 0.99f && c.getBlue() > 0.99f; }
bool isBlack(GMANColor const& c) { return c.getRed() < 0.01f && c.getGreen() < 0.01f && c.getBlue() < 0.01f; }

// The small receiver: M ~ max|P|, a sphere of radius 5 hit at its near
// pole (0,0,5) -- max|P| == 5 exactly, M measured off the sphere's own
// bbox, close to 5. Ng.L == 1 (near-normal), so the fixture's own
// Ng.L > g / (kCeiling * max|P|) condition holds however g is chosen
// inside the window.
void checkSmallReceiver() {
  RtFloat const maxP = 5.0f;
  GMANPoint const hitPoint(0.0f, 0.0f, maxP);
  GMANVector const Ng(0.0f, 0.0f, 1.0f);
  GMANVector const towardLight(0.0f, 0.0f, 1.0f);
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0f, 0.0f, -1.0f));

  GMANRaySphere* receiver = sphereAt(maxP, 0.0f);
  RtFloat const M = receiver->getBBox().getMax().getZ(); // ~5, padded
  RtFloat const magnitude = GMANMax(maxP, M);

  // Window (§1): 4*c*u*max(max|P|,M) <= g <= 0.5*kCeiling*max|P|.
  RtFloat const windowLow = 4.0f * kDerivedC * kUnitRoundoff * magnitude;
  RtFloat const windowHigh = 0.5f * kCeiling * maxP;
  RtFloat const g = 4.0e-5f;
  check(g >= windowLow && g <= windowHigh, "small receiver setup: g lies inside §1's contact-gap window");

  RtFloat const oldOffset = kCeiling * maxP;
  check(g < oldOffset, "small receiver setup: g is a gap the unkeyed old offset would have carried past");

  RtFloat const newOffset = GMANMin(kCeiling * maxP, kScale * magnitude);
  std::printf("small receiver: max|P|=%.6g M=%.6g g=%.6g old-offset=%.6g new-offset=%.6g Ng.L=%.6g\n", (double)maxP,
              (double)M, (double)g, (double)oldOffset, (double)newOffset, (double)Ng.dot(towardLight));

  {
    GMANLinearWorldManager worldManager;
    worldManager.add(receiver);
    worldManager.add(squareAt(maxP + g, 1.0f));
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);
    GMANColor const result = occluder.transmission(light, hitPoint, towardLight, Ng, RI_INFINITY, magnitude);
    check(isBlack(result), "small receiver: transmission returns black at the contact gap");
  }
  {
    GMANRaySphere* alone = sphereAt(maxP, 0.0f);
    GMANLinearWorldManager worldManager;
    worldManager.add(alone);
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);
    GMANColor const result = occluder.transmission(light, hitPoint, towardLight, Ng, RI_INFINITY, magnitude);
    check(isWhite(result), "small receiver: with no blocker, the same point returns white");
  }
  {
    GMANRaySphere* alone = sphereAt(maxP, 0.0f);
    GMANLinearWorldManager worldManager;
    worldManager.add(alone);
    worldManager.add(squareAt(maxP + 4.0f * kCeiling * maxP, 1.0f));
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);
    GMANColor const result = occluder.transmission(light, hitPoint, towardLight, Ng, RI_INFINITY, magnitude);
    check(isBlack(result), "small receiver: a blocker at 4*kCeiling*max|P| still transmits black");
  }
}

// The large receiver: M >> max|P|, a radius-1000 sphere placed so its own
// near pole sits at max|P| == 5 (centre (0,0,1005)), M ~ 2005. g ==
// 4*kCeiling*max|P|, the same gap the small receiver's own third check
// uses; Ng.L == 1 comfortably clears Ng.L > g / (kScale*M).
void checkLargeReceiver() {
  RtFloat const radius = 1000.0f;
  RtFloat const maxP = 5.0f;
  RtFloat const centreZ = radius + maxP;
  GMANPoint const hitPoint(0.0f, 0.0f, maxP);
  GMANVector const Ng(0.0f, 0.0f, -1.0f); // outward normal at the near pole, facing the camera
  GMANVector const towardLight(0.0f, 0.0f, -1.0f);
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0f, 0.0f, 1.0f));

  GMANRaySphere* receiver = sphereAt(radius, centreZ);
  RtFloat const M = receiver->getBBox().getMax().getZ(); // ~ centreZ + radius, ~2005
  RtFloat const magnitude = GMANMax(maxP, M);

  RtFloat const g = 4.0f * kCeiling * maxP;
  RtFloat const oldOffset = kCeiling * maxP;
  RtFloat const uncappedOffset = kScale * magnitude;
  RtFloat const newOffset = GMANMin(oldOffset, uncappedOffset);
  check(uncappedOffset > g, "large receiver setup: without the ceiling, kScale*M would exceed g");
  check(newOffset < g, "large receiver setup: the ceiling keeps the offset under g");

  std::printf("large receiver: max|P|=%.6g M=%.6g g=%.6g offset-without-ceiling=%.6g offset-with-ceiling=%.6g "
              "Ng.L=%.6g\n",
              (double)maxP, (double)M, (double)g, (double)uncappedOffset, (double)newOffset,
              (double)Ng.dot(towardLight));

  GMANLinearWorldManager worldManager;
  worldManager.add(receiver);
  worldManager.add(squareAt(maxP - g, 1.0f));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANColor const result = occluder.transmission(light, hitPoint, towardLight, Ng, RI_INFINITY, magnitude);
  check(isBlack(result), "large receiver: transmission returns black -- the ceiling keeps today's own contact");
}

} // namespace

int main() {
  checkSmallReceiver();
  checkLargeReceiver();

  return checkSummary("R9's contact gap: a small receiver shadows at a gap the old offset hid, and a large "
                      "receiver's ceiling keeps today's own contact");
}
