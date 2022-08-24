/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

// LJL - Feb 2001 - small changes for libgmanrib

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

/* System Headers */
// variable argument lists
#include <stdarg.h> 
#include <stack>

/* Local headers */
#include "ri.h"
#include "gmanerror.h"
#include "gmangetarguments.h"
#include "gmanrenderman.h"
#include "gmancontext.h"

/*
 * RenderMan C API General functions
 *
 */

/* The context handler */
static GMANContext context;

/* Pass all C API functions through the instantiation of
 * the C++ API
 */

extern "C" RtContextHandle RiGetContext (RtVoid)
{
  try {
    return context.getContext();
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
};

extern "C" RtVoid RiContext (RtContextHandle ch)
{
  try {
    context.switchTo(ch);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

//
extern "C" RtToken RiDeclare (char *name, char *declaration)
{
  try {
    RtToken a;
    a=context.current().Declare(name, declaration);
    context.current().RiDeclare(name, declaration);
    return a; 
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
}; 

//
extern "C" RtVoid RiBegin (RtToken name)
{
  try {
    context.addContext();
    context.current().RiBegin(name);
  } catch (GMANError &error) {
    if (RiLastError) {
      context.removeCurrent();
    }
    GMANHandleError(error);
  }
}

// 
extern "C" RtVoid RiEnd (RtVoid)
{
  try {
    context.current().RiEnd();
    context.removeCurrent();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}

//
extern "C" RtVoid RiFrameBegin (RtInt frame)
{
  try {
    context.current().RiFrameBegin(frame);
    context.current().push();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

//
extern "C" RtVoid RiFrameEnd (RtVoid)
{
  try {
    context.current().RiFrameEnd();
    context.current().pop();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

//
extern "C" RtVoid RiWorldBegin (RtVoid)
{
  try {
    context.current().RiWorldBegin();
    context.current().push();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

//
extern "C" RtVoid RiWorldEnd (RtVoid)
{
  try {
    context.current().RiWorldEnd();
    context.current().pop();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Global Options ======= */
extern "C" RtVoid RiFormat (RtInt xres, RtInt yres, RtFloat aspect)
{
  try {
    context.current().RiFormat(xres, yres, aspect);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiFrameAspectRatio (RtFloat aspect)
{
  try {
    context.current().RiFrameAspectRatio(aspect);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiScreenWindow (RtFloat left, RtFloat right, 
				  RtFloat bot, RtFloat top)
{
  try {
    context.current().RiScreenWindow(left, right, bot, top);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiCropWindow (RtFloat xmin, 
				RtFloat xmax, 
				RtFloat ymin, 
				RtFloat ymax)
{
  try {
    context.current().RiCropWindow( xmin,  xmax,  ymin, ymax);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiProjection (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];

    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiProjectionV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiProjectionV (RtToken name, RtInt n, 
				 RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiProjectionV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiClipping (RtFloat hither, RtFloat yon)
{
  try {
    context.current().RiClipping( hither, yon);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDepthOfField (RtFloat fstop, 
				  RtFloat focallength, RtFloat focaldistance)
{
  try {
    context.current().RiDepthOfField (fstop, focallength, focaldistance);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiShutter (RtFloat min, RtFloat max)
{
  try {
    context.current().RiShutter( min, max);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPixelVariance (RtFloat variation)
{
  try {
    context.current().RiPixelVariance( variation);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPixelSamples (RtFloat xsamples, RtFloat ysamples)
{
  try {
    context.current().RiPixelSamples( xsamples, ysamples);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPixelFilter (RtFilterFunc filterfunc, RtFloat xwidth, 
				 RtFloat ywidth)
{
  try {
    context.current().RiPixelFilter( filterfunc,  xwidth, ywidth);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiExposure (RtFloat gain, RtFloat gamma)
{
  try {
    context.current().RiExposure( gain, gamma);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiImager (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiImagerV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiImagerV (RtToken name, RtInt n, 
			     RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiImagerV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiQuantize (RtToken type, RtInt one, 
			      RtInt min, RtInt max, RtFloat ampl)
{
  try {
    context.current().RiQuantize( type,  one,  min,  max,  ampl);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDisplay (char *name, RtToken type, RtToken mode, ...)
{
  try {
    va_list args;
    va_start(args, mode);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];

    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiDisplayV(name, type, mode, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDisplayV (char *name, RtToken type, RtToken mode,
			      RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiDisplayV(name, type,  mode, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiHider (RtToken type, ...)
{
  try {
    va_list args;
    va_start(args, type);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiHiderV(type, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiHiderV (RtToken type, RtInt n, RtToken tokens[], 
			    RtPointer parms[])
{
  try {
    context.current().RiHiderV( type,  n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiColorSamples (RtInt n, RtFloat nRGB[], RtFloat RGBn[])
{
  try {
    context.current().RiColorSamples( n,  nRGB,  RGBn);
    context.current().ColorSamples(n);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiRelativeDetail (RtFloat relativedetail)
{
  try {
    context.current().RiRelativeDetail(relativedetail);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiOption (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiOptionV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiOptionV (RtToken name, RtInt n, 
			     RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiOptionV( name,  n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiAttributeBegin (RtVoid)
{
  try {
    context.current().RiAttributeBegin();
    context.current().push();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiAttributeEnd (RtVoid)
{
  try {
    context.current().RiAttributeEnd();
    context.current().pop();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Attributes ======= */
extern "C" RtVoid RiColor (RtColor color)
{
  try {
    context.current().RiColor(color);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiOpacity (RtColor color)
{
  try {
    context.current().RiOpacity(color);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTextureCoordinates(RtFloat s1, RtFloat t1, 
				       RtFloat s2, RtFloat t2,
				       RtFloat s3, RtFloat t3, 
				       RtFloat s4, RtFloat t4)
{
  try {
    context.current().RiTextureCoordinates(s1, t1, s2, t2, s3, t3, s4, t4);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtLightHandle RiLightSource (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    return RiLightSourceV( name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
};

extern "C" RtLightHandle RiLightSourceV (RtToken name, RtInt n, 
					 RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiLightSourceV(name, n, tokens, parms);
    return context.current().LightSource();
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
};

extern "C" RtLightHandle RiAreaLightSource (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    return RiAreaLightSourceV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
};

extern "C" RtLightHandle RiAreaLightSourceV (RtToken name,
					     RtInt n, RtToken tokens[], 
					     RtPointer parms[])
{
  try {
    context.current().RiAreaLightSourceV(name, n, tokens, parms);
    return context.current().AreaLightSource();
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
};


extern "C" RtVoid RiIlluminate (RtLightHandle light, RtBoolean onoff)
{
  try {
    context.current().RiIlluminate(light, onoff);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};
extern "C" RtVoid RiSurface (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);
    
    RiSurface(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSurfaceV (RtToken name, RtInt n, 
			      RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiSurfaceV(name,  n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiAtmosphere (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiAtmosphere( name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiAtmosphereV (RtToken name, RtInt n, 
				 RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiAtmosphereV(name, n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiInterior (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiInteriorV(name, n, tokens, parms );
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiInteriorV (RtToken name, RtInt n, 
			       RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiInteriorV( name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiExterior (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiExteriorV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiExteriorV (RtToken name, RtInt n, 
			       RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiExteriorV( name,  n,  tokens,  parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiShadingRate (RtFloat size)
{
  try {
    context.current().RiShadingRate(size);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiShadingInterpolation (RtToken type)
{
  try {
    context.current().RiShadingInterpolation(type);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMatte (RtBoolean onoff)
{
  try {
    context.current().RiMatte(onoff);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiBound (RtBound b)
{
  try {
    context.current().RiBound(b);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDetail (RtBound b)
{
  try {
    context.current().RiDetail(b);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDetailRange (RtFloat minvis, RtFloat lowtran, 
				 RtFloat uptran, RtFloat maxvis)
{
  try {
    context.current().RiDetailRange(minvis, lowtran, uptran, maxvis);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiGeometricApproximation (RtToken type, RtFloat value)
{
  try {
    context.current().RiGeometricApproximation(type, value);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiOrientation (RtToken orientation)
{
  try {
    context.current().RiOrientation(orientation);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiReverseOrientation (RtVoid)
{
  try {
    context.current().RiReverseOrientation();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSides (RtInt sides)
{
  try {
    context.current().RiSides(sides);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Transformations ======= */
extern "C" RtVoid RiIdentity (RtVoid)
{
  try {
    context.current().RiIdentity();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTransform (RtMatrix transform)
{
  try {
    context.current().RiTransform(transform);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiConcatTransform (RtMatrix transform)
{
  try {
    context.current().RiConcatTransform(transform);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPerspective (RtFloat fov)
{
  try {
    context.current().RiPerspective(fov);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTranslate (RtFloat dx, RtFloat dy, RtFloat dz)
{
  try {
    context.current().RiTranslate(dx,  dy,  dz);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiRotate (RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)
{
  try {
    context.current().RiRotate( angle,  dx,  dy,  dz);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiScale (RtFloat sx, RtFloat sy, RtFloat sz)
{
  try {
    context.current().RiScale(sx, sy, sz);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSkew (RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
			  RtFloat dx2, RtFloat dy2, RtFloat dz2)
{  
  try {
    context.current().RiSkew( angle,  dx1, dy1, dz1, dx2, dy2,  dz2);

  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDisplacement (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);
  
    RiDisplacementV( name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDisplacementV (RtToken name, RtInt n, 
				   RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiDisplacementV( name,  n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiCoordinateSystem (RtToken space)
{
  try {
    context.current().RiCoordinateSystem(space);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiCoordSysTransform (RtToken space)
{
  try {
    context.current().RiCoordSysTransform(space);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};  

/* Spatial transformation of a list of points */
extern "C" RtPoint * RiTransformPoints (RtToken fromspace, RtToken tospace, 
					RtInt n, RtPoint points[])
{
    GMANError error(RIE_CONSISTENCY, RIE_WARNING, "RiTransformPoints is a C api only call");
    GMANHandleError(error);
    return (RtPoint *) RI_NULL;
};

extern "C" RtVoid RiTransformBegin (RtVoid)
{
  try {
    context.current().RiTransformBegin();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTranformEnd (RtVoid)
{
  try {
    context.current().RiTranformEnd();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiAttribute (RtToken name, ...)
{
  try {
    va_list args;
    va_start(args, name);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiAttributeV(name, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiAttributeV (RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiAttributeV( name,  n,  tokens,  parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Primitives ======= */
extern "C" RtVoid RiPolygon (RtInt nverts, ...)
{
  try {
    va_list args;
    va_start(args, nverts);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiPolygonV( nverts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPolygonV (RtInt nverts, RtInt n, 
			      RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiPolygonV(nverts,  n,  tokens,  parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiGeneralPolygon (RtInt nloops, RtInt nverts[], ...)
{
  try {
    va_list args;
    va_start(args, nverts);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiGeneralPolygonV( nloops,  nverts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiGeneralPolygonV (RtInt nloops, RtInt nverts[], RtInt n,
				     RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiGeneralPolygonV( nloops, nverts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPointsPolygons (RtInt npolys, RtInt nverts[], 
				    RtInt verts[], ...)
{
  try {
    va_list args;
    va_start(args, verts);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiPointsPolygonsV(npolys,  nverts,  verts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};


extern "C" RtVoid RiPointsPolygonsV (RtInt npolys, RtInt nverts[],
				     RtInt verts[],  RtInt n,
				     RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiPointsPolygonsV( npolys, nverts, verts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPointsGeneralPolygons (RtInt npolys, RtInt nloops[], 
					   RtInt nverts[], RtInt verts[], ...)
{
  try {
    va_list args;
    va_start(args, verts);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiPointsGeneralPolygonsV( npolys, nloops, nverts, verts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPointsGeneralPolygonsV (RtInt npolys, RtInt nloops[], 
					    RtInt nverts[], RtInt verts[], 
					    RtInt n, RtToken tokens[], 
					    RtPointer parms[])
{
  try {
    context.current().RiPointsGeneralPolygonsV( npolys, nloops, nverts,
				       verts, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiBasis (RtBasis ubasis, RtInt ustep, 
			   RtBasis vbasis, RtInt vstep)
{
  try {
    context.current().RiBasis( ubasis,  ustep,  vbasis,  vstep);
    context.current().Basis( ustep, vstep);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPatch (RtToken type, ...)
{
  try {
    va_list args;
    va_start(args, type);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiPatchV( type, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPatchV (RtToken type, RtInt n, 
			    RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiPatchV( type,  n,  tokens,  parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPatchMesh (RtToken type, RtInt nu, RtToken uwrap,
			       RtInt nv, RtToken vwrap, ...)
{
  try {
    va_list args;
    va_start(args, vwrap);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiPatchMeshV( type,  nu,  uwrap, nv, vwrap, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiPatchMeshV (RtToken type, RtInt nu, RtToken uwrap,
				RtInt nv, RtToken vwrap, RtInt n, 
				RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiPatchMeshV(type, nu, uwrap, nv, vwrap, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiNuPatch (RtInt nu, RtInt uorder, 
			     RtFloat uknot[], RtFloat umin,
			     RtFloat umax, RtInt nv, 
			     RtInt vorder, RtFloat vknot[],
			     RtFloat vmin, RtFloat vmax, ...)
{
  try {
    va_list args;
    va_start(args, vmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiNuPatchV(nu,  uorder,  uknot,  umin,
	       umax,  nv,  vorder,  vknot,
	       vmin,  vmax, n,  tokens,  parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiNuPatchV (RtInt nu, RtInt uorder, 
			      RtFloat uknot[], RtFloat umin,
			      RtFloat umax, RtInt nv, 
			      RtInt vorder, RtFloat vknot[],
			      RtFloat vmin, RtFloat vmax,
			      RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiNuPatchV(nu,  uorder,  uknot,  umin,
				 umax,  nv,  vorder,  vknot,
				 vmin,  vmax, n,  tokens,  parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTrimCurve (RtInt nloops, RtInt ncurves[], RtInt order[],
			       RtFloat knot[], RtFloat min[], RtFloat max[], 
			       RtInt n[], RtFloat u[], RtFloat v[], RtFloat w[])
{
  try {
    context.current().RiTrimCurve( nloops, ncurves, order,
				   knot,  min,  max,  n,
				   u, v, w);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSphere (RtFloat radius, RtFloat zmin, 
			    RtFloat zmax, RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiSphereV( radius, zmin, zmax,  tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSphereV (RtFloat radius, RtFloat zmin, RtFloat zmax, 
			     RtFloat tmax, RtInt n, RtToken tokens[], 
			     RtPointer parms[])
{  
  try {
    context.current().RiSphereV( radius, zmin, zmax, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiCone (RtFloat height, RtFloat radius, RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiConeV( height, radius,  tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiConeV (RtFloat height, RtFloat radius, RtFloat tmax,
			   RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiConeV( height,  radius,  tmax, n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiCylinder (RtFloat radius, RtFloat zmin, 
			      RtFloat zmax, RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiCylinderV( radius, zmin, zmax, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiCylinderV (RtFloat radius, RtFloat zmin, 
			       RtFloat zmax, RtFloat tmax,
			       RtInt n, RtToken tokens[], 
			       RtPointer parms[])
{
  try {
    context.current().RiCylinderV( radius, zmin, zmax, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiHyperboloid (RtPoint point1, RtPoint point2, 
				 RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiHyperboloidV( point1,  point2, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiHyperboloidV (RtPoint point1, RtPoint point2, 
				  RtFloat tmax, RtInt n, RtToken tokens[], 
				  RtPointer parms[])
{
  try {
    context.current().RiHyperboloidV( point1,  point2, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiParaboloid (RtFloat rmax, RtFloat zmin, RtFloat zmax, 
				RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiParaboloidV( rmax, zmin, zmax, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiParaboloidV (RtFloat rmax, RtFloat zmin, 
				 RtFloat zmax, RtFloat tmax,
				 RtInt n, RtToken tokens[], 
				 RtPointer parms[])
{
  try {
    context.current().RiParaboloidV( rmax, zmin, zmax, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDisk (RtFloat height, RtFloat radius, RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);

    RiDiskV( height, radius, tmax, n, tokens, parms);

    va_end(args);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiDiskV (RtFloat height, RtFloat radius, RtFloat tmax,
			   RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiDiskV( height,  radius,  tmax, n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTorus (RtFloat majrad, RtFloat minrad, 
			   RtFloat phimin, RtFloat phimax,
			   RtFloat tmax, ...)
{
  try {
    va_list args;
    va_start(args, tmax);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiTorusV( majrad, minrad, phimin, phimax, tmax, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiTorusV (RtFloat majrad, RtFloat minrad, RtFloat phimin,
			    RtFloat phimax, RtFloat tmax, RtInt n, 
			    RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiTorusV( majrad, minrad, phimin, phimax,
				tmax,  n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiBlobby (RtInt nleaf, RtInt ncode, RtInt code[],
			    RtInt nflt, RtFloat flt[],
			    RtInt nstr, RtToken str[], ...)
{
  try {
    va_list args;
    va_start(args, str);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiBlobbyV(nleaf, ncode, code, nflt, flt, nstr, str, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiBlobbyV (RtInt nleaf, RtInt ncode, RtInt code[],
			     RtInt nflt, RtFloat flt[],
			     RtInt nstr, RtToken str[], 
			     RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiBlobbyV(nleaf, ncode, code, nflt, flt,
				nstr, str, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}

extern "C" RtVoid RiPoints(RtInt npoints, ...)
{
  try {
    va_list args;
    va_start(args, npoints);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);
   
    RiPointsV(npoints, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}

extern "C" RtVoid RiPointsV(RtInt npoints,
			    RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiPointsV(npoints, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}

extern "C" RtVoid RiCurves (RtToken type, RtInt ncurves,
			    RtInt nvertices[], RtToken wrap, ...)
{
  try {
    va_list args;
    va_start(args, wrap);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);
   
    RiCurvesV(type, ncurves, nvertices, wrap, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid  RiCurvesV (RtToken type, RtInt ncurves,
			      RtInt nvertices[], RtToken wrap,
			      RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiCurvesV(type, ncurves, nvertices, wrap, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}

extern "C" RtVoid  RiSubdivisionMesh (RtToken mask, RtInt nf, RtInt nverts[],
				      RtInt verts[],
				      RtInt ntags, RtToken tags[], RtInt numargs[],
				      RtInt intargs[], RtFloat floatargs[], ...)
{
  try {
    va_list args;
    va_start(args, floatargs);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);
 
    RiSubdivisionMeshV(mask, nf, nverts, verts, ntags, tags, numargs,
		       intargs, floatargs, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSubdivisionMeshV (RtToken mask, RtInt nf, RtInt nverts[],
				      RtInt verts[],
				      RtInt ntags, RtToken tags[], RtInt numargs[],
				      RtInt intargs[], RtFloat floatargs[],
				      RtInt n, RtToken tokens[], RtPointer parms[])
{
  try {
    context.current().RiSubdivisionMeshV(mask, nf, nverts, verts,
					 ntags, tags, numargs,intargs, floatargs,
					 n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}
 
extern "C" RtVoid RiProcedural (RtPointer data, RtBound bound,
				RtVoid (*subdivfunc)(RtPointer, RtFloat),
				RtVoid (*freefunc)(RtPointer))
{
  try {  
    context.current().RiProcedural( data,  bound, subdivfunc, freefunc );
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}
extern "C" RtVoid RiGeometry (RtToken type, ...)
{
  try {
    va_list args;
    va_start(args, type);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);
    
    RiGeometryV(type, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiGeometryV (RtToken type, RtInt n, RtToken tokens[], 
			       RtPointer parms[])
{
  try {
    context.current().RiGeometryV(type, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* Solid */
extern "C" RtVoid RiSolidBegin (RtToken operation)
{
  try {
    context.current().RiSolidBegin( operation);
    context.current().push();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiSolidEnd (RtVoid)
{
  try {
    context.current().RiSolidEnd();
    context.current().pop();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Retained geometry ======= */
extern "C" RtObjectHandle RiObjectBegin (RtVoid)
{
  try {
    context.current().RiObjectBegin();
    context.current().push();
    return context.current().ObjectBegin();
  } catch (GMANError &error) {
    GMANHandleError(error);
    return RI_NULL;
  }
};

extern "C" RtVoid RiObjectEnd (RtVoid)
{
  try {
    context.current().RiObjectEnd();
    context.current().pop();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiObjectInstance (RtObjectHandle handle)
{
  try {
    context.current().RiObjectInstance(handle);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Motion ======= */
extern "C" RtVoid RiMotionBegin (RtInt n, ...)
{
  try {
    va_list args;
    va_start(args, n);
    RtFloat times[n];

    for (int i=0;i<n;i++) {
      // some compilers promote RtFloat to double for va_arg
      // therefore ask for double and not RtFloat
      times[i]=va_arg(args, double);
      //times[i]=va_arg(args, RtFloat);
    }
    va_end(args);

    RiMotionBeginV(n, times);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMotionBeginV (RtInt n, RtFloat times[])
{
  try {
    context.current().RiMotionBeginV(n, times);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMotionEnd (RtVoid) {
  try {
    context.current().RiMotionEnd();
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Texture ======= */
extern "C" RtVoid RiMakeTexture (char *pic, char *tex, 
				 RtToken swrap, RtToken twrap,
				 RtFilterFunc filterfunc, 
				 RtFloat swidth, RtFloat twidth, ...)
{
  try {
    va_list args;
    va_start(args, twidth);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiMakeTextureV(pic, tex, swrap, twrap,
			    filterfunc, swidth, twidth, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeTextureV (char *pic, char *tex, 
				  RtToken swrap, RtToken twrap,
				  RtFilterFunc filterfunc, 
				  RtFloat swidth, RtFloat twidth,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{  
  try {
    context.current().RiMakeTextureV( pic, tex, swrap,  twrap, filterfunc,  swidth, 
				      twidth, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeLatLongEnvironment (char *pic, char *tex, 
					    RtFilterFunc filterfunc,
					    RtFloat swidth, 
					    RtFloat twidth, ...)
{
  try {
    va_list args;
    va_start(args, twidth);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiMakeLatLongEnvironmentV(pic, tex, filterfunc, 
			      swidth, twidth, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeLatLongEnvironmentV (char *pic, char *tex, 
					     RtFilterFunc filterfunc,
					     RtFloat swidth, RtFloat twidth,
					     RtInt n, RtToken tokens[], 
					     RtPointer parms[])
{
  try {
    context.current().RiMakeLatLongEnvironmentV(pic, tex, filterfunc,
						swidth, twidth,
						n,  tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeCubeFaceEnvironment (char *px, char *nx, 
					     char *py, char *ny,
					     char *pz, char *nz, 
					     char *tex, RtFloat fov,
					     RtFilterFunc filterfunc, 
					     RtFloat swidth, 
					     RtFloat ywidth, ...)
{
  try {
    va_list args;
    va_start(args, ywidth);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiMakeCubeFaceEnvironmentV(px, nx, py, ny, pz, nz, tex, fov,
			       filterfunc, swidth, ywidth, n,
			       tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeCubeFaceEnvironmentV (char *px, char *nx, 
					      char *py, char *ny,
					      char *pz, char *nz, 
					      char *tex, RtFloat fov,
					      RtFilterFunc filterfunc, 
					      RtFloat swidth, RtFloat ywidth,
					      RtInt n, RtToken tokens[], 
					      RtPointer parms[])
{
  try {
    context.current().RiMakeCubeFaceEnvironmentV(px, nx, py, ny, pz, nz, tex, fov,
						 filterfunc, swidth, ywidth,
						 n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeShadow (char *pic, char *tex, ...)
{
  try {
    va_list args;
    va_start(args, tex);
    RtInt n=GMANCountArguments(args);
    RtToken tokens[n];
    RtPointer parms[n];
  
    GMANGetArguments(args, n, tokens, parms);
    va_end(args);

    RiMakeShadowV(pic, tex, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

extern "C" RtVoid RiMakeShadowV (char *pic, char *tex,
				 RtInt n, RtToken tokens[], 
				 RtPointer parms[])
{
  try {
    context.current().RiMakeShadowV(pic, tex, n, tokens, parms);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
};

/* ======= Error handler ======= */
extern "C" RtVoid RiErrorHandler (RtErrorHandler handler)
{
  try {
    context.current().RiErrorHandler(handler);
  } catch (GMANError &error) {
    GMANHandleError(error);
  }
}
