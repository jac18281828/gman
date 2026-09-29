/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "gmanobjectmanager.h"
#include "gmanpolygon.h"
#include "gmanpolygoninternal.h"
#include "gmanpolygonmesh.h"
#include "gmanprimitives.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayinterface.h"
#include "gmanrayobjectmanager.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmanshading.h"
#include "ri.h"

namespace {

// One polygon request's face: loopVerts[0] the outer boundary, loopVerts[1..]
// holes, each a run of indices into the mesh's shared point pool. Rejects a
// degenerate outer loop exactly as the z-buffer's buildFace does; a hole
// needs no texCoords, so only the outer loop's are gathered.
bool buildMeshFace(std::vector<std::vector<RtInt>> const& loopVerts, RtFloat const* p,
                   std::vector<std::pair<RtFloat, RtFloat>> const& pointTexCoords, GMANTransform* t,
                   std::unique_ptr<GMANRayPolygon>& outFace) {
  std::vector<std::vector<GMANPoint>> loops(loopVerts.size());
  for (std::size_t li = 0; li < loopVerts.size(); li++) {
    loops[li].resize(loopVerts[li].size());
    for (std::size_t j = 0; j < loopVerts[li].size(); j++) {
      RtInt const pointIndex = loopVerts[li][j];
      loops[li][j] = t->apply(GMANPoint(p[3 * pointIndex], p[3 * pointIndex + 1], p[3 * pointIndex + 2]));
    }
  }
  if (loops.empty() || gman::isDegeneratePolygon(loops[0])) {
    return false;
  }

  std::vector<std::pair<RtFloat, RtFloat>> outerTexCoords(loopVerts[0].size());
  for (std::size_t j = 0; j < loopVerts[0].size(); j++) {
    outerTexCoords[j] = pointTexCoords[loopVerts[0][j]];
  }

  std::vector<GMANPoint> outer = std::move(loops[0]);
  std::vector<std::vector<GMANPoint>> holes(std::make_move_iterator(loops.begin() + 1),
                                            std::make_move_iterator(loops.end()));
  outFace = std::make_unique<GMANRayPolygon>(std::move(outer), std::move(holes), std::move(outerTexCoords));
  return true;
}

} // namespace

/*
 * RenderMan API GMANRayObjectManager
 *
 */

// default constructor
GMANRayObjectManager::GMANRayObjectManager() : GMANObjectManager() {};

// default destructor
GMANRayObjectManager::~GMANRayObjectManager() {};

GMANPrimitive* GMANRayObjectManager::create(RtVoid) { return new GMANRayInterface(); }

// One call for every polygon request, a one-face Polygon included: one
// GMANRayPolygonMesh when a face survives, the empty stub when none does,
// never a bare GMANRayPolygon. GMANRayBVH::build flattens a mesh into the
// same entries a bare polygon gives, so a one-face mesh's box and
// appearance already equal its own face's.
GMANPrimitive* GMANRayObjectManager::getRSPolygonMesh(GMANPolygonMesh const& mesh, GMANOptions* /*opt*/,
                                                      GMANAttributes* attr, GMANTransform* t) {
  std::span<RtFloat const> const p = mesh.points();
  std::size_t const pointCount = p.size() / 3;

  std::vector<std::pair<RtFloat, RtFloat>> pointTexCoords = gman::resolvePointTexCoords(mesh.parameters(), pointCount);
  gman::Appearance const appearance = gman::appearanceOf(*attr);

  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  for (std::size_t i = 0; i < mesh.faceCount(); i++) {
    std::span<RtInt const> const faceLoops = mesh.face(i);
    std::vector<std::vector<RtInt>> loopVerts(faceLoops.size());
    for (std::size_t li = 0; li < faceLoops.size(); li++) {
      std::span<RtInt const> const indices = mesh.loop(i, li);
      loopVerts[li].assign(indices.begin(), indices.end());
    }

    std::unique_ptr<GMANRayPolygon> face;
    if (buildMeshFace(loopVerts, p.data(), pointTexCoords, t, face)) {
      face->setAppearance(appearance);
      faces.push_back(std::move(face));
    }
  }

  if (faces.empty()) {
    return create();
  }
  GMANRayPolygonMesh* rayMesh = new GMANRayPolygonMesh(std::move(faces));
  rayMesh->setAppearance(appearance);
  return rayMesh;
}

GMANPrimitive* GMANRayObjectManager::getRSPatch(RtToken /*type*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSPatchMesh(RtToken /*type*/, RtInt /*nu*/, RtToken /*uwrap*/, RtInt /*nv*/,
                                                    RtToken /*vwrap*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                    GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSNuPatch(RtInt /*nu*/, RtInt /*uorder*/, RtFloat /*uknot*/[], RtFloat /*umin*/,
                                                  RtFloat /*umax*/, RtInt /*nv*/, RtInt /*vorder*/, RtFloat /*vknot*/[],
                                                  RtFloat /*vmin*/, RtFloat /*vmax*/, GMANParameterList /*pl*/,
                                                  GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                  GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                 GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                 GMANTransform* t) {
  GMANRaySphere* sphere = new GMANRaySphere(radius, zmin, zmax, tmax, pl, *t);
  sphere->setAppearance(gman::appearanceOf(*attr));
  return sphere;
};

GMANPrimitive* GMANRayObjectManager::getRSCone(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                               GMANOptions* /*opt*/, GMANAttributes* attr, GMANTransform* t) {
  GMANRayCone* cone = new GMANRayCone(height, radius, tmax, pl, *t);
  cone->setAppearance(gman::appearanceOf(*attr));
  return cone;
};

GMANPrimitive* GMANRayObjectManager::getRSCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                   GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                   GMANTransform* t) {
  GMANRayCylinder* cylinder = new GMANRayCylinder(radius, zmin, zmax, tmax, pl, *t);
  cylinder->setAppearance(gman::appearanceOf(*attr));
  return cylinder;
};

GMANPrimitive* GMANRayObjectManager::getRSHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax,
                                                      GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                      GMANTransform* t) {
  GMANRayHyperboloid* hyperboloid = new GMANRayHyperboloid(point1, point2, tmax, pl, *t);
  hyperboloid->setAppearance(gman::appearanceOf(*attr));
  return hyperboloid;
};

GMANPrimitive* GMANRayObjectManager::getRSParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                     GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                     GMANTransform* t) {
  GMANRayParaboloid* paraboloid = new GMANRayParaboloid(rmax, zmin, zmax, tmax, pl, *t);
  paraboloid->setAppearance(gman::appearanceOf(*attr));
  return paraboloid;
};

GMANPrimitive* GMANRayObjectManager::getRSDisk(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                               GMANOptions* /*opt*/, GMANAttributes* attr, GMANTransform* t) {
  GMANRayDisk* disk = new GMANRayDisk(height, radius, tmax, pl, *t);
  disk->setAppearance(gman::appearanceOf(*attr));
  return disk;
};

GMANPrimitive* GMANRayObjectManager::getRSTorus(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax,
                                                RtFloat tmax, GMANParameterList pl, GMANOptions* /*opt*/,
                                                GMANAttributes* attr, GMANTransform* t) {
  GMANRayTorus* torus = new GMANRayTorus(majrad, minrad, phimin, phimax, tmax, pl, *t);
  torus->setAppearance(gman::appearanceOf(*attr));
  return torus;
};

GMANPrimitive* GMANRayObjectManager::getRSBlobby(RtInt /*nleaf*/, RtInt /*ncode*/, RtInt /*code*/[], RtInt /*nflt*/,
                                                 RtFloat /*flt*/[], RtInt /*nstr*/, RtToken /*str*/[],
                                                 GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                 GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSPoints(RtInt /*npoints*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                 GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSCurves(RtToken /*type*/, RtInt /*ncurves*/, RtInt /*nvertices*/[],
                                                 RtToken /*wrap*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                 GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSSubdivisionMesh(RtToken /*mask*/, RtInt /*nf*/, RtInt /*nverts*/[],
                                                          RtInt /*verts*/[], RtInt /*ntags*/, RtToken /*tags*/[],
                                                          RtInt /*numargs*/[], RtInt /*intargs*/[],
                                                          RtFloat /*floatargs*/[], GMANParameterList /*pl*/,
                                                          GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                          GMANTransform* /*t*/) {
  return create();
};
