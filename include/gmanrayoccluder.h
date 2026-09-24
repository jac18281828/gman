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

#pragma once

#include "gmanocclude.h"
#include "gmanraybvh.h"

namespace gman {

// The composite loop's own stop condition: a stack deeper than this many
// surfaces bounds a pathological scene rather than tracing it forever.
// Read by GMANRayOccluder::transmission's own walk and by a render loop's
// front-to-back composite over the same stack.
inline constexpr int kMaxCompositeLayers = 16;

// hitPoint displaced along Ng by the self-shadow offset, oriented toward
// reference by the sign of Ng . reference -- Ng carries no orientation
// guarantee of its own, and an offset on the wrong side would place the
// new origin inside the surface. reference is towardLight for a shadow
// ray (the offset lands toward the light) and the incoming ray's own
// direction for a composite loop (the offset lands on the far side of
// the surface the ray is continuing through), both unit length by
// construction. surfaceMagnitude is hitPoint's own surface's camera-space
// magnitude (0 for a free point), keying the offset to the surface's own
// size rather than to the camera distance alone.
GMAN_EXPORT GMANPoint offsetOrigin(GMANPoint const& hitPoint, GMANVector const& Ng, GMANVector const& reference,
                                   RtFloat surfaceMagnitude);

// 1 - c per channel: what a surface of opacity c leaves for whatever lies
// behind it.
GMAN_EXPORT GMANColor oneMinus(GMANColor const& c);

// Channel-wise product: how an attenuation composes with a colour or with
// another attenuation.
GMAN_EXPORT GMANColor multiplyChannels(GMANColor const& a, GMANColor const& b);

// True once transmission is invisible in an 8-bit image on every channel.
GMAN_EXPORT bool transmissionNegligible(GMANColor const& transmission);

} // namespace gman

/*
 * The ray tracer's own occlusion test: casts a shadow ray from P toward
 * the light and walks bvh's nearestHit repeatedly, each call's hit
 * shrinking the ray's remaining interval and compositing that blocker's
 * own opacity into the running transmission, until the interval is spent
 * or transmission is negligible -- the nearest hit at each step is what
 * lets the walk advance past exactly one blocker per call. A public,
 * standalone class so a unit test can probe transmission() directly
 * against a GMANRayBVH it controls. GMAN_EXPORT, along with GMANRayBVH:
 * the path tracer's own API; const and re-entrant, the same contract
 * GMANRayBVH's own class comment states.
 */
class GMAN_EXPORT GMANRayOccluder : public gman::Occluder {
public:
  explicit GMANRayOccluder(GMANRayBVH const& bvh) : bvh(bvh) {}

  GMANColor transmission(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                         GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude) const override;

private:
  GMANRayBVH const& bvh;
};
