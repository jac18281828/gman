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

#include <cstddef>
#include <vector>

#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmannormal.h"
#include "gmanpoint.h"
#include "gmanrayinterface.h"
#include "gmanvector.h"
#include "gmanworldmanager.h"
#include "ri.h"

/*
 * The renderer-facing emitter API: what radiates light, one interface a
 * path tracer's next-event estimation and a radiosity solver's own
 * shooting pass both read, public, installed and GMAN_EXPORT.
 *
 * Two queries only: sample draws a direction and its arriving radiance from
 * a receiving point, for next-event estimation; samplePoint draws a point
 * and its outgoing radiance on the emitter itself, for a shooting pass that
 * starts rays from the light instead. A delta light (point, spot or
 * distant) answers both through GMANLight's own sample(); an area emitter
 * -- a sphere or disk carrying a pending, illuminated RiAreaLightSource --
 * answers both by sampling its own shape.
 */
namespace gman {

// One thing that radiates light: a delta light (shape null) or a sphere or
// disk primitive carrying an area light (shape non-null). light is never
// null -- the enumerated GMANLight itself for a delta emitter, the
// arealight's own GMANLight for an area one.
struct GMAN_EXPORT Emitter {
  GMANLight const* light;
  GMANRayInterface const* shape;
  RtFloat power; // Phi; 0 for a delta emitter, which has no finite one
};

// One next-event-estimation draw: the direction toward the emitter, its
// distance (RI_INFINITY for a distant light), the radiance arriving along
// wi already scaled for a delta emitter's own pi convention, and the
// density that direction was drawn with, per unit solid angle at the
// query point -- 1, by convention, for a delta emitter's own discrete
// draw.
struct GMAN_EXPORT EmitterSample {
  GMANVector wi;
  RtFloat distance;
  GMANColor Cl;
  RtFloat pdf;
  bool isDelta;
};

// One shooting-pass draw: a point on the emitter, its outward normal
// (undefined for a delta emitter, which has no surface), the radiance it
// emits there (undoubled -- no pi scale, unlike EmitterSample's Cl), and
// the density that point was drawn with, per unit area for an area
// emitter.
struct GMAN_EXPORT EmitterPoint {
  GMANPoint point;
  GMANNormal normal;
  GMANColor Le;
  RtFloat pdf;
  bool isDelta;
};

// The union of every primitive's own Appearance::lights in world,
// deduplicated by pointer in first-seen order, ambient excluded, each
// becoming one delta Emitter; plus one area Emitter per primitive whose
// Appearance::areaLight is non-null and whose shape is an eligible,
// supported (sphere or disk) primitive. Counts and warns once, naming the
// count, when an Appearance::areaLight-tagged primitive is ineligible or
// unsupported. ambientCount, when non-null, receives the same walk's own
// count of distinct ambient lights -- a caller that also needs that count
// takes it from this one pass over world rather than walking it again.
GMAN_EXPORT std::vector<Emitter> emitters(GMANWorldManager& world, std::size_t* ambientCount = nullptr);

// A next-event-estimation draw toward emitter from p. A delta emitter
// ignores u1/u2 and forwards to its own light->sample(); an area emitter
// draws a point on its shape, places it by the shape's own
// object-to-camera transform, and reports Cl black when that placed point
// emits away from p.
GMAN_EXPORT EmitterSample sample(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2);

// A shooting-pass draw on emitter itself, ignoring u1/u2 for a delta
// emitter.
GMAN_EXPORT EmitterPoint samplePoint(Emitter const& emitter, RtFloat u1, RtFloat u2);

} // namespace gman
