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

#include <memory>
#include <vector>

#include "gmanraypolygon.h"

// One PointsPolygons/PointsGeneralPolygons request's own faces, each a
// GMANRayPolygon in camera space, joined under one GMANPrimitive so
// RiPointsPolygonsV/RiPointsGeneralPolygonsV add exactly one primitive to
// the world. GMANRayBVH::build recognizes this type by dynamic_cast and
// adds each face as its own entry instead of the mesh itself, so a
// house-sized mesh culls per face; intersect below tests every face
// linearly, for any caller that reaches this primitive directly rather
// than through the BVH. Internal: no exported declaration names it, and
// it carries no face accessor.
class GMANRayPolygonMesh : public GMANRayInterface {
public:
  explicit GMANRayPolygonMesh(std::vector<std::unique_ptr<GMANRayPolygon>> polygons);

  bool intersect(const GMANRay& ray, GMANHit& hit) const;

private:
  friend class GMANRayBVH; // the only flattener: reads faces directly

  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
};
