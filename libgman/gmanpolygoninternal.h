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

#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "ri.h"

// Shared by gmanpolygon.cpp, gmanpatchpolyobjectmanager.cpp,
// gmanraypolygon.cpp and gmanrayobjectmanager.cpp, all built into
// gman_core: internal to libgman, not installed and not GMAN_EXPORT, since
// nothing outside this library needs it.
namespace gman {

// The ring's largest bounding-box side, in whichever of x, y or z spans it
// widest. isDegeneratePolygon and gmanpatchpolyobjectmanager.cpp's own
// coincidence tolerances judge a ring's area or an edge length against
// this extent rather than against an absolute constant, so a sliver a
// million times longer than it is wide reads the same way at any scale.
RtFloat boundingBoxExtent(std::vector<GMANPoint> const& ring);

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
std::vector<std::pair<RtFloat, RtFloat>> polygonTexCoords(GMANParameterList const& pl, RtFloat const* p,
                                                          std::size_t count);

} // namespace gman
