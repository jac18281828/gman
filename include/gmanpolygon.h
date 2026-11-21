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
#include <utility>
#include <vector>

#include "gman/export.h"
#include "gmandictionary.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

// Newell's method: the face normal as the sum of every edge's
// contribution, rather than the cross product of two edges at one
// arbitrarily chosen vertex. Correct for any simple planar polygon,
// including one where three consecutive vertices form a reflex corner.
// Not normalized -- its magnitude is twice the ring's own area.
GMAN_EXPORT GMANVector newellNormal(std::vector<GMANPoint> const& ring);

// True for a ring too small or too thin to shade or intersect: fewer than
// three vertices, or a Newell normal whose magnitude, against the ring's
// own bounding-box extent, reads as a sliver rather than a real face.
GMAN_EXPORT bool isDegeneratePolygon(std::vector<GMANPoint> const& ring);

// The dictionary every standard RI_* token (e.g. RI_P) resolves against,
// shared by both renderers so each parameter list is read with the same
// token IDs it was built with. Must not be mutated concurrently: callers
// only look tokens up, never declare new ones through this instance.
GMAN_EXPORT GMANDictionary& standardDictionary();

// The ring's largest bounding-box side, in whichever of x, y or z spans it
// widest. A coincidence tolerance that judges a ring's area or an edge
// length against this extent, rather than against an absolute constant,
// reads a sliver a million times longer than it is wide the same way at any
// scale.
GMAN_EXPORT RtFloat boundingBoxExtent(std::vector<GMANPoint> const& ring);

// pl's token array for token, cast to RtFloat*: NULL when pl carries no
// such token, non-NULL otherwise. token must be one standardDictionary()
// pre-registers (RI_P, RI_PW, RI_S, RI_T, RI_ST), so getTokenId never
// throws.
inline RtFloat* floatArray(GMANParameterList const& pl, RtToken token) {
  return (RtFloat*)pl.getPointer(standardDictionary().getTokenId(token));
}

// Each of count points' own resolved (s, t): entry i defaults to (p[3i],
// p[3i+1]), overridden by "st" (both components), then by "s" and "t"
// (each its own component). p is non-null and holds 3 * count floats. The
// one definition of the rule every polygon and ray-polygon face resolves
// its texture coordinates through.
GMAN_EXPORT std::vector<std::pair<RtFloat, RtFloat>> polygonTexCoords(GMANParameterList const& pl, RtFloat const* p,
                                                                      std::size_t count);

} // namespace gman
