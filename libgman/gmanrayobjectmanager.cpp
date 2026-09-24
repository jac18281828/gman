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
#include <utility>
#include <vector>

#include "gmanobjectmanager.h"
#include "gmanpolygon.h"
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

// One Points*/PointsGeneralPolygons face: loopVerts[0] the outer boundary,
// loopVerts[1..] holes, each a run of indices into the shared point pool
// "verts" carries. Rejects a degenerate outer loop exactly as the
// z-buffer's buildFace does; a hole needs no texCoords, so only the outer
// loop's are gathered.
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

GMANPrimitive* GMANRayObjectManager::getRSPolygon(RtInt nverts, GMANParameterList pl, GMANOptions* /*opt*/,
                                                  GMANAttributes* attr, GMANTransform* t) {
  // nverts < 3 is degenerate input, and a "P" absent from this attribute
  // scope's parameter list is malformed RiPolygon/RiPolygonV input (public
  // API, callable with no "P" at all): both degrade to the bare stub every
  // other malformed shape here falls back to, the same guard
  // GMANPatchPolyObjectManager::getRSPolygon applies.
  if (nverts < 3) {
    return create();
  }
  RtFloat* p = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_P));
  if (!p) {
    return create();
  }

  // Captured in camera space now, through t (the CTM RiPolygonV's own
  // transform carries): a polygon is flat, so this is its whole geometry
  // and no per-ray matrix work is needed later.
  std::vector<GMANPoint> vertices(nverts);
  for (RtInt i = 0; i < nverts; i++) {
    vertices[i] = t->apply(GMANPoint(p[3 * i], p[3 * i + 1], p[3 * i + 2]));
  }

  GMANRayPolygon* polygon = new GMANRayPolygon(std::move(vertices), pl);
  polygon->setAppearance(gman::appearanceOf(*attr));
  return polygon;
};

GMANPrimitive* GMANRayObjectManager::getRSGeneralPolygon(RtInt nloops, RtInt nverts[], GMANParameterList pl,
                                                         GMANOptions* /*opt*/, GMANAttributes* attr, GMANTransform* t) {
  // nloops < 1 and a missing "P" both degrade to the same empty stub the
  // z-buffer's own getRSGeneralPolygon falls back to.
  if (nloops < 1) {
    return create();
  }
  RtFloat* p = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_P));
  if (!p) {
    return create();
  }

  // Loop 0 is the outer boundary; every later loop a hole (RISpec's
  // GeneralPolygon). "P" is flat across every loop in that same order.
  std::vector<std::vector<GMANPoint>> loops(nloops);
  RtInt offset = 0;
  for (RtInt i = 0; i < nloops; i++) {
    RtInt const count = nverts[i] > 0 ? nverts[i] : 0;
    loops[i].resize(count);
    for (RtInt j = 0; j < count; j++) {
      loops[i][j] = t->apply(GMANPoint(p[3 * (offset + j)], p[3 * (offset + j) + 1], p[3 * (offset + j) + 2]));
    }
    offset += count;
  }

  // A degenerate outer loop draws nothing, the same rejection the
  // z-buffer's buildFace applies.
  if (gman::isDegeneratePolygon(loops[0])) {
    return create();
  }
  std::vector<GMANPoint> outer = std::move(loops[0]);
  std::vector<std::vector<GMANPoint>> holes(std::make_move_iterator(loops.begin() + 1),
                                            std::make_move_iterator(loops.end()));

  GMANRayPolygon* polygon = new GMANRayPolygon(std::move(outer), std::move(holes), pl);
  polygon->setAppearance(gman::appearanceOf(*attr));
  return polygon;
};

GMANPrimitive* GMANRayObjectManager::getRSPointsPolygon(RtInt npolys, RtInt nverts[], RtInt verts[],
                                                        GMANParameterList pl, GMANOptions* /*opt*/,
                                                        GMANAttributes* attr, GMANTransform* t) {
  // Direct, white-box caller guard, as getRSGeneralPolygon's own
  // nloops < 1 guard is -- RiPointsPolygonsV rejects npolys < 0 before this
  // ever runs, and npolys == 0 draws nothing either way.
  if (npolys < 1) {
    return create();
  }
  RtFloat* p = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_P));
  if (!p) {
    return create();
  }

  RtInt totalVerts = 0;
  for (RtInt i = 0; i < npolys; i++) {
    totalVerts += nverts[i] > 0 ? nverts[i] : 0;
  }
  // 1 + max(verts): RiSpec's own vertex/varying count for this request --
  // one "P"/"s"/"t"/"st" entry per point the mesh actually references.
  RtInt pointCount = 0;
  for (RtInt i = 0; i < totalVerts; i++) {
    if (verts[i] + 1 > pointCount) {
      pointCount = verts[i] + 1;
    }
  }
  // Resolved once over the whole shared point pool, not per face, so a
  // point three faces reference still reads the same values wherever a
  // face gathers it from.
  std::vector<std::pair<RtFloat, RtFloat>> pointTexCoords = gman::resolvePointTexCoords(pl, (std::size_t)pointCount);
  gman::Appearance const appearance = gman::appearanceOf(*attr);

  // Faceted: every face gathers its own vertices through "verts", one
  // PointsPolygons face being a one-loop GeneralPolygon. A degenerate face
  // is skipped, not fatal; every surviving face joins one mesh primitive,
  // so the request still adds exactly one primitive to the world.
  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  RtInt offset = 0;
  for (RtInt i = 0; i < npolys; i++) {
    RtInt const count = nverts[i] > 0 ? nverts[i] : 0;
    std::vector<RtInt> const faceVerts(verts + offset, verts + offset + count);
    offset += count;

    std::unique_ptr<GMANRayPolygon> face;
    if (buildMeshFace({faceVerts}, p, pointTexCoords, t, face)) {
      face->setAppearance(appearance);
      faces.push_back(std::move(face));
    }
  }

  if (faces.empty()) {
    return create(); // no face survived: the whole mesh is the empty stub
  }
  GMANRayPolygonMesh* mesh = new GMANRayPolygonMesh(std::move(faces));
  mesh->setAppearance(appearance);
  return mesh;
};

GMANPrimitive* GMANRayObjectManager::getRSPointsGeneralPolygons(RtInt npolys, RtInt nloops[], RtInt nverts[],
                                                                RtInt verts[], GMANParameterList pl,
                                                                GMANOptions* /*opt*/, GMANAttributes* attr,
                                                                GMANTransform* t) {
  if (npolys < 1) {
    return create();
  }
  RtFloat* p = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_P));
  if (!p) {
    return create();
  }

  RtInt sumNloops = 0;
  for (RtInt i = 0; i < npolys; i++) {
    sumNloops += nloops[i] > 0 ? nloops[i] : 0;
  }
  RtInt totalVerts = 0;
  for (RtInt i = 0; i < sumNloops; i++) {
    totalVerts += nverts[i] > 0 ? nverts[i] : 0;
  }
  RtInt pointCount = 0;
  for (RtInt i = 0; i < totalVerts; i++) {
    if (verts[i] + 1 > pointCount) {
      pointCount = verts[i] + 1;
    }
  }
  // Resolved once over the whole shared point pool, not per face, so a
  // point three faces reference still reads the same values wherever a
  // face gathers it from.
  std::vector<std::pair<RtFloat, RtFloat>> pointTexCoords = gman::resolvePointTexCoords(pl, (std::size_t)pointCount);
  gman::Appearance const appearance = gman::appearanceOf(*attr);

  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  RtInt loopOffset = 0, vertOffset = 0;
  for (RtInt i = 0; i < npolys; i++) {
    RtInt const faceLoops = nloops[i] > 0 ? nloops[i] : 0;
    if (faceLoops == 0) {
      continue; // no outer loop at all: degenerate, skip
    }

    std::vector<std::vector<RtInt>> loopVerts(faceLoops);
    for (RtInt li = 0; li < faceLoops; li++) {
      RtInt const count = nverts[loopOffset + li] > 0 ? nverts[loopOffset + li] : 0;
      loopVerts[li].assign(verts + vertOffset, verts + vertOffset + count);
      vertOffset += count;
    }
    loopOffset += faceLoops;

    std::unique_ptr<GMANRayPolygon> face;
    if (buildMeshFace(loopVerts, p, pointTexCoords, t, face)) {
      face->setAppearance(appearance);
      faces.push_back(std::move(face));
    }
  }

  if (faces.empty()) {
    return create();
  }
  GMANRayPolygonMesh* mesh = new GMANRayPolygonMesh(std::move(faces));
  mesh->setAppearance(appearance);
  return mesh;
};

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
