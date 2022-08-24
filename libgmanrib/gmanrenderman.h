/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - Feb 2001 - Modifications for libgmanrib */

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
#include <stack>
#if HAVE_STD_NAMESPACE
using std::stack;
#endif

#include "ri.h"
#include "gmandictionary.h"

class NOTGMANDLL GMANRenderMan
{
protected:
  GMANDictionary dictionary;


  RtInt colorNComps;
  RtInt lastObjectHandle;
  RtInt lastLightHandle;

  struct Steps
  {
    RtInt uStep;
    RtInt vStep;
  };
  stack<Steps> steps;

public:
  GMANRenderMan();
  virtual ~GMANRenderMan();

  RtVoid push();
  RtVoid pop();

  RtToken        Declare(const char *name, const char *declaration);
  RtVoid         ColorSamples(RtInt n);
  RtLightHandle  LightSource();
  RtLightHandle  AreaLightSource();
  RtVoid         Basis(RtInt u, RtInt v);
  RtObjectHandle ObjectBegin();


  /* Declare Shading Language variable */
  virtual RtVoid  RiDeclare(const char *name, const char *declaration)=0;

  /* RenderMan State machine */
  virtual RtVoid  RiBegin(RtToken name)=0;
  virtual RtVoid  RiEnd(RtVoid)=0;

  /* begin single frame, number frame */
  virtual RtVoid  RiFrameBegin(RtInt frame)=0;
  virtual RtVoid  RiFrameEnd(RtVoid)=0;

  /* begin world (declaration of objects) */
  virtual RtVoid  RiWorldBegin(RtVoid)=0;
  virtual RtVoid  RiWorldEnd(RtVoid)=0;

  virtual RtVoid  RiObjectBegin(RtVoid)=0;
  virtual RtVoid  RiObjectEnd(RtVoid)=0;
  virtual RtVoid  RiObjectInstance(RtObjectHandle handle)=0;

  virtual RtVoid  RiAttributeBegin(RtVoid)=0;
  virtual RtVoid  RiAttributeEnd(RtVoid)=0;

  virtual RtVoid  RiTransformBegin(RtVoid)=0;
  virtual RtVoid  RiTranformEnd(RtVoid)=0;

  virtual RtVoid  RiSolidBegin(RtToken operation)=0;
  virtual RtVoid  RiSolidEnd(RtVoid)=0;

  virtual RtVoid  RiMotionBeginV(RtInt n, RtFloat times[])=0;
  virtual RtVoid  RiMotionEnd(RtVoid)=0;


  /* CAMERA OPTIONS */
  virtual RtVoid  RiFormat (RtInt xres, RtInt yres, RtFloat aspect)=0;
  virtual RtVoid  RiFrameAspectRatio (RtFloat aspect)=0;
  virtual RtVoid  RiScreenWindow (RtFloat left, RtFloat right, RtFloat bot, RtFloat top)=0;
  virtual RtVoid  RiCropWindow (RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax)=0;
  virtual RtVoid  RiProjectionV (RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiClipping(RtFloat hither, RtFloat yon)=0;
  virtual RtVoid  RiDepthOfField (RtFloat fstop, RtFloat focallength, RtFloat focaldistance)=0;
  virtual RtVoid  RiShutter(RtFloat min, RtFloat max)=0;

  /* DISPLAY OPTIONS */
  virtual RtVoid  RiPixelVariance(RtFloat variation)=0;
  virtual RtVoid  RiPixelSamples(RtFloat xsamples, RtFloat ysamples)=0;
  virtual RtVoid  RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth)=0;
  virtual RtVoid  RiExposure(RtFloat gain, RtFloat gamma)=0;
  virtual RtVoid  RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl)=0;
  virtual RtVoid  RiDisplayV(char *name, RtToken type, RtToken mode,
			     RtInt n, RtToken tokens[], RtPointer parms[])=0;

  /* ADDITIONAL OPTIONS */
  virtual RtVoid  RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[])=0;
  virtual RtVoid  RiRelativeDetail(RtFloat relativedetail)=0;
  virtual RtVoid  RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;


  /* SHADING ATTRIBUTES */
  virtual RtVoid  RiColor(RtColor color)=0;
  virtual RtVoid  RiOpacity(RtColor color)=0;
  virtual RtVoid  RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
				       RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4)=0;
  virtual RtVoid  RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiAreaLightSourceV(RtToken name,
				     RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiIlluminate(RtLightHandle light, RtBoolean onoff)=0;
  virtual RtVoid  RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiShadingRate(RtFloat size)=0;
  virtual RtVoid  RiShadingInterpolation(RtToken type)=0;
  virtual RtVoid  RiMatte(RtBoolean onoff)=0;

  /* GEOMETRY ATTRIBUTES */
  virtual RtVoid  RiBound(RtBound)=0;
  virtual RtVoid  RiDetail(RtBound)=0;
  virtual RtVoid  RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis)=0;
  virtual RtVoid  RiGeometricApproximation(RtToken type, RtFloat value)=0;
  virtual RtVoid  RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep)=0;
  virtual RtVoid  RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[],
			      RtFloat knot[], RtFloat min[], RtFloat max[], RtInt n[],
			      RtFloat u[], RtFloat v[], RtFloat w[])=0;
  virtual RtVoid  RiOrientation(RtToken orientation)=0;
  virtual RtVoid  RiReverseOrientation(RtVoid)=0;
  virtual RtVoid  RiSides(RtInt sides)=0;
  virtual RtVoid  RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;

  /* TRANSFORMATIONS */
  virtual RtVoid  RiIdentity(RtVoid)=0;
  virtual RtVoid  RiTransform(RtMatrix transform)=0;
  virtual RtVoid  RiConcatTransform(RtMatrix transform)=0;
  virtual RtVoid  RiPerspective(RtFloat fov)=0;
  virtual RtVoid  RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz)=0;
  virtual RtVoid  RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)=0;
  virtual RtVoid  RiScale(RtFloat sx, RtFloat sy, RtFloat sz)=0;
  virtual RtVoid  RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
			 RtFloat dx2, RtFloat dy2, RtFloat dz2)=0;
  virtual RtVoid  RiDeformationV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiCoordinateSystem(RtToken space)=0;
  virtual RtVoid  RiCoordSysTransform(RtToken space)=0;

  virtual RtVoid  RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])=0;

  /* PRIMITIVES */
  virtual RtVoid  RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n,
				    RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[],  RtInt n,
				    RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[],
					   RtInt verts[], RtInt n, RtToken tokens[], 
					   RtPointer parms[])=0;
  virtual RtVoid  RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap,
			       RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[], 
			       RtPointer parms[])=0;
  virtual RtVoid  RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin,
			     RtFloat umax, RtInt nv, RtInt vorder, RtFloat vknot[],
			     RtFloat vmin, RtFloat vmax,
			     RtInt n, RtToken tokens[], RtPointer parms[])=0;
  
  virtual RtVoid  RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
			    RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
			  RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
			      RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
				 RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
				RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
			  RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiTorusV(RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
			   RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[])=0;
  
  virtual RtVoid  RiBlobbyV(RtInt nleaf, RtInt ncode, RtInt code[],
			    RtInt nflt, RtFloat flt[],
			    RtInt nstr, RtToken str[], 
			    RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiPointsV(RtInt npoints,
			    RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiCurvesV(RtToken type, RtInt ncurves,
			    RtInt nvertices[], RtToken wrap,
			    RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiSubdivisionMeshV(RtToken mask, RtInt nf, RtInt nverts[],
				     RtInt verts[],
				     RtInt ntags, RtToken tags[], RtInt numargs[],
				     RtInt intargs[], RtFloat floatargs[],
				     RtInt n, RtToken tokens[], RtPointer parms[])=0;
  

  virtual RtVoid  RiProcedural(RtPointer data, RtBound bound,
			       RtVoid (*subdivfunc)(RtPointer, RtFloat),
			       RtVoid (*freefunc)(RtPointer))=0;
  virtual RtVoid  RiGeometryV(RtToken type, RtInt n, RtToken tokens[], 
			      RtPointer parms[])=0;

  /* TEXTURE */
  virtual RtVoid  RiMakeTextureV(char *pic, char *tex, RtToken swrap, RtToken twrap,
				 RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
				 RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiMakeLatLongEnvironmentV(char *pic, char *tex, RtFilterFunc filterfunc,
					    RtFloat swidth, RtFloat twidth,
					    RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiMakeCubeFaceEnvironmentV(char *px, char *nx, char *py, char *ny,
					     char *pz, char *nz, char *tex, RtFloat fov,
					     RtFilterFunc filterfunc, RtFloat swidth, 
					     RtFloat ywidth,
					     RtInt n, RtToken tokens[], RtPointer parms[])=0;
  virtual RtVoid  RiMakeShadowV(char *pic, char *tex,
				RtInt n, RtToken tokens[], RtPointer parms[])=0;

  /* ERROR HANDLER */
  virtual RtVoid  RiErrorHandler (RtErrorHandler handler)=0;
};


#endif

