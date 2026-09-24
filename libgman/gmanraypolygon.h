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

#include <utility>
#include <vector>

#include "gmanrayinterface.h"

class GMAN_EXPORT GMANRayPolygon : public GMANRayInterface, public GMANPolygon {
public:
  // outer's vertices are already in camera space (the CTM at RiPolygonV's
  // own call, applied once per vertex by the factory): a polygon is flat,
  // so its transformed vertices are its whole geometry and no per-ray
  // matrix work is needed. Captured by value -- RiPolygonV deletes its
  // transform right after the factory returns, so there is nothing to hold
  // a reference to. A Polygon (the one-loop case) constructs this way,
  // with no holes.
  GMANRayPolygon(std::vector<GMANPoint> outer, GMANParameterList pl);

  // A GeneralPolygon: outer is the boundary loop, each entry of holes a
  // loop cut out of it (RISpec's even-odd loop rule). texCoords resolve
  // from pl's own leading entries, the outer loop's flat "P" slots -- the
  // same rule the one-loop constructor uses.
  GMANRayPolygon(std::vector<GMANPoint> outer, std::vector<std::vector<GMANPoint>> holes, GMANParameterList pl);

  // A Points*/PointsGeneralPolygons face: outerTexCoords is already
  // gathered per vertex, through the request's own "verts", since no flat
  // pl indexes this face's vertices by local slot.
  GMANRayPolygon(std::vector<GMANPoint> outer, std::vector<std::vector<GMANPoint>> holes,
                 std::vector<std::pair<RtFloat, RtFloat>> outerTexCoords);

  bool intersect(const GMANRay& ray, GMANHit& hit) const;

private:
  // The plane, the Newell normal and the degeneracy test all come from
  // this loop alone. A hit lands inside it and outside every loop in
  // holes, each loop tested on its own -- no bridging, no combined
  // crossing count.
  std::vector<GMANPoint> vertices;
  std::vector<std::vector<GMANPoint>> holes;

  // The Newell normal, unit length, and whether vertices even describes a
  // real face -- both computed once here rather than per ray.
  GMANVector normal;
  bool degenerate;

  // Each outer-loop vertex's own resolved (s, t) -- default its
  // object-space "P", "st" then "s"/"t" overriding,
  // GMANPatchPolyObjectManager's own polygon rule -- computed once here
  // rather than per ray. Every entry is (0, 0) when pl carries no "P"
  // (direct construction bypassing GMANRayObjectManager::getRSPolygon).
  // Holes carry no texCoords: a hit never lands inside one.
  std::vector<std::pair<RtFloat, RtFloat>> texCoords;

  // Computes bbox, degeneracy and the unit Newell normal from vertices --
  // the outer loop alone, regardless of how many holes it carries. Shared
  // by every constructor once vertices/holes are set.
  void initGeometry();
};
