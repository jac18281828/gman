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

#include "gman/rayexport.h"
#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanocclude.h"
#include "gmanpoint.h"
#include "gmanprimitive.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayinterface.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "ri.h"

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
// ray (the offset lands toward the light), the incoming ray's own
// direction for a composite loop (the offset lands on the far side of
// the surface the ray is continuing through), and an area emitter's own
// sampled point offset back toward the query point (the shadow walk's own
// end target, off the emitter's own surface), each unit length by
// construction. surfaceMagnitude is hitPoint's own surface's camera-space
// magnitude (0 for a free point), keying the offset to the surface's own
// size rather than to the camera distance alone.
GMANRAY_EXPORT GMANPoint offsetOrigin(GMANPoint const& hitPoint, GMANVector const& Ng, GMANVector const& reference,
                                      RtFloat surfaceMagnitude);

// 1 - c per channel: what a surface of opacity c leaves for whatever lies
// behind it.
GMANRAY_EXPORT GMANColor oneMinus(GMANColor const& c);

// Channel-wise product: how an attenuation composes with a colour or with
// another attenuation.
GMANRAY_EXPORT GMANColor multiplyChannels(GMANColor const& a, GMANColor const& b);

// True once transmission is invisible in an 8-bit image on every channel.
GMANRAY_EXPORT bool transmissionNegligible(GMANColor const& transmission);

// Each channel of os clamped to [0, 1], a NaN channel read as 1, RISpec's
// default Opacity, so a NaN never makes a surface clear: GMANMin and GMANMax
// drop a NaN operand, so GMANMin(NaN, 1) is 1 and GMANMax then leaves it at
// 1, where GMANClamp would map a NaN to 0.
GMANRAY_EXPORT GMANColor clampCoverage(GMANColor const& os);

// What one blocker does to a shadow ray: coverage is the blocker's opacity
// at the hit, clamped by clampCoverage, and transmittance the factor the
// ray's running transmission is multiplied by.
struct GMANRAY_EXPORT BlockerTransmittance {
  GMANColor coverage;
  GMANColor transmittance;
};

// A blocker's factor on a shadow ray: per channel (1 - c) + c * T, c the
// coverage, from gman::opacity at the shadow ray's hit, and T the
// closure's straight-through thin transmittance along the ray's direction,
// from gman::bsdf at that hit (BSDF::shadowTransmittance). A null-shader
// appearance gives c = Os and a Lambert lobe, so T is black and the factor
// 1 - Os. A non-finite channel of the factor reads as 0, blocked: a NaN
// transmittance never lights a surface. hit is shadowRay's own hit on the blocker whose appearance is
// appearance. textureCache is gman::opacity's and gman::bsdf's argument: a
// caller running threads passes the calling thread's own, and a null one
// samples through gman::textureCache()'s process cache.
GMANRAY_EXPORT BlockerTransmittance blockerTransmittance(GMANRay const& shadowRay, GMANHit const& hit,
                                                         Appearance const& appearance, GMANMatrix4 const& cameraToWorld,
                                                         TextureCache* textureCache);

// Whether a shadow ray that starts inside its mesh's facets passes hit, the
// first surface it crosses. origin is the primitive the shadow ray starts
// from and hitPrimitive the one hit names. True when all three hold:
// hitPrimitive and origin are polygons of one mesh; hit's shading normal
// faces towardLight, so the crossing is from behind the mesh's own normals,
// whatever the polygon's winding; and hit.t is at most the diagonal of
// origin's bounding box, so only the ray leaving the solid near its origin
// passes.
GMANRAY_EXPORT bool passesTerminatorExit(GMANHit const& hit, GMANRayInterface const& hitPrimitive,
                                         GMANVector const& towardLight, GMANPrimitive const& origin);

} // namespace gman

/*
 * The ray tracer's own occlusion test: casts a shadow ray from P toward
 * the light and walks bvh's nearestHit repeatedly, each call's hit
 * shrinking the ray's remaining interval and compositing that blocker's
 * own opacity into the running transmission, until the interval is spent
 * or transmission is negligible -- the nearest hit at each step is what
 * lets the walk advance past exactly one blocker per call. A public,
 * standalone class so a unit test can probe transmission() directly
 * against a GMANRayBVH it controls. GMANRAY_EXPORT, along with GMANRayBVH:
 * the path tracer's own API; const and re-entrant, the same contract
 * GMANRayBVH's own class comment states.
 */
class GMANRAY_EXPORT GMANRayOccluder : public gman::Occluder {
public:
  explicit GMANRayOccluder(GMANRayBVH const& bvh) : bvh(bvh) {}

  GMANColor transmission(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                         GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude) const override;

  // transmission, except that the walk's first hit contributes nothing,
  // and the walk continues past it, when gman::passesTerminatorExit passes
  // it for origin.
  GMANColor transmissionFromTerminator(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                                       GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude,
                                       GMANPrimitive const& origin) const override;

private:
  GMANColor walk(GMANPoint const& P, GMANVector const& towardLight, GMANVector const& Ng, RtFloat distance,
                 RtFloat surfaceMagnitude, GMANPrimitive const* terminatorOrigin) const;

  GMANRayBVH const& bvh;
};
