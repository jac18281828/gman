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
  // Phi, pi * mean(Le) * A_cam, A_cam the emitter's own placed area: exact
  // for a disk, whose area Jacobian is constant over its own plane; an
  // estimate for a sphere under a non-uniform placement, whose Jacobian
  // varies pointwise. power feeds only the light-choice weight, where any
  // positive weight keeps the estimator unbiased, so the estimate costs
  // variance, never correctness. 0 for a delta emitter, which has no
  // finite one.
  RtFloat power;
};

// One next-event-estimation draw: the direction toward the emitter, its
// true distance to the sampled point (RI_INFINITY for a distant light),
// the shadow walk's own end point for an area draw (off the emitter's own
// surface, toward the query point; unset and never read for a delta
// draw), the radiance arriving along wi already scaled for a delta
// emitter's own pi convention, and the density that direction was drawn
// with, per unit solid angle at the query point -- 1, by convention, for
// a delta emitter's own discrete draw.
struct GMAN_EXPORT EmitterSample {
  GMANVector wi;
  RtFloat distance;
  GMANPoint shadowTarget;
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

// The solid-angle density sample() reports in EmitterSample::pdf for a
// freshly drawn point on emitter, generalized to a point already known --
// a BSDF-sampled bounce's own hit, which sample() never drew. p is the
// receiving point, hitPoint the known point on emitter and hitNormal its
// outward normal there, all camera space. 0 when hitNormal's cosine to the
// direction from hitPoint back to p is at or below 0 (emitter's own
// non-emitting side) or when p and hitPoint coincide -- unlike
// EmitterSample::pdf, which a back-facing draw still reports nonzero,
// since that draw's own Cl already goes black on that side and no weight
// computed from either pdf ever multiplies it. Precondition: emitter.shape
// != nullptr (an area emitter, which alone has a surface to place a known
// point against); checked by assert.
GMAN_EXPORT RtFloat lightSolidAnglePdf(Emitter const& emitter, GMANPoint const& p, GMANPoint const& hitPoint,
                                       GMANNormal const& hitNormal);

// A shooting-pass draw on emitter itself, ignoring u1/u2 for a delta
// emitter.
GMAN_EXPORT EmitterPoint samplePoint(Emitter const& emitter, RtFloat u1, RtFloat u2);

// emitter's own weight for next-event estimation's light choice at p: a
// delta emitter's fixed preview draw of its own sample(); an area emitter's
// power divided by four times the greater of its placed centre's squared
// distance from p and its own squared bounding radius -- strictly positive
// wherever it contributes, unlike a fixed preview draw whose own
// object-space point may face away from p.
GMAN_EXPORT RtFloat lightChoiceWeight(Emitter const& emitter, GMANPoint const& p);

} // namespace gman
