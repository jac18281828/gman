/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
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
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/
 

#ifndef __GMAN_RENDERMAN_H
#define __GMAN_RENDERMAN_H 1


/* Headers */

// renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"

// the frame buffer
#include "gmanframebuffer.h"
// the gman object manager
#include "gmanobjectmanager.h"
// the rendering system
#include "gmanrenderer.h"
// the viewing system
#include "gmanviewingsystem.h"
// the output object
#include "gmanoutput.h"
// the token dictionary
#include "gmandictionary.h"
// the graphic state manager
#include "gmangraphicstate.h"



/*
 * RenderMan API RiRenderMan
 *
 * The Renderman object supporting the primary interface from the
 * underlying rendering system to the RenderMan API
 *
 */

// renderman is an instance of the graphics state machine
class GMANDLL  GMANRenderMan : protected GMANGraphicState {
private:
  // the renderer that computes the lighting model and 
  // simulates the environment
  GMANRenderer *                        renderer;

  // the object manager that stores the objects in the environment
  GMANObjectManager *                   objectManager;

  // world manager
  GMANWorldManager *                    worldManager;

  // Viewing system
  GMANViewingSystem *                   viewingSystem;

  // the token dictionary
  GMANDictionary                        dictionary;

  GMANOutput *				output;
public:
  GMANRenderMan(); // default constructor  
  ~GMANRenderMan(); // default destructor


  /* Declare Shading Language variable */
  RtToken RiDeclare(const char *name, const char *declaration);

  /* RenderMan State machine */
  RtVoid	RiBegin(RtToken name);
  RtVoid        RiEnd(RtVoid);

  // begin single frame, number frame
  RtVoid        RiFrameBegin(RtInt frame);
  RtVoid        RiFrameEnd(RtVoid);

  // begin world (declaration of objects)
  RtVoid	RiWorldBegin(RtVoid);
  RtVoid        RiWorldEnd(RtVoid);

  // create a new object 
  RtObjectHandle RiObjectBegin(RtVoid);

  // commit the most recently created object
  RtVoid RiObjectEnd(RtVoid);
  
  RtVoid  RiObjectInstance(RtObjectHandle handle);

  RtVoid  RiAttributeBegin(RtVoid);
  RtVoid  RiAttributeEnd(RtVoid);

  RtVoid  RiTransformBegin(RtVoid);
  RtVoid  RiTransformEnd(RtVoid);

  RtVoid  RiSolidBegin(RtToken operation);
  RtVoid  RiSolidEnd(RtVoid) ;

  RtVoid  RiMotionBeginV(RtInt n, RtFloat times[]);
  RtVoid  RiMotionEnd(RtVoid) ;


  /* CAMERA OPTIONS */
  RtVoid  RiFormat (RtInt xres, RtInt yres, RtFloat aspect);
  RtVoid  RiFrameAspectRatio (RtFloat aspect);
  RtVoid  RiScreenWindow (RtFloat left, RtFloat right, RtFloat bot, RtFloat top);
  RtVoid  RiCropWindow (RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax);
  RtVoid  RiProjectionV (RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiClipping(RtFloat hither, RtFloat yon);
  RtVoid  RiDepthOfField (RtFloat fstop, RtFloat focallength, RtFloat focaldistance);
  RtVoid  RiShutter(RtFloat min, RtFloat max);

  /* DISPLAY OPTIONS */
  RtVoid  RiPixelVariance(RtFloat variation);
  RtVoid  RiPixelSamples(RtFloat xsamples, RtFloat ysamples);
  RtVoid  RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth);
  RtVoid  RiExposure(RtFloat gain, RtFloat gamma);
  RtVoid  RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl);
  RtVoid  RiDisplayV(char *name, RtToken type, RtToken mode,
		     RtInt n, RtToken tokens[], RtPointer parms[]);

  /* ADDITIONAL OPTIONS */
  RtVoid  RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[]);
  RtVoid  RiRelativeDetail(RtFloat relativedetail);
  RtVoid  RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);


  /* SHADING ATTRIBUTES */
  RtVoid  RiColor(RtColor color);
  RtVoid  RiOpacity(RtColor color);
  RtVoid  RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
			       RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4);
  RtLightHandle RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtLightHandle RiAreaLightSourceV(RtToken name,
				   RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiIlluminate(RtLightHandle light, RtBoolean onoff);
  RtVoid  RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiShadingRate(RtFloat size);
  RtVoid  RiShadingInterpolation(RtToken type);
  RtVoid  RiMatte(RtBoolean onoff);

  /* GEOMETRY ATTRIBUTES */
  RtVoid  RiBound(RtBound);
  RtVoid  RiDetail(RtBound);
  RtVoid  RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis);
  RtVoid  RiGeometricApproximation(RtToken type, RtFloat value);
  RtVoid  RiBasis(RtBasis ubasis, RtInt ustep, 
		  RtBasis vbasis, RtInt vstep);
  RtVoid  RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[],
		      RtFloat knot[], RtFloat min[], RtFloat max[], RtInt n[],
		      RtFloat u[], RtFloat v[], RtFloat w[]);
  RtVoid  RiOrientation(RtToken orientation);
  RtVoid  RiReverseOrientation(RtVoid);
  RtVoid  RiSides(RtInt sides);
  RtVoid  RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);

  /* TRANSFORMATIONS */
  RtVoid  RiIdentity(RtVoid);
  RtVoid  RiTransform(RtMatrix transform);
  RtVoid  RiConcatTransform(RtMatrix transform);
  RtVoid  RiPerspective(RtFloat fov);
  RtVoid  RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz);
  RtVoid  RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz);
  RtVoid  RiScale(RtFloat sx, RtFloat sy, RtFloat sz);
  RtVoid  RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
		 RtFloat dx2, RtFloat dy2, RtFloat dz2);
  RtVoid  RiDeformationV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiCoordinateSystem(RtToken space);
  RtVoid  RiCoordSysTransform(RtToken space);

  RtPoint *RiTransformPoints(RtToken fromspace, RtToken tospace, RtInt n,
			     RtPoint points[]);

  RtVoid  RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]);

  /* PRIMITIVES */
  RtVoid  RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n,
			    RtToken tokens[], RtPointer parms[]);
  RtVoid  RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[],
			    RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[],
				  RtInt verts[], RtInt n, RtToken tokens[], 
				  RtPointer parms[]);
  RtVoid  RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap,
		       RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[], 
		       RtPointer parms[]);
  RtVoid  RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin,
		     RtFloat umax, RtInt nv, RtInt vorder, RtFloat vknot[],
		     RtFloat vmin, RtFloat vmax,
		     RtInt n, RtToken tokens[], RtPointer parms[]);
  
  RtVoid  RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
		    RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
		  RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
		      RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
			 RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
			RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
		  RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiTorusV(RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
		   RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[]);
  
  RtVoid  RiBlobbyV(RtInt nleaf, RtInt ncode, RtInt code[],
		    RtInt nflt, RtFloat flt[],
		    RtInt nstr, RtToken str[], 
		    RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiPointsV(RtInt npoints,
		    RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiCurvesV(RtToken type, RtInt ncurves,
		    RtInt nvertices[], RtToken wrap,
		    RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiSubdivisionMeshV(RtToken mask, RtInt nf, RtInt nverts[],
			     RtInt verts[],
			     RtInt ntags, RtToken tags[], RtInt numargs[],
			     RtInt intargs[], RtFloat floatargs[],
			     RtInt n, RtToken tokens[], RtPointer parms[]);


  RtVoid  RiProcedural(RtPointer data, RtBound bound,
		       RtVoid (*subdivfunc)(RtPointer, RtFloat),
		       RtVoid (*freefunc)(RtPointer));
  RtVoid  RiGeometryV(RtToken type, RtInt n, RtToken tokens[], 
		      RtPointer parms[]);

  /* MISC */
  RtVoid  RiMakeTextureV(char *pic, char *tex, RtToken swrap, RtToken twrap,
			 RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
			 RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiMakeBumpV(char *pic, char *tex, RtToken swrap, RtToken twrap,
		      RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
		      RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiMakeLatLongEnvironmentV(char *pic, char *tex, RtFilterFunc filterfunc,
				    RtFloat swidth, RtFloat twidth,
				    RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiMakeCubeFaceEnvironmentV(char *px, char *nx, char *py, char *ny,
				     char *pz, char *nz, char *tex, RtFloat fov,
				     RtFilterFunc filterfunc, RtFloat swidth, 
				     RtFloat ywidth,
				     RtInt n, RtToken tokens[], RtPointer parms[]);
  RtVoid  RiMakeShadowV(char *pic, char *tex,
			RtInt n, RtToken tokens[], RtPointer parms[]);

};


#endif

