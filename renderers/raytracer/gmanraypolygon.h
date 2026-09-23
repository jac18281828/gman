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
  // vertices are already in camera space (the CTM at RiPolygonV's own call,
  // applied once per vertex by the factory): a polygon is flat, so its
  // transformed vertices are its whole geometry and no per-ray matrix work
  // is needed. Captured by value -- RiPolygonV deletes its transform right
  // after the factory returns, so there is nothing to hold a reference to.
  GMANRayPolygon(std::vector<GMANPoint> vertices, GMANParameterList pl);

  bool intersect(const GMANRay& ray, GMANHit& hit) const;

private:
  std::vector<GMANPoint> vertices;

  // The Newell normal, unit length, and whether it and vertices even
  // describe a real face -- both computed once here rather than per ray.
  GMANVector normal;
  bool degenerate;

  // Each vertex's own resolved (s, t) -- default its object-space "P",
  // "st" then "s"/"t" overriding, GMANPatchPolyObjectManager's own polygon
  // rule -- computed once here rather than per ray. Every entry is (0, 0)
  // when pl carries no "P" (direct construction bypassing
  // GMANRayObjectManager::getRSPolygon).
  std::vector<std::pair<RtFloat, RtFloat>> texCoords;
};
