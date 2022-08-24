/***********************************************************
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001 First release
  ----------------------------------------------------------
  ASCII output
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
#include <iostream>
#if HAVE_STD_NAMESPACE
using std::endl;
using std::ios;
#endif

#include "gmanascii.h"
#include "gmanerror.h"

// **************************************************************
// ******* ******* ******* PRINTING TOOLS ******* ******* *******
// **************************************************************
RtVoid GMANASCII::printPL(RtInt n, RtToken tokens[], RtPointer parms[],
			  RtInt vertex, RtInt varying, RtInt uniform)
{
  RtFloat *flt;
  RtInt *nt;
  char **cp;

  GMANTokenId id;
  GMANTokenEntry::TokenType tt;

  RtInt i,j;
  for (i=0; i<n ; i++) {
    out << "\"" << string(tokens[i])<< "\" ";
    out << "[ ";
    id=dictionary.getTokenId(string(tokens[i]));
    tt=dictionary.getType(id);
    
    for (j=0; j<(dictionary.allocSize(id, vertex, varying, uniform)); j++ ) {
      switch (tt) {
      case GMANTokenEntry::FLOAT:
      case GMANTokenEntry::POINT:
      case GMANTokenEntry::VECTOR:
      case GMANTokenEntry::NORMAL:
      case GMANTokenEntry::COLOR:
      case GMANTokenEntry::MATRIX:
      case GMANTokenEntry::HPOINT:
	flt=static_cast<RtFloat *> (parms[i]);
	out << flt[j] <<" ";
	break;
      case GMANTokenEntry::STRING:
	cp=static_cast<char **> (parms[i]);
	out <<"\""<< string(cp[j]) <<"\" ";
	break;
      case GMANTokenEntry::INTEGER:
	nt=static_cast<RtInt *> (parms[i]);
	out << nt[j] << " ";
	break;
      }
    }
    out << "] ";
  }
  out << endl;
}

RtVoid GMANASCII::printToken(RtToken t)
{
  out << "\"" << string(t) << "\" ";
}
RtVoid GMANASCII::printCharP(const char *c)
{
  out << "\"" << string(c) << "\" ";
}

RtVoid GMANASCII::printArray (RtInt n, RtInt *p)
{
  out << "[ ";
  for (RtInt i=0; i<n; i++) {
    out << p[i] << " ";
  }
  out << "] ";
}

RtVoid GMANASCII::printArray (RtInt n, RtFloat *p)
{
  out << "[ ";
  for (RtInt i=0; i<n; i++) {
    out << p[i] << " ";
  }
  out << "] ";
}




// *********************************************************************
// ******* ******* ******* RIB OUTPUT FUNCTIONS  ******* ******* *******
// *********************************************************************
RtVoid GMANASCII::RiDeclare(const char *name, const char *declaration) 
{
  out <<"Declare ";
  printCharP(name);
  printCharP(declaration);
  out <<endl;
}
RtVoid GMANASCII::RiBegin(RtToken name)
{
  out.open(name,ios::out);
  if (!out) {
    GMANError error(RIE_BADFILE,RIE_SEVERE,"Unable to open file");
    throw error;
  }
}
RtVoid GMANASCII::RiEnd(RtVoid)
{
  out.close();
}
RtVoid GMANASCII::RiFrameBegin(RtInt frame)
{
  out <<"FrameBegin "<< frame <<endl;
}
RtVoid GMANASCII::RiFrameEnd(RtVoid)
{
  out <<"FrameEnd"<<endl;
}
RtVoid GMANASCII::RiWorldBegin(RtVoid)
{
  out <<"WorldBegin"<<endl;
}
RtVoid GMANASCII::RiWorldEnd(RtVoid)
{
  out <<"WorldEnd"<<endl;
}
RtVoid GMANASCII::RiObjectBegin(RtVoid)
{
  out <<"ObjectBegin"<<endl;
}
RtVoid GMANASCII::RiObjectEnd(RtVoid)
{
  out <<"ObjectEnd" <<endl;
}
RtVoid  GMANASCII::RiObjectInstance(RtObjectHandle handle)
{
  out <<"ObjectInstance "<< (RtInt) handle <<endl;
}
RtVoid  GMANASCII::RiAttributeBegin(RtVoid)
{
  out <<"AttributeBegin"<<endl;
}
RtVoid  GMANASCII::RiAttributeEnd(RtVoid)
{
  out <<"AttributeEnd"<<endl;
}
RtVoid  GMANASCII::RiTransformBegin(RtVoid)
{
  out <<"TransformBegin"<<endl;
}
RtVoid  GMANASCII::RiTranformEnd(RtVoid)
{
  out <<"TransformEnd"<<endl;
}
RtVoid  GMANASCII::RiSolidBegin(RtToken operation)
{
  out <<"SolidBegin ";
  printToken(operation);
  out << endl;
}
RtVoid  GMANASCII::RiSolidEnd(RtVoid)
{
  out <<"SolidEnd"<<endl;
}
RtVoid  GMANASCII::RiMotionBeginV(RtInt n, RtFloat times[])
{
  out <<"MotionBegin ";
  printArray(n,times);
  out << endl;
}
RtVoid  GMANASCII::RiMotionEnd(RtVoid)
{
  out <<"MotionEnd"<<endl; 
}




// **************************************************************
// ******* ******* ******* CAMERA OPTIONS ******* ******* *******
// **************************************************************
RtVoid  GMANASCII::RiFormat (RtInt xres, RtInt yres, RtFloat aspect)
{
  out <<"Format "<< xres <<" "<< yres <<" "<< aspect <<endl;
}
RtVoid  GMANASCII::RiFrameAspectRatio (RtFloat aspect)
{
  out <<"FrameAspectRatio "<< aspect <<endl; 
}
RtVoid  GMANASCII::RiScreenWindow (RtFloat left, RtFloat right, RtFloat bottom, RtFloat top)
{
  out <<"ScreenWindow "<< left <<" "<< right <<" "<< bottom <<" "<< top <<endl;
}
RtVoid  GMANASCII::RiCropWindow (RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax)
{
  out <<"CropWindow "<< xmin <<" "<< xmax <<" "<< ymin <<" "<< ymax <<endl; 
}
RtVoid  GMANASCII::RiProjectionV (RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Projection ";
  printToken(name);
  printPL(n,tokens,parms);
}
RtVoid  GMANASCII::RiClipping(RtFloat hither, RtFloat yon)
{
  out <<"Clipping "<< hither <<" "<< yon <<endl;
}
RtVoid  GMANASCII::RiDepthOfField (RtFloat fstop, RtFloat focallength, RtFloat focaldistance)
{
  out <<"DepthOfField "<< fstop <<" "<< focallength <<" "<< focaldistance <<endl;
}
RtVoid  GMANASCII::RiShutter(RtFloat min, RtFloat max)
{
  out <<"Shutter "<< min <<" "<< max <<" "<<endl;
}




// ***************************************************************
// ******* ******* ******* DISPLAY OPTIONS ******* ******* *******
// ***************************************************************
RtVoid  GMANASCII::RiPixelVariance(RtFloat variation)
{
  out <<"PixelVariance "<< variation <<endl;
}
RtVoid  GMANASCII::RiPixelSamples(RtFloat xsamples, RtFloat ysamples)
{
  out <<"PixelSamples "<< xsamples <<" "<< ysamples <<endl;
}
RtVoid  GMANASCII::RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth)
{
  out <<"PixelFilter ";
  if (filterfunc==RiBoxFilter)
    out << "box ";
  else if (filterfunc==RiTriangleFilter)
    out << "triangle ";
  else if (filterfunc==RiCatmullRomFilter)
    out << "catmull-rom ";
  else if (filterfunc==RiSincFilter)
    out << "sinc ";
  else if (filterfunc==RiGaussianFilter)
    out << "gaussian ";
  out << xwidth << " " << ywidth << endl;
}
RtVoid  GMANASCII::RiExposure(RtFloat gain, RtFloat gamma)
{
  out <<"Exposure "<< gain <<" "<< gamma <<endl;
}
RtVoid  GMANASCII::RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Imager ";
  printToken(name);
  printPL(n,tokens,parms);
}
RtVoid  GMANASCII::RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl)
{
  out <<"Quantize ";
  printToken(type);
  out << one <<" "<< min <<" "<< max <<" "<< ampl <<endl;
}
RtVoid  GMANASCII::RiDisplayV(char *name, RtToken type, RtToken mode,
			      RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Display ";
  printCharP(name);
  printToken(type);
  printToken(mode);
  printPL(n, tokens, parms);
}




// ******************************************************************
// ******* ******* ******* ADDITIONAL OPTIONS ******* ******* *******
// ******************************************************************
RtVoid  GMANASCII::RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Hider ";
  printToken(type);
  printPL(n,tokens,parms);
}
RtVoid  GMANASCII::RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[])
{
  out <<"ColorSamples ";
  printArray(n*3,nRGB);
  printArray(n*3,RGBn);
  out <<endl;
}
RtVoid  GMANASCII::RiRelativeDetail(RtFloat relativedetail)
{
  out <<"RelativeDetail "<< relativedetail <<endl;
}
RtVoid  GMANASCII::RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Option ";
  printToken(name);
  printPL(n,tokens,parms);
}




// ******************************************************************
// ******* ******* ******* SHADING ATTRIBUTES ******* ******* *******
// ******************************************************************
RtVoid  GMANASCII::RiColor(RtColor color)
{
  out <<"Color ";
  printArray(colorNComps,color);
  out <<endl;  
}
RtVoid  GMANASCII::RiOpacity(RtColor color)
{
  out <<"Opacity ";
  printArray(colorNComps,color);
  out <<endl;
}
RtVoid  GMANASCII::RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
					RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4)
{
  out <<"TextureCoordinates ";
  out << s1 <<" "<< t1 <<" "<< s2 <<" "<< t2 <<" ";
  out << s3 <<" "<< t3 <<" "<< s4 <<" "<< t4 <<endl;
}
RtVoid GMANASCII::RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"LightSource ";
  printPL(n, tokens, parms);
}
RtVoid GMANASCII::RiAreaLightSourceV(RtToken name,
					RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"AreaLightSource ";
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiIlluminate(RtLightHandle light, RtBoolean onoff)
{
  out <<"Illuminate ";
  out <<(RtInt) light <<" ";
  if (onoff==RI_TRUE)
    out << "1" <<endl;
  else
    out << "0" <<endl;
}
RtVoid  GMANASCII::RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Surface ";
  printToken(name);
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Atmosphere ";
  printToken(name);
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Interior ";
  printToken(name);
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Exterior ";
  printToken(name);
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiShadingRate(RtFloat size)
{
  out <<"ShadingRate "<< size <<endl;
}
RtVoid  GMANASCII::RiShadingInterpolation(RtToken type)
{
  out <<"ShadingInterpolation ";
  printToken(type);
  out << endl;
}
RtVoid  GMANASCII::RiMatte(RtBoolean onoff)
{
  out <<"Matte ";
  if (onoff==RI_TRUE)
    out << "1" <<endl;
  else
    out << "0" <<endl;
}




// *******************************************************************
// ******* ******* ******* GEOMETRY ATTRIBUTES ******* ******* *******
// *******************************************************************
RtVoid  GMANASCII::RiBound(RtBound b)
{
  out <<"Bound ";
  printArray(6,b);
  out << endl;
}
RtVoid  GMANASCII::RiDetail(RtBound d)
{
  out <<"Detail ";
  printArray(6,d);
  out << endl;
}
RtVoid  GMANASCII::RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis)
{
  out <<"DetailRange "<< minvis <<" "<< lowtran <<" "<< uptran <<" "<< maxvis <<endl;
}
RtVoid  GMANASCII::RiGeometricApproximation(RtToken type, RtFloat value)
{
  out <<"GeometricApproximation ";
  printToken(type);
  out << value << endl;
}
RtVoid  GMANASCII::RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep)
{
  RtInt i;
  out <<"Basis [";
  for (i=0; i<16; i++) {
    out << ubasis[i/4][i%4] << " ";
  }
  out << "] ";
  out << ustep <<" ";
  out << "[ ";
  for (i=0; i<16; i++) {
    out << vbasis[i/4][i%4] << " ";
  }
  out << "] ";
  out << vstep <<endl;
}
RtVoid  GMANASCII::RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[],
			       RtFloat knot[], RtFloat min[], RtFloat max[], RtInt n[],
			       RtFloat u[], RtFloat v[], RtFloat w[])
{
  RtInt i;
  RtInt ttlc=0;
  for(i=0; i<nloops; i++)
    ttlc+=ncurves[i];

  RtInt nbcoords=0;
  RtInt knotsize=0;
  for(i=0; i<ttlc; i++) {
    nbcoords+=n[i];
    knotsize+=order[i]+n[i];
  }

  out <<"TrimCurve ";
  printArray(nloops, ncurves);
  printArray(ttlc, order);
  printArray(knotsize, knot);
  printArray(ttlc, min);
  printArray(ttlc, max);
  printArray(ttlc, n);
  printArray(nbcoords,u);
  printArray(nbcoords,v);
  printArray(nbcoords,w);
}
RtVoid  GMANASCII::RiOrientation(RtToken o)
{
  out <<"Orientation ";
  printToken(o);
  out << endl;
}
RtVoid  GMANASCII::RiReverseOrientation(RtVoid)
{
  out <<"ReverseOrientation"<<endl;
}
RtVoid  GMANASCII::RiSides(RtInt sides)
{
  out <<"Sides "<< sides <<endl;
}
RtVoid  GMANASCII::RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Displacement ";
  printToken(name);
  printPL(n, tokens, parms);
}




// ***************************************************************
// ******* ******* ******* TRANSFORMATIONS ******* ******* *******
// ***************************************************************
RtVoid  GMANASCII::RiIdentity(RtVoid)
{
  out <<"Identity"<<endl;
}
RtVoid  GMANASCII::RiTransform(RtMatrix transform)
{
  out <<"Transform [";
  for (RtInt i=0; i<16; i++) {
    out << transform[i/4][i%4] << " ";
  }
  out << "]" << endl;
}
RtVoid  GMANASCII::RiConcatTransform(RtMatrix transform)
{
  out <<"ConcatTransform [";
  for (RtInt i=0; i<16; i++) {
    out << transform[i/4][i%4] << " ";
  }
  out << "]" << endl;
}
RtVoid  GMANASCII::RiPerspective(RtFloat fov)
{
  out <<"Perspective "<< fov <<endl;
}
RtVoid  GMANASCII::RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz)
{
  out <<"Translate "<< dx <<" "<< dy <<" "<< dz <<" "<<endl;
}
RtVoid  GMANASCII::RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)
{
  out <<"Rotate "<< angle <<" "<< dx <<" "<< dy <<" "<< dz <<" "<<endl;
}
RtVoid  GMANASCII::RiScale(RtFloat sx, RtFloat sy, RtFloat sz)
{
  out <<"Scale "<< sx <<" "<< sy <<" "<< sz <<" "<<endl;
}
RtVoid  GMANASCII::RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
			  RtFloat dx2, RtFloat dy2, RtFloat dz2)
{
  out <<"Skew " << angle << " ";
  out << dx1 <<" "<< dy1 <<" "<< dz1 <<" ";
  out << dx2 <<" "<< dy2 <<" "<< dz2 <<endl;
}
RtVoid  GMANASCII::RiDeformationV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Deformation ";
  printToken(name);
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiCoordinateSystem(RtToken space)
{
  out <<"CoordinateSystem ";
  printToken(space);
  out << endl;
}
RtVoid  GMANASCII::RiCoordSysTransform(RtToken space)
{
  out <<"CoordSysTransform ";
  printToken(space);
  out << endl;
}
RtPoint *GMANASCII::RiTransformPoints(RtToken fromspace, RtToken tospace, RtInt n,
				      RtPoint points[])
{
  GMANError error(RIE_CONSISTENCY, RIE_WARNING, "RiTransformPoints is a C api only call");
  throw error;
  return (RtPoint *) 0;
}
RtVoid GMANASCII::RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Attribute ";
  printToken(name);
  printPL(n, tokens, parms);
}




// **********************************************************
// ******* ******* ******* PRIMITIVES ******* ******* *******
// **********************************************************
RtVoid  GMANASCII::RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Polygon ";
  printPL(n, tokens, parms, nverts, nverts);
}
RtVoid  GMANASCII::RiGeneralPolygonV(RtInt nloops, RtInt nverts[], 
				     RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"GeneralPolygon ";
  printArray(nloops,nverts);

  RtInt nbpts=0;
  for (RtInt i=0; i<nloops; i++) {
    nbpts+=nverts[i];
  }
  printPL(n, tokens, parms, nbpts, nbpts);
}
RtVoid  GMANASCII::RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[],
				     RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"PointsPolygons ";
  printArray(npolys,nverts);

  RtInt i;
  RtInt nbpts=0;
  for (i=0; i<npolys; i++) {
    nbpts+=nverts[i];
  }
  printArray(nbpts,verts);

  RtInt psize=0;
  for (i=0; i<nbpts; i++) {
    if (psize<verts[i])
      psize=verts[i];
  }
  printPL(n, tokens, parms, psize+1, psize+1, npolys);
}
RtVoid  GMANASCII::RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[],
					    RtInt verts[],
					    RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"PointsGeneralpolygons ";
  printArray(npolys,nloops);

  RtInt i;
  RtInt nbvert=0;
  for (i=0; i<npolys; i++) {
    nbvert+=nloops[i];
  }
  printArray(nbvert,nverts);

  RtInt nv=0;
  for (i=0; i<nbvert; i++) {
    nv+=nverts[i];
  }
  printArray(nv,verts);

  RtInt psize=0;
  for (i=0; i<nv; i++) {
    if (psize<verts[i])
      psize=verts[i];
  }
  printPL(n, tokens, parms, psize+1, psize+1, npolys);
}
RtVoid  GMANASCII::RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])
{
  RtInt nb;
  if (type==RI_BILINEAR)
    nb=4;
  else if (type==RI_BICUBIC)
    nb=16;
  else {
    GMANError error(RIE_BADTOKEN, RIE_WARNING, "Unknown RiPatch type specified");
    throw error;
  }

  out <<"Patch ";
  printToken(type);
  printPL(n, tokens, parms, nb, 4);
}
RtVoid  GMANASCII::RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap,
				RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[], 
				RtPointer parms[])
{
  out <<"PatchMesh ";
  printToken(type);
  out << nu << " ";
  printToken(uwrap);
  out << nv << " ";
  printToken(vwrap);

  RtInt nuptch, nvptch;
  RtInt ii=0;
  if (type==RI_BILINEAR) {
    if (uwrap==RI_PERIODIC) {
      nuptch=nu;
    } else if (uwrap==RI_NONPERIODIC) {
      nuptch=nu-1;
      ii+=1;
    } else {
      GMANError error(RIE_BADTOKEN, RIE_WARNING,
		      "Unknown RiPatchMesh uwrap token specified");
      throw error;
    }
    if (vwrap==RI_PERIODIC) {
      nvptch=nv;
    } else if (vwrap==RI_NONPERIODIC) {
      nvptch=nv-1;
      ii+=1;
    } else {
      GMANError error(RIE_BADTOKEN, RIE_WARNING,
		      "Unknown RiPatchMesh vwrap token specified");
      throw error;
    }
    ii+=nuptch+nvptch;


  } else if (type==RI_BICUBIC) {
    RtInt nustep=steps.top().uStep;
    RtInt nvstep=steps.top().vStep;
    
    if (uwrap==RI_PERIODIC) {
      nuptch=nu/nustep;
    } else if (uwrap==RI_NONPERIODIC) {
      nuptch=(nu-4)/nustep +1;
      ii+=1;
    } else {
      GMANError error(RIE_BADTOKEN, RIE_WARNING,
		      "Unknown RiPatchMesh uwrap token specified");
      throw error;
    }
    if (vwrap==RI_PERIODIC) {
      nvptch=nv/nvstep;
    } else if (vwrap==RI_NONPERIODIC) {
      nvptch=(nv-4)/nvstep +1;
      ii+=1;
    } else {
      GMANError error(RIE_BADTOKEN, RIE_WARNING,
		      "Unknown RiPatchMesh vwrap token specified");
      throw error;
    }
    ii+=nuptch+nvptch;


  
  } else {
    GMANError error(RIE_BADTOKEN, RIE_WARNING,
		    "Unknown RiPatchMesh type specified");
    throw error;
  }
  printPL(n, tokens, parms, nu*nv, ii, nuptch*nvptch);
}
RtVoid  GMANASCII::RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[],
			      RtFloat umin, RtFloat umax,
			      RtInt nv, RtInt vorder, RtFloat vknot[],
			      RtFloat vmin, RtFloat vmax,
			      RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"NuPatch ";
  out << nu <<" "<< uorder << " ";
  printArray(nu+uorder,uknot);
  out << umin <<" "<< umax <<" ";

  out << nv <<" "<< vorder << " ";
  printArray(nv+vorder,vknot);
  out << vmin <<" "<< vmax <<" ";
  printPL(n, tokens, parms, nu*nv, (2+nu-uorder)*(2+nv-vorder), (1+nu-uorder)*(1+nv-vorder));
}
RtVoid  GMANASCII::RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
			     RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Sphere "<< radius <<" "<< zmin<<" "<< zmax <<" "<< tmax <<" ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
			   RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Cone "<< height <<" "<< radius <<" "<< tmax <<" ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
			       RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Cylinder "<< radius <<" "<< zmin <<" "<< zmax <<" "<< tmax <<" ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Hyperboloid ";
  printArray(3,point1);
  printArray(3,point2);
  out << tmax << " ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
				 RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Paraboloid "<< rmax <<" "<< zmin <<" "<< zmax <<" "<< tmax <<" ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
			   RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Disk "<< height <<" "<< radius <<" "<< tmax <<" ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiTorusV(RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
			    RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Torus "<< majrad <<" "<< minrad <<" "<< phimin <<" "<< phimax <<" "<< tmax <<" ";
  printPL(n, tokens, parms, 4, 4);
}
RtVoid  GMANASCII::RiBlobbyV(RtInt nleaf, RtInt ncode, RtInt code[],
			     RtInt nflt, RtFloat flt[],
			     RtInt nstr, RtToken str[], 
			     RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Blobby " << nleaf << " ";
  printArray(ncode,code);
  printArray(nflt,flt);
  for(RtInt i=0; i<nstr; i++)
    printToken(str[i]);
  printPL(n, tokens, parms, nleaf, nleaf);
}
RtVoid  GMANASCII::RiPointsV(RtInt npoints,
			     RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"Points ";
  printPL(n, tokens, parms, npoints, npoints);
}
RtVoid  GMANASCII::RiCurvesV(RtToken type, RtInt ncurves,
			     RtInt nvertices[], RtToken wrap,
			     RtInt n, RtToken tokens[], RtPointer parms[])
{
  RtInt i;
  RtInt vval=0;
  if (type==RI_LINEAR) {
    if (wrap==RI_PERIODIC) {
      for(i=0; i<ncurves; i++) {
	vval+=nvertices[i];
      }
    } else if (wrap==RI_NONPERIODIC) {
      for(i=0; i<ncurves; i++) {
	vval+=nvertices[i];
      }
    } else {
      GMANError error(RIE_BADTOKEN, RIE_WARNING, "Unknown RiCurves wrap mode");
      throw error;
    }
  } else if (type==RI_CUBIC) {
    if (wrap==RI_PERIODIC) {
      for(i=0; i<ncurves; i++) {
	vval+=(nvertices[i]-4)/steps.top().vStep;
      }
    } else if (wrap==RI_NONPERIODIC) {
      for(i=0; i<ncurves; i++) {
	vval+=2 + (nvertices[i]-4)/steps.top().vStep;
      }
    } else {
      GMANError error(RIE_BADTOKEN, RIE_WARNING, "Unknown RiCurves wrap mode");
      throw error;
    }
  } else {
    GMANError error(RIE_BADTOKEN, RIE_WARNING, "Unknown RiCurves type");
    throw error;
  }

  out <<"Curves ";
  printToken(type);
  printArray(ncurves,nvertices);
  printToken(wrap);

  RtInt nbpts=0;
  for(i=0; i<ncurves; i++) {
    nbpts+=nvertices[i];
  }
  printPL(n, tokens, parms, nbpts, vval, ncurves);
}
RtVoid  GMANASCII::RiSubdivisionMeshV(RtToken mask, RtInt nf, RtInt nverts[],
				      RtInt verts[],
				      RtInt ntags, RtToken tags[], RtInt numargs[],
				      RtInt intargs[], RtFloat floatargs[],
				      RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"SubdivisionMesh ";
  printToken(mask);
  printArray(nf,nverts);

  RtInt i;
  RtInt vsize=0;
  for(i=0; i<nf; i++) {
    vsize+=nverts[i];
  }
  printArray(vsize,verts);
  
  for(i=0; i<ntags; i++) {
    printToken(tags[i]);
  }
  printArray(ntags*2,numargs);

  RtInt isize=0, fsize=0;
  for(i=0; i<ntags*2; i++) {
    if (i%2==0)
      isize+=numargs[i];
    else
      fsize+=numargs[i];
  }
  printArray(isize,intargs);
  printArray(fsize,floatargs);

  RtInt psize=0;
  for(i=0; i<vsize; i++) {
    if (psize<verts[i])
      psize=verts[i];
  }
  printPL(n, tokens, parms, psize+1, psize+1, nf);
}
RtVoid  GMANASCII::RiProcedural(RtPointer data, RtBound bound,
				RtVoid (*subdivfunc)(RtPointer, RtFloat),
				RtVoid (*freefunc)(RtPointer))
{
  string sf;
  RtInt a;
  if (subdivfunc==RiProcDelayedReadArchive) {
    sf="DelayedReadArchive";
    a=1;
  } else if (subdivfunc==RiProcRunProgram) {
    sf="ReadProgram";
    a=2;
  } else if (subdivfunc==RiProcDynamicLoad) {
    sf="DynamicLoad";
    a=3;
  } else {
    GMANError error(RIE_CONSISTENCY,RIE_WARNING,"Unknow procedural function");
    throw error;
  }

  out <<"Procedural ";
  switch (a) {
  case 1:
    out << sf <<" [ "<< string(&((char *) data)[0]) <<" ] [ "; 
    for (RtInt i=0; i<6 ; i++)
      out << bound[i] <<" ";
    out << "]" << endl;
    break;
  case 2:
    out << sf <<" [ ";
    out << string(&((char *) data)[0]) <<" ";
    out << string(&((char *) data)[1]) <<" ] [ ";
    for (RtInt i=0; i<6 ; i++)
      out << bound[i] <<" ";
    out << "]" << endl;
    break;
  case 3:
    out << sf <<" [ ";
    out << string(&((char *) data)[0]) <<" ";
    out << string(&((char *) data)[1]) <<" ] [ ";
    for (RtInt i=0; i<6 ; i++)
      out << bound[i] <<" ";
    out << "]" << endl;
    break;
  }
}
RtVoid  GMANASCII::RiGeometryV(RtToken type, RtInt n, RtToken tokens[], 
			       RtPointer parms[])
{
  out <<"Geometry ";
  printToken(type);
  printPL(n, tokens, parms);
}




// *******************************************************
// ******* ******* ******* TEXTURE ******* ******* *******
// *******************************************************
RtVoid  GMANASCII::RiMakeTextureV(char *pic, char *tex, RtToken swrap, RtToken twrap,
				  RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{
  string ff;
  if (filterfunc==RiGaussianFilter)
    ff="gaussian";
  else if (filterfunc==RiBoxFilter)
    ff="box";
  else if (filterfunc==RiTriangleFilter)
    ff="triangle";
  else if (filterfunc==RiCatmullRomFilter)
    ff="catmull-rom";
  else if (filterfunc==RiSincFilter)
    ff="sinc";
  else {
    GMANError error(RIE_CONSISTENCY, RIE_WARNING, "Unknown filter function");
    throw error;
  }

  out <<"MakeTexture ";
  printCharP(pic);
  printCharP(tex);
  printToken(swrap);
  printToken(twrap);
  out << ff <<" "<< swidth <<" "<< twidth <<" ";
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiMakeLatLongEnvironmentV(char *pic, char *tex, RtFilterFunc filterfunc,
					     RtFloat swidth, RtFloat twidth,
					     RtInt n, RtToken tokens[], RtPointer parms[])
{
  string ff;
  if (filterfunc==RiGaussianFilter)
    ff="gaussian";
  else if (filterfunc==RiBoxFilter)
    ff="box";
  else if (filterfunc==RiTriangleFilter)
    ff="triangle";
  else if (filterfunc==RiCatmullRomFilter)
    ff="catmull-rom";
  else if (filterfunc==RiSincFilter)
    ff="sinc";
  else {
    GMANError error(RIE_CONSISTENCY, RIE_WARNING, "Unknown filter function");
    throw error;
  }

  out <<"MakeLatLongEnvironment ";
  printCharP(pic);
  printCharP(tex);
  out << ff <<" "<< swidth <<" "<< twidth <<" ";
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiMakeCubeFaceEnvironmentV(char *px, char *nx, char *py, char *ny,
					      char *pz, char *nz, char *tex, RtFloat fov,
					      RtFilterFunc filterfunc, RtFloat swidth, 
					      RtFloat twidth,
					      RtInt n, RtToken tokens[], RtPointer parms[])
{
  string ff;
  if (filterfunc==RiGaussianFilter)
    ff="gaussian";
  else if (filterfunc==RiBoxFilter)
    ff="box";
  else if (filterfunc==RiTriangleFilter)
    ff="triangle";
  else if (filterfunc==RiCatmullRomFilter)
    ff="catmull-rom";
  else if (filterfunc==RiSincFilter)
    ff="sinc";
  else {
    GMANError error(RIE_CONSISTENCY, RIE_WARNING, "Unknown filter function");
    throw error;
  }
  out <<"MakeCubeFaceEnvironment ";
  printCharP(px);
  printCharP(nx);
  printCharP(py);
  printCharP(ny);
  printCharP(pz);
  printCharP(nz);
  printCharP(tex);
  out << fov <<" "<< ff <<" "<< swidth <<" "<< twidth <<" ";
  printPL(n, tokens, parms);
}
RtVoid  GMANASCII::RiMakeShadowV(char *pic, char *tex,
				 RtInt n, RtToken tokens[], RtPointer parms[])
{
  out <<"MakeShadow ";
  printCharP(pic);
  printCharP(tex);
  printPL(n, tokens, parms);
}




// *************************************************************
// ******* ******* ******* ERROR HANDLER ******* ******* *******
// *************************************************************
RtVoid GMANASCII::RiErrorHandler(RtErrorHandler handler)
{
  string ch;
  if (handler==RiErrorIgnore) ch="\"ignore\"";
  else if (handler==RiErrorPrint) ch="\"print\"";
  else if (handler==RiErrorAbort) ch="\"abort\"";
  else {
    throw GMANError(RIE_CONSISTENCY,RIE_WARNING,"Unknown Error handler");
  }
  out <<"ErrorHandler "<< ch <<endl;
}
