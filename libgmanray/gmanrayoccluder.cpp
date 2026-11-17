/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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

#include <cmath>
#include <limits>

#include "gmanmath.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"

namespace {

// u: half an RtFloat ulp at 1.0. kSelfShadowOffsetScale below scales the
// self-shadow offset in units of u.
constexpr RtFloat kUnitRoundoff = std::numeric_limits<RtFloat>::epsilon() / (RtFloat)2.0;

// The distance a ray continuing past a hit displaces its own origin,
// along the surface's geometric normal, keyed on the larger of the hit
// point's own coordinate magnitude and the hit primitive's own
// camera-space extent (M) -- a primitive's own size, not the camera
// distance alone, since a large surface near the camera needs an offset
// proportional to itself, not to |P|. kSelfShadowOffsetFloor keeps a hit
// point at or near the origin, where the scaled term vanishes, a positive
// offset. kSelfShadowOffsetCeiling caps the result at
// kSelfShadowOffsetCeiling * max|P|, the same bound the old, unkeyed
// offset used, so a large receiver near the camera never exceeds it, and
// its own contact shadows are no worse.
//
// c (16) is 4x the self-shadow sweep's own measured minimum self-hit-free
// c on the power-of-two grid {1, 2, 4, 8, ...}: the nofma build still
// self-hits at c = 2 but clears every quadric and the torus, at x1 and
// x1000, at c = 4. 16 clears that floor with a 4x margin.
constexpr RtFloat kSelfShadowOffsetScale = (RtFloat)16.0 * kUnitRoundoff;
constexpr RtFloat kSelfShadowOffsetCeiling = (RtFloat)3.0e-5;
constexpr RtFloat kSelfShadowOffsetFloor = (RtFloat)1.0e-9;

// A transmission below this in every channel is invisible in an 8-bit
// image.
constexpr RtFloat kTransmissionCutoff = (RtFloat)(1.0 / 255.0);

// The self-shadow offset's own magnitude at hitPoint (see
// kSelfShadowOffsetScale/kSelfShadowOffsetFloor); gman::offsetOrigin below
// scales Ng by this and orients the result, and transmission's own loop
// guard compares a ray's remaining interval against it directly.
// surfaceMagnitude is the hit primitive's own M (0 for a free point, no
// surface known), so max|P| alone applies there.
RtFloat selfShadowOffsetMagnitude(GMANPoint const& hitPoint, RtFloat surfaceMagnitude) {
  RtFloat const maxP =
      GMANMax(GMANMax(std::fabs(hitPoint.getX()), std::fabs(hitPoint.getY())), std::fabs(hitPoint.getZ()));
  RtFloat const keyMagnitude = GMANMax(maxP, surfaceMagnitude);
  RtFloat const scaled = GMANMin(kSelfShadowOffsetCeiling * maxP, kSelfShadowOffsetScale * keyMagnitude);
  return GMANMax(kSelfShadowOffsetFloor, scaled);
}

} // namespace

namespace gman {

GMANPoint offsetOrigin(GMANPoint const& hitPoint, GMANVector const& Ng, GMANVector const& reference,
                       RtFloat surfaceMagnitude) {
  RtFloat const magnitude = selfShadowOffsetMagnitude(hitPoint, surfaceMagnitude);
  RtFloat const offset = (Ng.dot(reference) < (RtFloat)0.0) ? -magnitude : magnitude;
  return GMANPoint(hitPoint.getX() + Ng.getX() * offset, hitPoint.getY() + Ng.getY() * offset,
                   hitPoint.getZ() + Ng.getZ() * offset);
}

GMANColor multiplyChannels(GMANColor const& a, GMANColor const& b) {
  return GMANColor(a.getRed() * b.getRed(), a.getGreen() * b.getGreen(), a.getBlue() * b.getBlue());
}

GMANColor oneMinus(GMANColor const& c) { return GMANColor(1.0f - c.getRed(), 1.0f - c.getGreen(), 1.0f - c.getBlue()); }

bool transmissionNegligible(GMANColor const& transmission) {
  return transmission.getRed() < kTransmissionCutoff && transmission.getGreen() < kTransmissionCutoff &&
         transmission.getBlue() < kTransmissionCutoff;
}

} // namespace gman

GMANColor GMANRayOccluder::transmission(GMANLight const& /*light*/, GMANPoint const& P, GMANVector const& towardLight,
                                        GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude) const {
  GMANColor transmission(1.0f, 1.0f, 1.0f);
  GMANPoint origin = gman::offsetOrigin(P, Ng, towardLight, surfaceMagnitude);
  RtFloat remaining = distance;
  // The surface origin currently sits on: the caller's own P until the
  // walk crosses a blocker, then that blocker's own M.
  RtFloat currentMagnitude = surfaceMagnitude;

  // A closed solid contributes one (1 - Os) factor per surface the shadow
  // ray crosses, not per blocker: bvh.nearestHit keeps one hit per call,
  // so this walks the interval itself, moving origin/remaining past each
  // surface found. This matches a render loop's own composite, which
  // likewise crosses both shells of a sphere the ray enters. The cap
  // bounds a stack deeper than it, not an infinite walk: offsetOrigin
  // always advances toward reference regardless of which surface's own
  // normal produced it, so a zero-opacity surface stack never decays
  // transmission enough to break the loop on its own, and a pathological
  // stack deeper than the cap would otherwise be walked in full.
  for (int layer = 0; layer < gman::kMaxCompositeLayers; ++layer) {
    // A light nearer than the offset itself still has to end the walk,
    // though at this offset's scale that is effectively never.
    if (selfShadowOffsetMagnitude(origin, currentMagnitude) >= remaining) {
      break;
    }
    GMANRay const shadowRay(origin, towardLight, RI_EPSILON, remaining);
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!bvh.nearestHit(shadowRay, hit, hitPrimitive)) {
      break;
    }
    transmission = gman::multiplyChannels(transmission, gman::oneMinus(hitPrimitive->getAppearance().Os));
    if (gman::transmissionNegligible(transmission)) {
      return GMANColor(0.0f, 0.0f, 0.0f);
    }
    // The surface this iteration is leaving, not the Ng the caller
    // passed: that one belongs to the first hit only, a different
    // surface once the walk has crossed it.
    currentMagnitude = gman::primitiveMagnitude(hitPrimitive->getBBox());
    origin = gman::offsetOrigin(hit.point, hit.normal, towardLight, currentMagnitude);
    remaining -= hit.t;
  }
  return transmission;
}
