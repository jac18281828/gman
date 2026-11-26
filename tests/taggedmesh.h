/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * Shared across the tests that make a polygon mesh an area light: a mesh
 * built as getRSPolygonMesh builds one, with its appearance on the mesh and on
 * every face, a one-mesh world tagged as one area light, and the rectangles
 * the tests draw from.
 */

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanemitter.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "ri.h"

namespace taggedmesh {

// One face of a tagged mesh: its outer loop, the loops cut out of it and its
// per-vertex normals, empty for a face with none.
struct FaceSpec {
  std::vector<GMANPoint> outer;
  std::vector<std::vector<GMANPoint>> holes;
  std::vector<GMANVector> vertexNormals;
};

// A face with neither holes nor vertex normals.
inline FaceSpec solidFace(std::vector<GMANPoint> outer) { return FaceSpec{std::move(outer), {}, {}}; }

// The solid face of each of loops.
inline std::vector<FaceSpec> solidFaces(std::vector<std::vector<GMANPoint>> const& loops) {
  std::vector<FaceSpec> specs;
  for (std::vector<GMANPoint> const& loop : loops) {
    specs.push_back(solidFace(loop));
  }
  return specs;
}

// The appearance of an area light and nothing else.
inline gman::Appearance areaLightAppearance(GMANLight const& light) {
  gman::Appearance appearance;
  appearance.areaLight = &light;
  return appearance;
}

// A mesh of specs' faces with appearance on the mesh and on each face, as
// getRSPolygonMesh builds one. The caller owns the mesh, which owns its faces.
inline std::unique_ptr<GMANRayPolygonMesh> buildTaggedMesh(std::vector<FaceSpec> const& specs,
                                                           gman::Appearance const& appearance) {
  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  for (FaceSpec const& spec : specs) {
    std::vector<std::pair<RtFloat, RtFloat>> const texCoords(spec.outer.size(),
                                                             std::pair<RtFloat, RtFloat>(0.0f, 0.0f));
    auto face = std::make_unique<GMANRayPolygon>(spec.outer, spec.holes, texCoords, spec.vertexNormals);
    face->setAppearance(appearance);
    faces.push_back(std::move(face));
  }
  auto mesh = std::make_unique<GMANRayPolygonMesh>(std::move(faces));
  mesh->setAppearance(appearance);
  return mesh;
}

// A one-mesh world tagged as one area light of the given radiance. The world
// owns the mesh, which owns its faces; the light outlives neither.
struct MeshWorld {
  GMANLight light;
  GMANLinearWorldManager world;
  GMANRayPolygonMesh const* mesh = nullptr;

  explicit MeshWorld(std::vector<FaceSpec> const& specs, GMANColor const& radiance = GMANColor(4.0f, 4.0f, 4.0f))
      : light(GMAN_LIGHT_AREA, radiance, GMANPoint(), GMANVector()) {
    auto owned = buildTaggedMesh(specs, areaLightAppearance(light));
    mesh = owned.get();
    world.add(owned.release());
  }
};

// The one emitter gman::emitters enumerates for w; empty, and a failed check,
// when it enumerates any other count.
inline std::optional<gman::Emitter> soleEmitter(MeshWorld& w, std::string const& label) {
  std::vector<gman::Emitter> const list = gman::emitters(w.world);
  check(list.size() == 1 && list[0].shape == w.mesh && list[0].light == &w.light,
        label + ": the mesh is one area emitter naming its own light");
  if (list.size() != 1) {
    return std::nullopt;
  }
  return list[0];
}

// The rectangle [x0, x1] x [z0, z1] at height y, wound (x0, y, z0) (x0, y, z1)
// (x1, y, z1) (x1, y, z0), so its plane normal is +y.
inline std::vector<GMANPoint> upRectangle(RtFloat x0, RtFloat x1, RtFloat z0, RtFloat z1, RtFloat y) {
  return {GMANPoint(x0, y, z0), GMANPoint(x0, y, z1), GMANPoint(x1, y, z1), GMANPoint(x1, y, z0)};
}

// The square of half-width half at height y, wound (-half, y, -half) (-half,
// y, half) (half, y, half) (half, y, -half): its plane normal is +y.
inline std::vector<GMANPoint> squareLoop(RtFloat half, RtFloat y) { return upRectangle(-half, half, -half, half, y); }

} // namespace taggedmesh
