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

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanobjectmanager.h" /* Super class */
#include "gmanpatchpolyobjectmanager.h" /* Declaration Header */
#include "gmanprimitives.h"


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

GMANPrimitive * GMANPatchPolyObjectManager::getRSPolygon (RtInt nverts, 
							  GMANParameterList pl,
							  GMANOptions *opt,
							  GMANAttributes *attr,
							  GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSGeneralPolygon (RtInt nloops, 
								 RtInt nverts[], 
								 GMANParameterList pl,
								 GMANOptions *opt,
								 GMANAttributes *attr,
								 GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsPolygon (RtInt npolys, 
								RtInt nverts[], 
								RtInt verts[],
								GMANParameterList pl,
								GMANOptions *opt,
								GMANAttributes *attr,
								GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsGeneralPolygons (RtInt npolys, 
									RtInt nloops[],
									RtInt nverts[], 
									RtInt verts[],
									GMANParameterList pl,
									GMANOptions *opt,
									GMANAttributes *attr,
									GMANTransform *t)
  throw(GMANError) {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPatch (RtToken type, 
							GMANParameterList pl,
							GMANOptions *opt,
							GMANAttributes *attr,
							GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSPatchMesh (RtToken type, 
							    RtInt nu, 
							    RtToken uwrap,
							    RtInt nv, 
							    RtToken vwrap, 
							    GMANParameterList pl,
							    GMANOptions *opt,
							    GMANAttributes *attr,
							    GMANTransform *t)
  throw(GMANError) {
  return create();
}; 
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSNuPatch (RtInt nu,
							  RtInt uorder,
							  RtFloat uknot[],
							  RtFloat umin,
							  RtFloat umax,
							  RtInt nv,
							  RtInt vorder,
							  RtFloat vknot[],
							  RtFloat vmin,
							  RtFloat vmax,
							  GMANParameterList pl,
							  GMANOptions *opt,
							  GMANAttributes *attr,
							  GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSSphere (RtFloat radius,
							 RtFloat zmin,
							 RtFloat zmax,
							 RtFloat tmax,
							 GMANParameterList pl,
							 GMANOptions *opt,
							 GMANAttributes *attr,
							 GMANTransform *t)
  throw(GMANError) {
  GMANSphere sphere(radius, zmin, zmax, tmax, pl);
  return createParametric(&sphere, t);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCone (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl, 
						       GMANOptions *opt,
						       GMANAttributes *attr,
						       GMANTransform *t)
  throw(GMANError) {
  GMANCone cone(height, radius, tmax, pl);
  return createParametric(&cone, t);
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSCylinder (RtFloat radius,
							   RtFloat zmin,
							   RtFloat zmax,
							   RtFloat tmax,
							   GMANParameterList pl,
							   GMANOptions *opt,
							   GMANAttributes *attr,
							   GMANTransform *t)
  throw(GMANError) {
  GMANCylinder cylinder(radius, zmin, zmax, tmax, pl);
  return createParametric(&cylinder, t);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSHyperboloid (RtPoint point1,
							      RtPoint point2,
							      RtFloat tmax,
							      GMANParameterList pl,
							      GMANOptions *opt,
							      GMANAttributes *attr,
							      GMANTransform *t)
  throw(GMANError) {
  GMANHyperboloid hyperboloid(point1, point2, tmax, pl);
  return createParametric(&hyperboloid, t);
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSParaboloid (RtFloat rmax,
							     RtFloat zmin,
							     RtFloat zmax,
							     RtFloat tmax,
							     GMANParameterList pl,
							     GMANOptions *opt,
							     GMANAttributes *attr,
							     GMANTransform *t)
  throw(GMANError) {
  GMANParaboloid paraboloid(rmax, zmin, zmax, tmax, pl);
  return createParametric(&paraboloid, t);
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSDisk (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl,
						       GMANOptions *opt,
						       GMANAttributes *attr,
						       GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSTorus (RtFloat majrad,
							RtFloat minrad,
							RtFloat phimin,
							RtFloat phimax,
							RtFloat tmax,
							GMANParameterList pl,
							GMANOptions *opt,
							GMANAttributes *attr,
							GMANTransform *t)
  throw(GMANError) {
  GMANTorus torus(majrad, minrad, phimin, phimax, tmax, pl);
  return createParametric(&torus, t);
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSBlobby (RtInt nleaf,
							 RtInt ncode,
							 RtInt code[],
							 RtInt nflt,
							 RtFloat flt[],
							 RtInt nstr,
							 RtToken str[], 
							 GMANParameterList pl,
							 GMANOptions *opt,
							 GMANAttributes *attr,
							 GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSPoints (RtInt npoints,
							 GMANParameterList pl,
							 GMANOptions *opt,
							 GMANAttributes *attr,
							 GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSCurves (RtToken type,
							 RtInt ncurves, 
							 RtInt nvertices[],
							 RtToken wrap,
							 GMANParameterList pl,
							 GMANOptions *opt,
							 GMANAttributes *attr,
							 GMANTransform *t)
  throw(GMANError) {
  return create();
};
  
GMANPrimitive * GMANPatchPolyObjectManager::getRSSubdivisionMesh (RtToken mask,
								  RtInt nf,
								  RtInt nverts[],
								  RtInt verts[],
								  RtInt ntags,
								  RtToken tags[],
								  RtInt numargs[],
								  RtInt intargs[],
								  RtFloat floatargs[],
								  GMANParameterList pl,
								  GMANOptions *opt,
								  GMANAttributes *attr,
								  GMANTransform *t)
  throw(GMANError) {
  return create();
};


GMANObject* GMANPatchPolyObjectManager::createParametric (GMANParametric* p,
							  GMANTransform* t)
{
#define URES 16
#define VRES 16
  int i, j;

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
      //GMANVector normal = p->getNormal(u, v);
      GMANVertex* vertex = vertices[(URES + 1) * i + j];
      vertex->setLocation(location);
      //vertex->setNormal(normal);

      if (i < URES && j < VRES) {
	// Create a face
	GMANVertex* faceVertices[4];
	faceVertices[0] = vertices[(URES + 1) * i + j];
	faceVertices[1] = vertices[(URES + 1) * i + (j + 1)];
	faceVertices[2] = vertices[(URES + 1) * (i + 1) + (j + 1)];
	faceVertices[3] = vertices[(URES + 1) * (i + 1) + j];
	faces[URES * i + j] = new GMANFace(faceVertices, surface);
      }
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

  delete vertices;
  delete faces;

  return object;
}
