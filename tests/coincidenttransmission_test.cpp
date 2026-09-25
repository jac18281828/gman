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
 * More transparent layers than gman::kMaxCompositeLayers, spaced widely
 * enough (1 world unit, far past
 * the offset's own hop size at this magnitude) that the walk must find
 * each one in its own iteration rather than clearing several in a single
 * hop. gman::offsetOrigin orients every hop toward the light regardless
 * of which surface's own normal produced it (see gmanrayoccluder.cpp's
 * own comment), so it always advances and two exactly coincident surfaces
 * clear together in one hop -- never stuck re-finding the same point, and
 * so never a fixture for this check. Twenty separated layers, each
 * attenuating by a fixed factor, forces exactly what the cap is for:
 * bounding a walk's own depth against a pathological stack, not
 * preventing infinite recursion that this offset design does not
 * produce.
 *
 * RI_INFINITY carries the walk past every layer with no interval of its
 * own to run out first, so only the cap can end it early. The capped
 * result must match kMaxCompositeLayers layers' own attenuation, not all
 * twenty's -- the two are far enough apart that either observed value
 * settles which the walk actually crossed.
 */

#include <cmath>
#include <cstdio>
#include <vector>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr int kLayerCount = 20;         // more than gman::kMaxCompositeLayers (16)
constexpr RtFloat kLayerSpacing = 1.0f; // world units; far past the offset's own hop size at this magnitude
constexpr RtFloat kFirstLayerZ = 5.0f;
constexpr RtFloat kOpacity = 0.1f; // low enough that 20 layers still clear kTransmissionCutoff

GMANRayPolygon* squareAt(RtFloat z) {
  std::vector<GMANPoint> const verts = {
      GMANPoint(-10.0f, -10.0f, z),
      GMANPoint(-10.0f, 10.0f, z),
      GMANPoint(10.0f, 10.0f, z),
      GMANPoint(10.0f, -10.0f, z),
  };
  GMANRayPolygon* polygon = new GMANRayPolygon(verts, GMANParameterList());
  gman::Appearance transparent;
  transparent.Os = GMANColor(kOpacity, kOpacity, kOpacity);
  polygon->setAppearance(transparent);
  return polygon;
}

} // namespace

int main() {
  GMANLinearWorldManager worldManager;
  // GMANLinearWorldManager owns what it holds and deletes it on
  // destruction, so every square is heap-allocated rather than a stack
  // object.
  for (int i = 0; i < kLayerCount; ++i) {
    worldManager.add(squareAt(kFirstLayerZ + (RtFloat)i * kLayerSpacing));
  }
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);

  GMANVector towardLight(0.0f, 0.0f, 1.0f);
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0f, 0.0f, -1.0f));

  GMANPoint const P(0.0f, 0.0f, 0.0f);
  GMANVector const Ng(0.0f, 0.0f, -1.0f); // facing the shaded point away from the stack, toward the light
  // P is a free-space point, not on any primitive: surfaceMagnitude 0.0,
  // the header's own contract for that case.
  GMANColor const result = occluder.transmission(light, P, towardLight, Ng, RI_INFINITY, 0.0f);

  RtFloat const cappedPrediction = std::pow(1.0f - kOpacity, (RtFloat)gman::kMaxCompositeLayers);
  RtFloat const allLayersPrediction = std::pow(1.0f - kOpacity, (RtFloat)kLayerCount);
  std::printf("check 7: result=%.6f capped(%d)=%.6f all(%d)=%.6f\n", result.getRed(), gman::kMaxCompositeLayers,
              cappedPrediction, kLayerCount, allLayersPrediction);

  check(std::fabs(result.getRed() - cappedPrediction) <= 1e-4f,
        "check 7: the walk stops at kMaxCompositeLayers, not the stack's own depth");
  check(std::fabs(result.getRed() - allLayersPrediction) > 1e-3f,
        "check 7: the result is not what an uncapped walk through all twenty layers would read");

  return checkSummary("R7's shadow walk: a stack deeper than kMaxCompositeLayers stops at the cap");
}
