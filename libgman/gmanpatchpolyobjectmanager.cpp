/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.


  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanobjectmanager.h" /* Super class */
#include "gmanpatchpolyobjectmanager.h" /* Declaration Header */
#include "gmanprimitives.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanloadableshader.h"
#include "gmanlightsourcemgr.h"

namespace {

// The RISpec's own default: a scene that never calls RiSurface still
// shades, as matte. One instance, loaded on first use and reused --
// dlopen once, not once per primitive.
GMANSurfaceShader *defaultSurfaceShader() {
  static GMANLoadableShader loader("libmatte.so");
  static GMANSurfaceShader *shader = loader.getSurface();
  return shader;
}

}  // namespace


/*
 * RenderMan API GMANPatchPolyObjectManager
 *
 */

// default constructor
GMANPatchPolyObjectManager::GMANPatchPolyObjectManager() : GMANObjectManager() { };


// default destructor 
GMANPatchPolyObjectManager::~GMANPatchPolyObjectManager() { };


GMANPrimitive* GMANPatchPolyObjectManager::create(RtVoid) {
  return new GMANObject();
}

GMANPrimitive * GMANPatchPolyObjectManager::getRSPolygon (RtInt /*nverts*/, 
							  GMANParameterList /*pl*/,
							  GMANOptions */*opt*/,
							  GMANAttributes */*attr*/,
							  GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSGeneralPolygon (RtInt /*nloops*/, 
								 RtInt /*nverts*/[], 
								 GMANParameterList /*pl*/,
								 GMANOptions */*opt*/,
								 GMANAttributes */*attr*/,
								 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsPolygon (RtInt /*npolys*/, 
								RtInt /*nverts*/[], 
								RtInt /*verts*/[],
								GMANParameterList /*pl*/,
								GMANOptions */*opt*/,
								GMANAttributes */*attr*/,
								GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsGeneralPolygons (RtInt /*npolys*/, 
									RtInt /*nloops*/[],
									RtInt /*nverts*/[], 
									RtInt /*verts*/[],
									GMANParameterList /*pl*/,
									GMANOptions */*opt*/,
									GMANAttributes */*attr*/,
									GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPatch (RtToken /*type*/, 
							GMANParameterList /*pl*/,
							GMANOptions */*opt*/,
							GMANAttributes */*attr*/,
							GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPatchMesh (RtToken /*type*/, 
							    RtInt /*nu*/, 
							    RtToken /*uwrap*/,
							    RtInt /*nv*/, 
							    RtToken /*vwrap*/, 
							    GMANParameterList /*pl*/,
							    GMANOptions */*opt*/,
							    GMANAttributes */*attr*/,
							    GMANTransform */*t*/)
 {
  return create();
}; 

GMANPrimitive * GMANPatchPolyObjectManager::getRSNuPatch (RtInt /*nu*/,
							  RtInt /*uorder*/,
							  RtFloat /*uknot*/[],
							  RtFloat /*umin*/,
							  RtFloat /*umax*/,
							  RtInt /*nv*/,
							  RtInt /*vorder*/,
							  RtFloat /*vknot*/[],
							  RtFloat /*vmin*/,
							  RtFloat /*vmax*/,
							  GMANParameterList /*pl*/,
							  GMANOptions */*opt*/,
							  GMANAttributes */*attr*/,
							  GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSSphere (RtFloat radius,
							 RtFloat zmin,
							 RtFloat zmax,
							 RtFloat tmax,
							 GMANParameterList pl,
							 GMANOptions */*opt*/,
							 GMANAttributes *attr,
							 GMANTransform *t)
 {
  GMANSphere sphere(radius, zmin, zmax, tmax, pl);
  return createParametric(&sphere, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCone (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl, 
						       GMANOptions */*opt*/,
						       GMANAttributes *attr,
						       GMANTransform *t)
 {
  GMANCone cone(height, radius, tmax, pl);
  return createParametric(&cone, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCylinder (RtFloat radius,
							   RtFloat zmin,
							   RtFloat zmax,
							   RtFloat tmax,
							   GMANParameterList pl,
							   GMANOptions */*opt*/,
							   GMANAttributes *attr,
							   GMANTransform *t)
 {
  GMANCylinder cylinder(radius, zmin, zmax, tmax, pl);
  return createParametric(&cylinder, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSHyperboloid (RtPoint point1,
							      RtPoint point2,
							      RtFloat tmax,
							      GMANParameterList pl,
							      GMANOptions */*opt*/,
							      GMANAttributes *attr,
							      GMANTransform *t)
 {
  GMANHyperboloid hyperboloid(point1, point2, tmax, pl);
  return createParametric(&hyperboloid, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSParaboloid (RtFloat rmax,
							     RtFloat zmin,
							     RtFloat zmax,
							     RtFloat tmax,
							     GMANParameterList pl,
							     GMANOptions */*opt*/,
							     GMANAttributes *attr,
							     GMANTransform *t)
 {
  GMANParaboloid paraboloid(rmax, zmin, zmax, tmax, pl);
  return createParametric(&paraboloid, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSDisk (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl,
						       GMANOptions */*opt*/,
						       GMANAttributes *attr,
						       GMANTransform *t)
 {
  GMANDisk disk(height, radius, tmax, pl);
  return createParametric(&disk, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSTorus (RtFloat majrad,
							RtFloat minrad,
							RtFloat phimin,
							RtFloat phimax,
							RtFloat tmax,
							GMANParameterList pl,
							GMANOptions */*opt*/,
							GMANAttributes *attr,
							GMANTransform *t)
 {
  GMANTorus torus(majrad, minrad, phimin, phimax, tmax, pl);
  return createParametric(&torus, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSBlobby (RtInt /*nleaf*/,
							 RtInt /*ncode*/,
							 RtInt /*code*/[],
							 RtInt /*nflt*/,
							 RtFloat /*flt*/[],
							 RtInt /*nstr*/,
							 RtToken /*str*/[], 
							 GMANParameterList /*pl*/,
							 GMANOptions */*opt*/,
							 GMANAttributes */*attr*/,
							 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPoints (RtInt /*npoints*/,
							 GMANParameterList /*pl*/,
							 GMANOptions */*opt*/,
							 GMANAttributes */*attr*/,
							 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCurves (RtToken /*type*/,
							 RtInt /*ncurves*/, 
							 RtInt /*nvertices*/[],
							 RtToken /*wrap*/,
							 GMANParameterList /*pl*/,
							 GMANOptions */*opt*/,
							 GMANAttributes */*attr*/,
							 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSSubdivisionMesh (RtToken /*mask*/,
								  RtInt /*nf*/,
								  RtInt /*nverts*/[],
								  RtInt /*verts*/[],
								  RtInt /*ntags*/,
								  RtToken /*tags*/[],
								  RtInt /*numargs*/[],
								  RtInt /*intargs*/[],
								  RtFloat /*floatargs*/[],
								  GMANParameterList /*pl*/,
								  GMANOptions */*opt*/,
								  GMANAttributes */*attr*/,
								  GMANTransform */*t*/)
 {
  return create();
};


GMANObject* GMANPatchPolyObjectManager::createParametric (GMANParametric* p,
							  GMANTransform* t,
							  GMANAttributes* attr)
{
#define URES 16
#define VRES 16
  int i, j;
  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();

  // The vertex shading normal is object-space (p->getNormal), unlike the
  // face's geometric normal below, which gets to camera space for free as
  // a side effect of crossing already-transformed edges. A plain normal
  // does not get that gift: it needs the CTM's inverse transpose, computed
  // once per primitive rather than once per vertex. Row-vector convention
  // (p*M, translation in row 3) makes the inverse-transpose of M's linear
  // part exactly Minv's own upper-left 3x3 block used as n*Minv -- see
  // AGENTS.md's "Matrix convention" note and phase-3-REPORT.md.
  GMANMatrix4 ctmInv = t->interpolate(0.0);
  ctmInv.invert();

  // Shading setup, resolved once per primitive rather than once per
  // vertex: the surface shader (falling back to matte, per the RISpec's
  // own default, when RiSurface was never called) and the lights active
  // in this attribute scope (RiIlluminate), handle-to-object resolved
  // through the one light manager this render has. getSurface's const
  // pointer just reflects that GMANAttributes doesn't want its shader
  // pointer reseated through it; computeCi/computeOi are not logically
  // const on the shader instance itself, which is why this casts rather
  // than threading const through the shading call below.
  const GMANSurfaceShader *constShader = attr->getSurface(0.0);
  GMANSurfaceShader *shader = constShader
      ? const_cast<GMANSurfaceShader *>(constShader)
      : defaultSurfaceShader();

  std::vector<const GMANLight *> activeLights;
  const std::list<RtLightHandle> &handles = attr->getLightList().getHandles();
  for (std::list<RtLightHandle>::const_iterator it = handles.begin();
       it != handles.end(); ++it) {
    const GMANLight *light = gmanLightSourceMgr().get(*it);
    if (light) {
      activeLights.push_back(light);
    }
  }

  GMANColor surfaceCs = attr->getColor();
  GMANColor surfaceOs = attr->getOpacity();

  GMANVertex** vertices = new GMANVertex*[(URES + 1) * (VRES + 1)];
  GMANFace** faces = new GMANFace*[URES * VRES];
  GMANBody* body = new GMANBody(GMANColor(), GMANColor());
  GMANSurface* surface = new GMANSurface(body);
  body->setSurface(surface);

  for (i = 0; i < (URES + 1) * (VRES + 1); i++)
  {
    vertices[i] = new GMANVertex();
  }

  for (i = 0; i <= URES; i++)
  {
    for (j = 0; j <= VRES; j++)
    {
      // Create a vertex
      double u = i / (double) URES;
      double v = j / (double) VRES;
      GMANPoint location = t->apply(p->getLocation(u, v));
      GMANVector objectNormal = p->getNormal(u, v);
      GMANVector normal(ctmInv[0][0] * objectNormal.getX() +
			 ctmInv[0][1] * objectNormal.getY() +
			 ctmInv[0][2] * objectNormal.getZ(),
			 ctmInv[1][0] * objectNormal.getX() +
			 ctmInv[1][1] * objectNormal.getY() +
			 ctmInv[1][2] * objectNormal.getZ(),
			 ctmInv[2][0] * objectNormal.getX() +
			 ctmInv[2][1] * objectNormal.getY() +
			 ctmInv[2][2] * objectNormal.getZ());
      normal.normalize();
      GMANVertex* vertex = vertices[(URES + 1) * i + j];
      vertex->setLocation(location);
      vertex->setNormal(normal);

      // Shade this vertex now, in camera space, with every input the
      // shader needs already at hand -- this is what "shade per vertex
      // and let [Gouraud interpolation] interpolate" (SPEC.md) means in
      // practice: a clip-introduced vertex has no u,v of its own to shade
      // with, but it does get a color, because GMANClipEdge::intersect
      // already interpolates GMANVertex::color across a clipped edge (the
      // same machinery phase 1 wired up for the vertex alpha blend).
      GMANSurfaceEnv env;
      env.Cs = surfaceCs;
      env.Os = surfaceOs;
      env.P = location;
      env.N = GMANNormal(normal.getX(), normal.getY(), normal.getZ());
      env.Ng = env.N;  // no displacement this phase; the two never diverge
      // Eye sits at the camera-space origin (GMANVSPerspective::ray), so
      // the incident direction is just the normalized surface point.
      env.I = GMANVector(location.getX(), location.getY(), location.getZ());
      env.I.normalize();
      env.E = GMANPoint(0.0, 0.0, 0.0);
      env.u = (RtFloat) u;
      env.v = (RtFloat) v;
      env.s = (RtFloat) u;
      env.t = (RtFloat) v;
      env.lights = activeLights;

      vertex->setColor(shader->computeCi(env));
    }
  }

  // A second pass, now that every vertex in the grid has a finalized
  // location: face(i,j) touches vertices (i,j+1) and (i+1,*), which are
  // not visited yet when (i,j) is, so calcNormal() run in the same pass
  // as vertex creation would cross-product against up to three
  // still-default-constructed (0,0,0) vertices -- a real, if invisible,
  // pre-existing bug. Invisible because nothing before this phase used
  // the resulting near-zero, direction-free normal for anything: the
  // renderer's own rasterization always reads vertex positions fresh at
  // render time, long after this function returns, so geometry was never
  // affected -- only RiSides 1 culling, which silently culled and kept
  // faces close to at random. See phase-3-REPORT.md.
  for (i = 0; i < URES; i++)
  {
    for (j = 0; j < VRES; j++)
    {
      GMANVertex* faceVertices[4];
      faceVertices[0] = vertices[(URES + 1) * i + j];
      faceVertices[1] = vertices[(URES + 1) * i + (j + 1)];
      faceVertices[2] = vertices[(URES + 1) * (i + 1) + (j + 1)];
      faceVertices[3] = vertices[(URES + 1) * (i + 1) + j];
      faces[URES * i + j] = new GMANFace(faceVertices, surface);
      // Geometric normal, computed from the already-transformed (camera
      // space) vertices: cross(e1', e2') for e'=e*M is proportional to
      // (e1 x e2) transformed by M's inverse transpose, so this needs no
      // separate normal transform. RiSides/RiOrientation travel with the
      // face so visible() can answer without depending on renderer-global
      // state that may differ across attribute blocks.
      faces[URES * i + j]->calcNormal();
      faces[URES * i + j]->setSides(sides);
      faces[URES * i + j]->setOrientation(orientation);
    }
  }

  for (i = 0; i < (URES + 1) * (VRES + 1) - 1; i++)
  {
    vertices[i]->setNext(vertices[i + 1]);
  }
  for (i = 0; i < URES * VRES - 1; i++)
  {
    faces[i]->setNext(faces[i + 1]);
  }
  surface->setFace(faces[0]);

  GMANObject* object = (GMANObject*) create();
  object->setVert(vertices[0]);
  object->setBody(body);

  delete[] vertices;
  delete[] faces;

  return object;
}
