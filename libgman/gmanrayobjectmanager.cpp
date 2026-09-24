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
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmanshading.h"
#include "ri.h"

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

GMANPrimitive* GMANRayObjectManager::getRSGeneralPolygon(RtInt /*nloops*/, RtInt /*nverts*/[], GMANParameterList /*pl*/,
                                                         GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                         GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSPointsPolygon(RtInt /*npolys*/, RtInt /*nverts*/[], RtInt /*verts*/[],
                                                        GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                        GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSPointsGeneralPolygons(RtInt /*npolys*/, RtInt /*nloops*/[],
                                                                RtInt /*nverts*/[], RtInt /*verts*/[],
                                                                GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                                GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
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
