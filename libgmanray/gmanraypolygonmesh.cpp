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

#include "gmanraybbox.h"
#include "gmanraypolygonmesh.h"

GMANRayPolygonMesh::GMANRayPolygonMesh(std::vector<std::unique_ptr<GMANRayPolygon>> polygons)
    : faces(std::move(polygons)) {
  if (faces.empty()) {
    return;
  }
  GMANPoint minP = faces[0]->getBBox().getMin();
  GMANPoint maxP = faces[0]->getBBox().getMax();
  for (std::unique_ptr<GMANRayPolygon> const& face : faces) {
    minP = gman::pointMin(minP, face->getBBox().getMin());
    maxP = gman::pointMax(maxP, face->getBBox().getMax());
  }
  bbox = GMANBBox(minP, maxP);
}

bool GMANRayPolygonMesh::intersect(GMANRay const& ray, GMANHit& hit) const {
  bool found = false;
  for (std::unique_ptr<GMANRayPolygon> const& face : faces) {
    GMANHit candidate;
    if (!face->intersect(ray, candidate)) {
      continue;
    }
    if (!found || candidate.t < hit.t) {
      hit = candidate;
      found = true;
    }
  }
  return found;
}
