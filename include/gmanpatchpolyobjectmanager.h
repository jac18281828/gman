/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns 
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
 

#ifndef __GMAN_GMANPATCHPOLYOBJECTMANAGER_H
#define __GMAN_GMANPATCHPOLYOBJECTMANAGER_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// Our parent class
#include "gmanobjectmanager.h"

/*
 * RenderMan API GMANPatchPolyObjectManager
 *
 */

class GMANDLL  GMANPatchPolyObjectManager : public GMANObjectManager {
public:
  GMANPatchPolyObjectManager(); // default constructor

  ~GMANPatchPolyObjectManager(); // default destructor


  virtual GMANPrimitive* create(RtVoid);


  /* 
   * Renderer Specific primitives
   *
   * Each must provide its own object manager, and each
   * object manager must provide its own primitives.
   *
   */
  virtual GMANPrimitive * getRSPolygon (RtInt nverts, 
					GMANParameterList pl,
					GMANOptions *opt,
					GMANAttributes *attr,
					GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSGeneralPolygon (RtInt nloops, 
					       RtInt nverts[], 
					       GMANParameterList pl,
					       GMANOptions *opt,
					       GMANAttributes *attr,
					       GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSPointsPolygon (RtInt npolys, 
					      RtInt nverts[], 
					      RtInt verts[],
					      GMANParameterList pl,
					      GMANOptions *opt,
					      GMANAttributes *attr,
					      GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSPointsGeneralPolygons (RtInt npolys, 
						      RtInt nloops[],
						      RtInt nverts[], 
						      RtInt verts[],
						      GMANParameterList pl,
						      GMANOptions *opt,
						      GMANAttributes *attr,
						      GMANTransform *t)
    throw(GMANError);

  virtual GMANPrimitive * getRSPatch (RtToken type, 
				      GMANParameterList pl,
				      GMANOptions *opt,
				      GMANAttributes *attr,
				      GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSPatchMesh (RtToken type, 
					  RtInt nu, 
					  RtToken uwrap,
					  RtInt nv, 
					  RtToken vwrap, 
					  GMANParameterList pl,
					  GMANOptions *opt,
					  GMANAttributes *attr,
					  GMANTransform *t)
    throw(GMANError); 
  
  virtual GMANPrimitive * getRSNuPatch (RtInt nu,
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
    throw(GMANError);
  
  virtual GMANPrimitive * getRSSphere (RtFloat radius,
				       RtFloat zmin,
				       RtFloat zmax,
				       RtFloat tmax,
				       GMANParameterList pl,
				       GMANOptions *opt,
				       GMANAttributes *attr,
				       GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSCone (RtFloat height,
				     RtFloat radius,
				     RtFloat tmax,
				     GMANParameterList pl, 
				     GMANOptions *opt,
				     GMANAttributes *attr,
				     GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSCylinder (RtFloat radius,
					 RtFloat zmin,
					 RtFloat zmax,
					 RtFloat tmax,
					 GMANParameterList pl,
					 GMANOptions *opt,
					 GMANAttributes *attr,
					 GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSHyperboloid (RtPoint point1,
					    RtPoint point2,
					    RtFloat tmax,
					    GMANParameterList pl,
					    GMANOptions *opt,
					    GMANAttributes *attr,
					    GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSParaboloid (RtFloat rmax,
					   RtFloat zmin,
					   RtFloat zmax,
					   RtFloat tmax,
					   GMANParameterList pl,
					   GMANOptions *opt,
					   GMANAttributes *attr,
					   GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSDisk (RtFloat height,
				     RtFloat radius,
				     RtFloat tmax,
				     GMANParameterList pl,
				     GMANOptions *opt,
				     GMANAttributes *attr,
				     GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSTorus (RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
				      RtFloat tmax,
				      GMANParameterList pl,
				      GMANOptions *opt,
				      GMANAttributes *attr,
				      GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSBlobby (RtInt nleaf,
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
    throw(GMANError);
  
  virtual GMANPrimitive * getRSPoints (RtInt npoints,
				       GMANParameterList pl,
				       GMANOptions *opt,
				       GMANAttributes *attr,
				       GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSCurves (RtToken type,
				       RtInt ncurves, 
				       RtInt nvertices[],
				       RtToken wrap,
				       GMANParameterList pl,
				       GMANOptions *opt,
				       GMANAttributes *attr,
				       GMANTransform *t)
    throw(GMANError);
  
  virtual GMANPrimitive * getRSSubdivisionMesh (RtToken mask,
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
    throw(GMANError);

protected:

  GMANObject* createParametric (GMANParametric* p, GMANTransform *t);

};


#endif
