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

#include <optional>

#include "gmandictionary.h"
#include "gmanpolygonmesh.h"
#include "ri.h"

// The only way to build a GMANPolygonMesh: internal to libgman, not
// installed and not GMAN_EXPORT, since only the RI layer's four polygon
// requests -- this library's own production code -- call it. Not exported
// because a tests-on build already exports every symbol for
// gman_internal_headers to reach.
//
// Each function runs the same four stages: validate its request's indices,
// warning and returning std::nullopt on the first rule broken; build the
// parameter list; require "P", returning std::nullopt with no warning when
// absent; then normalise the request into the mesh's faces and loops.
namespace gman {

// Polygon's own two rules, applied to its one loop.
std::optional<GMANPolygonMesh> polygonMesh(RtInt nverts, GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                           RtPointer parms[], RtInt const* counts);

// GeneralPolygon's own rules: nloops, then each nverts[i], then their sum.
std::optional<GMANPolygonMesh> generalPolygonMesh(RtInt nloops, RtInt const nverts[], GMANDictionary& dictionary,
                                                  RtInt n, RtToken tokens[], RtPointer parms[], RtInt const* counts);

// PointsPolygons' own rules: npolys, then nverts, verts and the resulting
// vertex count.
std::optional<GMANPolygonMesh> pointsPolygonsMesh(RtInt npolys, RtInt const nverts[], RtInt const verts[],
                                                  GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                                  RtPointer parms[], RtInt const* counts);

// PointsGeneralPolygons' own rules: npolys, then nloops and its sum, then
// nverts, verts and the resulting vertex count.
std::optional<GMANPolygonMesh> pointsGeneralPolygonsMesh(RtInt npolys, RtInt const nloops[], RtInt const nverts[],
                                                         RtInt const verts[], GMANDictionary& dictionary, RtInt n,
                                                         RtToken tokens[], RtPointer parms[], RtInt const* counts);

} // namespace gman
