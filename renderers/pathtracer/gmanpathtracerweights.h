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

#include "gmanemitter.h"
#include "gmanpoint.h"
#include "gmanrayinterface.h"
#include "ri.h"

/*
 * The path tracer's multiple-importance-sampling weight computation,
 * defined in gmanpathtracerenderer.cpp. Private to renderers/pathtracer:
 * never installed and never exported, so tests reach it through the
 * gman_pathtracer_objects include directory and call it without rendering
 * a frame.
 */
namespace gman {

// One vertex's own light-choice weights at p: an emitter whose shape is
// primitive gets weight 0, since a point on an emitter's own surface
// cannot light itself, and the rest keep gman::lightChoiceWeight's own
// value. lightWeight is the caller's own per-path buffer, sized to
// emitters.size() and overwritten here, never reallocated per vertex.
// Returns the weights' own sum. The one function chooseLight's draw and
// every probability read below share, so a value read at a different
// vertex through it is provably the probability next-event estimation
// would have drawn for that emitter, had it drawn from p (excluding the
// same primitive) that round.
RtFloat lightChoiceWeights(std::vector<gman::Emitter> const& emitters, GMANPoint const& p,
                           GMANRayInterface const* primitive, std::vector<RtFloat>& lightWeight);

// emitters[index]'s own share of lightChoiceWeights' own sum at p,
// excluding primitive. lightWeight is the caller's own buffer, sized to
// emitters.size(); its contents are overwritten.
RtFloat lightChoiceProbability(std::vector<gman::Emitter> const& emitters, GMANPoint const& p,
                               GMANRayInterface const* primitive, std::size_t index, std::vector<RtFloat>& lightWeight);

// Next-event estimation's own weight toward a chosen light: 1 for a delta
// light at any vertex, since nothing can ever land on one by chance and no
// competing technique exists; 1 also when misEnabled is false, light-only
// sampling's own rule. Otherwise the power heuristic between the light's
// own solid-angle density (pLight, pChoice(j) times the draw's own
// EmitterSample::pdf) and the departing BSDF's own density toward the same
// direction (pBsdf).
RtFloat nextEventWeight(bool lightIsDelta, bool misEnabled, RtFloat pLight, RtFloat pBsdf);

// An emitter hit's own weight: 1 whenever no BSDF draw produced the
// arriving ray at all, or the most recent one was a delta lobe -- the one
// case with no competing technique, and the one where next-event
// estimation's own term at that vertex is already black, so nothing here
// can double either. Otherwise the power heuristic between the departing
// draw's own density (pBsdf) and the light-choice/solid-angle density
// next-event estimation would have used for this same emitter from that
// same vertex (pLight); 0, not 1, when misEnabled is false, since
// light-only sampling never credits this case at all.
RtFloat emitterHitWeight(bool rayEligibleForEmitterHit, bool misEnabled, RtFloat pBsdf, RtFloat pLight);

// An emitter hit's own eligibility, tracked across delta draws since the
// most recent non-delta departure: eligible (no departure yet, or a delta
// reflection since restored today's rule), weighted (the immediate next
// hit after a non-delta departure, MIS weighted against it) or suppressed
// (every draw since a non-delta departure was a delta transmission, so
// that departure's own shadow ray already carried the light through those
// surfaces and the hit adds nothing).
enum class EmitterHitEligibility { eligible, weighted, suppressed };

// current is the eligibility before this draw; isDelta and isTransmission
// describe the draw just taken (isTransmission meaningless unless
// isDelta). A non-delta draw always departs afresh (weighted). A delta
// reflection always restores eligible, whatever current was. A delta
// transmission extends a suppressed or weighted chain into suppressed,
// since a departure already exists to carry the light through; it leaves
// an eligible chain eligible, since no departure exists yet to double.
EmitterHitEligibility nextEmitterHitEligibility(EmitterHitEligibility current, bool isDelta, bool isTransmission);

} // namespace gman
