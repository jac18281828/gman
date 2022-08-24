/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
 * LJL -- Added calls to graphic state manager
 */

/*
 * JAC -- REMOVED THEM!
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

#include <string>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanrenderman.h" /* Declaration Header */

#include "gmanrenderer.h"  
#include "gmanloadablerenderer.h"

#include "gmanbspworldmanager.h"
#include "gmanlinearworldmanager.h"

#include "gmanviewingsystem.h"
#include "gmanvsorthographic.h"
#include "gmanvsperspective.h"

#include "gmanoutputpnm.h"
#include "gmanoutputpng.h"
#include "gmanoutputtiff.h"
#include "gmanoutputx11.h"
#include "gmanoutputwin32.h"
#include "gmaninlineparse.h"
#include "gmanvector.h"
#include "gmanmath.h"

/*
 * RenderMan API RiRenderMan
 *
 */

// default constructor
GMANRenderMan::GMANRenderMan() : GMANGraphicState() 
{
  viewingSystem = NULL;
};

// default destructor 
GMANRenderMan::~GMANRenderMan() 
{
};


// declare shading language variable
RtToken GMANRenderMan::RiDeclare(const char *name, const char *declaration) 
{
  GMANInlineParse ip;
  string a(name);
  string b(declaration);

  b+=" ";
  b+=a;
  ip.parse(b);
  dictionary.addToken(ip.getIdentifier(),ip.getClass(),ip.getType(),ip.getQuantity(),false);
  return const_cast<char *> (name);
}


/* RenderMan state machine */

// begin new renderman rendering instance
RtVoid GMANRenderMan::RiBegin(RtToken name)
{
  
  if(name == NULL) {
    name = "gmanzbuffer";
  }
  
  //
  // Load the renderer from a shared object by name
  string objectName = "lib";
  objectName += name;
#ifdef WIN32
  objectName += ".dll";
#else
  objectName += ".so";
#endif
  GMANLoadableRenderer* loadedRenderer =
      new GMANLoadableRenderer(objectName.c_str());
  renderer = loadedRenderer;
  worldManager = renderer->getWorldManager();
  
  objectManager= renderer->getObjectManager();
}

RtVoid GMANRenderMan::RiEnd(RtVoid)
{
  delete renderer;
  //  delete worldManager;
  //delete objectManager;
  if (viewingSystem)
    delete viewingSystem;
}

RtVoid GMANRenderMan::RiFrameBegin(RtInt frame)
{
  enterMode(F);
}

RtVoid GMANRenderMan::RiFrameEnd(RtVoid)
{
  leaveMode(F);
}

RtVoid GMANRenderMan::RiWorldBegin(RtVoid)
{
  enterMode(W);


  // FIXME FIXME FIXME
  // this order is messed up!!!
  GMANOptions::RasterInfo ri = getOptions().getRasterInfo();


  if(getOptions().getDisplay().type == "file") {
    size_t startExt = getOptions().getDisplay().name.rfind(".");
    string ext;
    if(startExt != string::npos) {
      ext = getOptions().getDisplay().name.substr(startExt+1);
    }
    debug("Displaying to file with extension, %s.", ext.c_str());
    
    if((ext == "tif") || (ext == "tiff")) {
      
      output = new GMANOutputTIFF( getOptions().
				   getDisplay().name.c_str(),
				   ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				   ri.rymax-ri.rymin+1);
    } else if (ext == "png") {
      
      output = new GMANOutputPNG( getOptions().
				  getDisplay().name.c_str(),
				  ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				  ri.rymax-ri.rymin+1);
      
      
    } else if (ext == "pnm") {
      
      output = new GMANOutputPNM( getOptions().
				  getDisplay().name.c_str(),
				  ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				  ri.rymax-ri.rymin+1);
    }
    
  } else {  // type == framebuffer
    // our frame buffer
#ifdef WIN32
    output = new GMANOutputWin32( getOptions().
				  getDisplay().name.c_str(),
				  ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				  ri.rymax-ri.rymin+1);
#else
    output = new GMANOutputX11( getOptions().
				getDisplay().name.c_str(),
				ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				ri.rymax-ri.rymin+1);
#endif
  }


  // the default projection is orthographic
  const GMANOptions::ScreenWindowStruct sw=
    getOptions().getScreenWindow();
  GMANOptions::ClippingStruct cw=getOptions().getClipping();
  const GMANOptions::ProjectionStruct &ps = getOptions().getProjection();

  GMANTokenId fovTok = dictionary.getTokenId(RI_FOV);
  RtFloat* param = (RtFloat *) ps.pl.getPointer(fovTok);
  RtFloat fov=0.0;
  if (param) {
    RtFloat fov = param[0];
  }
  if (fov == 0.0) {
    warning("FOV not set, defaulting to 90.0.");
    // token not found
    fov=90.0;
  }

  if(ps.name == "orthographic") {
    viewingSystem=new GMANVSOrthographic(ri.xres, 
					 ri.yres, 
					 sw, 
					 cw.nearDist, 
					 cw.farDist);
  } else {
    viewingSystem=new GMANVSPerspective(ri.xres, 
					ri.yres, 
					sw, 
					fov,
					cw.nearDist, 
					cw.farDist);
  }
}

RtVoid GMANRenderMan::RiWorldEnd(RtVoid)
{
  const GMANOptions::ExposureStruct &exposure = getOptions().getExposure();
  renderer->render(output, 
		   viewingSystem, 
		   getOptions(),
		   getAttributes());
  
  GMANOutput::DisplayMode dMode;

  if(getOptions().getDisplay().mode == RI_RGB) {
      dMode = GMANOutput::RGB; 
      output->setQuantization(dMode, 
			      getOptions().getColorQuantize().one, 
			      getOptions().getColorQuantize().min, 
			      getOptions().getColorQuantize().max, 
			      getOptions().getColorQuantize().ditheramplitude); 
  } else if(getOptions().getDisplay().mode == RI_RGBA) {
      dMode = GMANOutput::RGBA;
      output->setQuantization(dMode, 
			      getOptions().getColorQuantize().one, 
			      getOptions().getColorQuantize().min, 
			      getOptions().getColorQuantize().max, 
			      getOptions().getColorQuantize().ditheramplitude); 

  } else if(getOptions().getDisplay().mode == RI_RGBAZ) {
      dMode = GMANOutput::RGBAZ;
      output->setQuantization(dMode, 
			      getOptions().getColorQuantize().one, 
			      getOptions().getColorQuantize().min, 
			      getOptions().getColorQuantize().max, 
			      getOptions().getColorQuantize().ditheramplitude); 
      
  } else if(getOptions().getDisplay().mode == RI_A) {
      dMode = GMANOutput::A;      
      output->setQuantization(dMode, 
			      getOptions().getColorQuantize().one, 
			      getOptions().getColorQuantize().min, 
			      getOptions().getColorQuantize().max, 
			      getOptions().getColorQuantize().ditheramplitude); 

  } else if(getOptions().getDisplay().mode == RI_AZ) {
      dMode = GMANOutput::AZ; 
      output->setQuantization(dMode, 
			      getOptions().getColorQuantize().one, 
			      getOptions().getColorQuantize().min, 
			      getOptions().getColorQuantize().max, 
			      getOptions().getColorQuantize().ditheramplitude); 
     
  } else if(getOptions().getDisplay().mode == RI_Z) {
      dMode = GMANOutput::Z;      
      output->setQuantization(dMode, 
			      getOptions().getDepthQuantize().one, 
			      getOptions().getDepthQuantize().min, 
			      getOptions().getDepthQuantize().max, 
			      getOptions().getDepthQuantize().ditheramplitude); 
  } else {
      throw GMANError(RIE_ILLSTATE, RIE_ERROR, 
		      "Invalid token passed for display mode.");
  }
  
  // renderer must be copied to support depth
  if(dMode != GMANOutput::RGBA) {
      warning("This display mode, %s, may "
	      "not be fully supported at this time.",
	      getOptions().getDisplay().mode.c_str());
  }
	      
  output->save(dMode, exposure.gain, exposure.gamma);



  delete output;
  output = NULL;
  
  leaveMode(W);
}

// create a new object
RtObjectHandle GMANRenderMan::RiObjectBegin(RtVoid)
{
  enterMode(O);
  //return GetObjectManager()->Create()->GetHandle();
  return (RtObjectHandle) 0;
}

// commit the most recently created object
RtVoid GMANRenderMan::RiObjectEnd(RtVoid)
{
  leaveMode(O);
  //objectManagerStack.top().Commit();
}

RtVoid  GMANRenderMan::RiObjectInstance(RtObjectHandle handle)
{
  allowed(cmdObjectInstance);
}

RtVoid  GMANRenderMan::RiAttributeBegin(RtVoid)
{
  enterMode(A);
}
RtVoid  GMANRenderMan::RiAttributeEnd(RtVoid)
{
  leaveMode(A);
}

RtVoid  GMANRenderMan::RiTransformBegin(RtVoid)
{
  enterMode(T);
}
RtVoid  GMANRenderMan::RiTransformEnd(RtVoid)
{
  leaveMode(T);
}

RtVoid  GMANRenderMan::RiSolidBegin(RtToken operation)
{
  enterMode(S);
}
RtVoid  GMANRenderMan::RiSolidEnd(RtVoid)
{
  leaveMode(S);

}

RtVoid  GMANRenderMan::RiMotionBeginV(RtInt n, RtFloat times[])
{
  enterMotion(n,times);
}
RtVoid  GMANRenderMan::RiMotionEnd(RtVoid)
{
  leaveMode(M);
}


// **************************************************************
// ******* ******* ******* CAMERA OPTIONS ******* ******* *******
// **************************************************************
RtVoid  GMANRenderMan::RiFormat (RtInt xres, RtInt yres, RtFloat aspect)
{
  allowed(cmdFormat);
  getOptions().setFormat (xres, yres, aspect); 
}
RtVoid  GMANRenderMan::RiFrameAspectRatio (RtFloat aspect)
{
  allowed(cmdFrameAspectRatio);
  getOptions().setFrameAspectRatio (aspect);
}
RtVoid  GMANRenderMan::RiScreenWindow (RtFloat left, RtFloat right, RtFloat bottom, RtFloat top)
{
  allowed(cmdScreenWindow);
  getOptions().setScreenWindow (left, right, bottom, top);
}
RtVoid  GMANRenderMan::RiCropWindow (RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax)
{
  allowed(cmdCropWindow);
  getOptions().setCropWindow(xmin, xmax, ymin, ymax);
}
RtVoid  GMANRenderMan::RiProjectionV (RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdProjection);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setProjection (name, p);
}
RtVoid  GMANRenderMan::RiClipping(RtFloat hither, RtFloat yon)
{
  allowed(cmdClipping);
  getOptions().setClipping(hither, yon);
}
RtVoid  GMANRenderMan::RiDepthOfField (RtFloat fstop, RtFloat focallength, RtFloat focaldistance)
{
  allowed(cmdDepthOfField);
  getOptions().setDepthOfField(fstop, focallength, focaldistance);
}
RtVoid  GMANRenderMan::RiShutter(RtFloat min, RtFloat max)
{
  allowed(cmdShutter);
  getOptions().setShutter(min, max);
}

// ***************************************************************
// ******* ******* ******* DISPLAY OPTIONS ******* ******* *******
// ***************************************************************
RtVoid  GMANRenderMan::RiPixelVariance(RtFloat variation)
{
  allowed(cmdPixelVariance);
  getOptions().setPixelVariance(variation);
}
RtVoid  GMANRenderMan::RiPixelSamples(RtFloat xsamples, RtFloat ysamples)
{ 
  allowed(cmdPixelSamples);
  if (xsamples<1.0) {xsamples=1.0;}
  if (ysamples<1.0) {ysamples=1.0;}
  getOptions().setPixelSamples(xsamples, ysamples);
}
RtVoid  GMANRenderMan::RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth)
{
  allowed(cmdPixelFilter);
  getOptions().setPixelFilter(filterfunc, xwidth, ywidth);
}
RtVoid  GMANRenderMan::RiExposure(RtFloat gain, RtFloat gamma)
{
  allowed(cmdExposure);
  getOptions().setExposure(gain, gamma);
}
RtVoid  GMANRenderMan::RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdImager);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setImager(name, p, *renderer);
}
RtVoid  GMANRenderMan::RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl)
{
  allowed(cmdQuantize);
  if (type==RI_RGBA) {
    getOptions().setColorQuantize(one, min, max, ampl);
    return;
  } else if (type==RI_Z) {
    getOptions().setDepthQuantize(one, min, max, ampl);
    return;
  }
  GMANError error(RIE_UNIMPLEMENT, RIE_WARNING, "Unknown quantizer type");
  throw error;
}
RtVoid  GMANRenderMan::RiDisplayV(char *name, RtToken type, RtToken mode,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdDisplay);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setDisplay (name, type, mode, p);
}

// ******************************************************************
// ******* ******* ******* ADDITIONAL OPTIONS ******* ******* *******
// ******************************************************************
RtVoid  GMANRenderMan::RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdHider);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setHider(type, p);
}
RtVoid  GMANRenderMan::RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[])
{
  allowed(cmdColorSamples);
  getOptions().setColorSamples(n, nRGB, RGBn);
}
RtVoid  GMANRenderMan::RiRelativeDetail(RtFloat relativedetail)
{
  allowed(cmdRelativeDetail);
  getOptions().setRelativeDetail(relativedetail);
}
RtVoid  GMANRenderMan::RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
}






// ******************************************************************
// ******* ******* ******* SHADING ATTRIBUTES ******* ******* *******
// ******************************************************************
RtVoid  GMANRenderMan::RiColor(RtColor color)
{
  allowed(cmdColor);
  getAttributes().setColor(color);
}
RtVoid  GMANRenderMan::RiOpacity(RtColor color)
{
  allowed(cmdOpacity);
  getAttributes().setOpacity(color);
}
RtVoid  GMANRenderMan::RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
					    RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4)
{
  allowed(cmdTextureCoordinates);
  getAttributes().setTextureCoordinates(s1, t1, s2, t2, s3, t3, s4, t4);
}
RtLightHandle GMANRenderMan::RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdLightSource);
  return (RtLightHandle) 0;
}
RtLightHandle GMANRenderMan::RiAreaLightSourceV(RtToken name,
						RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdAreaLightSource);
  return (RtLightHandle) 0;
}
RtVoid  GMANRenderMan::RiIlluminate(RtLightHandle light, RtBoolean onoff)
{
  allowed(cmdIlluminate);
  getAttributes().setIlluminate(light, onoff);
}
RtVoid  GMANRenderMan::RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdSurface);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setSurface(name, p, *renderer);
}
RtVoid  GMANRenderMan::RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdAtmosphere);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setAtmosphere(name, p, *renderer);
}
RtVoid  GMANRenderMan::RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdInterior);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setInterior(name, p, *renderer);
}
RtVoid  GMANRenderMan::RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdExterior);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setExterior(name, p, *renderer);
}
RtVoid  GMANRenderMan::RiShadingRate(RtFloat size)
{
  allowed(cmdShadingRate);
  getAttributes().setShadingRate(size);
}
RtVoid  GMANRenderMan::RiShadingInterpolation(RtToken type)
{
  allowed(cmdShadingInterpolation);
  if (strcmp(type, RI_CONSTANT) && strcmp(type, RI_SMOOTH)) {
    GMANError error(RIE_UNIMPLEMENT,RIE_WARNING,
		    "Unknown shading interpolation type");
    throw error;
  }
  getAttributes().setShadingInterpolation (type);
}
RtVoid  GMANRenderMan::RiMatte(RtBoolean onoff)
{
  allowed(cmdMatte);
  getAttributes().setMatte(onoff);
}

// *******************************************************************
// ******* ******* ******* GEOMETRY ATTRIBUTES ******* ******* *******
// *******************************************************************
RtVoid  GMANRenderMan::RiBound(RtBound b)
{
  allowed(cmdBound);
  getAttributes().setBound(b);
}
RtVoid  GMANRenderMan::RiDetail(RtBound d)
{
  allowed(cmdDetail);
  getAttributes().setDetail(d);
}
RtVoid  GMANRenderMan::RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis)
{
  allowed(cmdDetailRange);
  getAttributes().setDetailRange(minvis, lowtran, uptran, maxvis);
}
RtVoid  GMANRenderMan::RiGeometricApproximation(RtToken type, RtFloat value)
{
  allowed(cmdGeometricApproximation);
  if (type!=RI_FLATNESS) {
    GMANError error(RIE_UNIMPLEMENT,RIE_WARNING,"Unknown geometric approximation type");
    throw error;
  }
  getAttributes().setGeometricApproximation (type, value);
}
RtVoid  GMANRenderMan::RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep)
{
  allowed(cmdBasis);
  getAttributes().setUVBasis(ubasis, ustep, vbasis, vstep);
}
RtVoid  GMANRenderMan::RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[],
				   RtFloat knot[], RtFloat min[], RtFloat max[], RtInt n[],
				   RtFloat u[], RtFloat v[], RtFloat w[])
{
  allowed(cmdTrimCurve);
  GMANTrimCurve tc(nloops, ncurves, order, knot, min, max, n, u, v, w);
  getAttributes().setTrimCurves (tc);
}
RtVoid  GMANRenderMan::RiOrientation(RtToken o)
{
  allowed(cmdOrientation);
  if (strcmp(o, RI_INSIDE) && strcmp(o, RI_OUTSIDE) &&
      strcmp(o, RI_LH) && strcmp(o, RI_RH)) {
    GMANError error(RIE_UNIMPLEMENT,RIE_WARNING,"Unknown orientation type");
    throw error;
  }
  getAttributes().setOrientation (o);
}

RtVoid  GMANRenderMan::RiReverseOrientation(RtVoid)
{
  allowed(cmdReverseOrientation);
  getAttributes().toggleOrientation();
}

RtVoid  GMANRenderMan::RiSides(RtInt sides)
{
  allowed(cmdSides);
  getAttributes().setSides(sides);
}
RtVoid  GMANRenderMan::RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdDisplacement);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setDisplacement (name, p, *renderer);
}


// ***************************************************************
// ******* ******* ******* TRANSFORMATIONS ******* ******* *******
// ***************************************************************
RtVoid  GMANRenderMan::RiIdentity(RtVoid)
{
  allowed(cmdIdentity);
  GMANMatrix4 m;
  setTransform(m);
}
RtVoid  GMANRenderMan::RiTransform(RtMatrix transform)
{
  allowed(cmdTransform);
  GMANMatrix4 m(transform);
  setTransform(m);
}
RtVoid  GMANRenderMan::RiConcatTransform(RtMatrix transform)
{
  allowed(cmdConcatTransform);
  GMANMatrix4 m(transform);
  buildTransform(m);
}
RtVoid  GMANRenderMan::RiPerspective(RtFloat fov)
{
  allowed(cmdPerspective);
  GMANMatrix4 m;
  m.persp(fov);
  buildTransform(m);
}
RtVoid  GMANRenderMan::RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz)
{
  allowed(cmdTranslate);
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  buildTransform(m);
}
RtVoid  GMANRenderMan::RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)
{
  allowed(cmdRotate);
  GMANMatrix4 m;
  m.rot(angle*DEGTORAD, dx, dy, dz);
  buildTransform(m);
}
RtVoid  GMANRenderMan::RiScale(RtFloat sx, RtFloat sy, RtFloat sz)
{
  allowed(cmdScale);
  GMANMatrix4 m;
  m.scale(sx, sy, sz);
  buildTransform(m);
}
RtVoid  GMANRenderMan::RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
			      RtFloat dx2, RtFloat dy2, RtFloat dz2)
{
  allowed(cmdSkew);
  GMANMatrix4 m;
  GMANVector a(dx1,dy1,dz1);
  GMANVector b(dx2,dy2,dz2);
  m.skew (angle, a, b);
  buildTransform(m);
}
RtVoid  GMANRenderMan::RiDeformationV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{}
RtVoid  GMANRenderMan::RiCoordinateSystem(RtToken space)
{
  allowed(cmdCoordinateSystem);
}
RtVoid  GMANRenderMan::RiCoordSysTransform(RtToken space)
{
  allowed(cmdCoordSysTransform);
}

RtPoint *GMANRenderMan::RiTransformPoints(RtToken fromspace, RtToken tospace, RtInt n,
					  RtPoint points[])
{
  allowed(cmdTransformPoints);
  return (RtPoint *) 0;
}

// AttributeV
RtVoid GMANRenderMan::RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{}





// **********************************************************
// ******* ******* ******* PRIMITIVES ******* ******* *******
// **********************************************************
RtVoid  GMANRenderMan::RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdPolygon);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSPolygon( nverts,
				      paramList,
				      &(getOptions()), 
				      &(getAttributes()),
				      transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n,
					 RtToken tokens[], RtPointer parms[])
{
  allowed(cmdGeneralPolygon);
}
RtVoid  GMANRenderMan::RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[],  RtInt n,
					 RtToken tokens[], RtPointer parms[])
{
  allowed(cmdPointsPolygon);
}
RtVoid  GMANRenderMan::RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[],
						RtInt verts[], RtInt n, RtToken tokens[], 
						RtPointer parms[])
{
  allowed(cmdPointsGeneralPolygons);
}
RtVoid  GMANRenderMan::RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdPatch);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSPatch( type,
				    paramList,
				    &(getOptions()), 
				    &(getAttributes()),
				    transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap,
				    RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[], 
				    RtPointer parms[])
{
  allowed(cmdPatchMesh);
}
RtVoid  GMANRenderMan::RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin,
				  RtFloat umax, RtInt nv, RtInt vorder, RtFloat vknot[],
				  RtFloat vmin, RtFloat vmax,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdNuPatch);
}
  
RtVoid  GMANRenderMan::RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
				 RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdSphere);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSSphere( radius, zmin, zmax, tmax, paramList,
				     &(getOptions()), 
				     &(getAttributes()),
				     transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
			       RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdCone);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSCone( height, radius, tmax, paramList,
				   &(getOptions()), 
				   &(getAttributes()),
				   transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
				   RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdCylinder);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSCylinder( radius, zmin, zmax, tmax, paramList,
				       &(getOptions()), 
				       &(getAttributes()),
				       transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
				      RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdHyperboloid);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSHyperboloid( point1, point2, tmax, paramList,
					  &(getOptions()), 
					  &(getAttributes()),
					  transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
				     RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdParaboloid);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSParaboloid( rmax, zmin, zmax, tmax, paramList,
					 &(getOptions()), 
					 &(getAttributes()),
					 transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderMan::RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
			       RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdDisk);
}
RtVoid  GMANRenderMan::RiTorusV(RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
				RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdTorus);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSTorus( majrad, minrad, phimin, phimax, tmax,
				    paramList,
				    &(getOptions()), 
				    &(getAttributes()),
				    transform);
  worldManager->add(prim);
  delete transform;
}
  
RtVoid  GMANRenderMan::RiBlobbyV(RtInt nleaf, RtInt ncode, RtInt code[],
				 RtInt nflt, RtFloat flt[],
				 RtInt nstr, RtToken str[], 
				 RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdBlobby);
}
RtVoid  GMANRenderMan::RiPointsV(RtInt npoints,
				 RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdPoints);
}
RtVoid  GMANRenderMan::RiCurvesV(RtToken type, RtInt ncurves,
				 RtInt nvertices[], RtToken wrap,
				 RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdCurves);
}
RtVoid  GMANRenderMan::RiSubdivisionMeshV(RtToken mask, RtInt nf, RtInt nverts[],
					  RtInt verts[],
					  RtInt ntags, RtToken tags[], RtInt numargs[],
					  RtInt intargs[], RtFloat floatargs[],
					  RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdSubdivisionMesh);
}

RtVoid  GMANRenderMan::RiProcedural(RtPointer data, RtBound bound,
				    RtVoid (*subdivfunc)(RtPointer, RtFloat),
				    RtVoid (*freefunc)(RtPointer))
{
  allowed(cmdProcedural);
}
RtVoid  GMANRenderMan::RiGeometryV(RtToken type, RtInt n, RtToken tokens[], 
				   RtPointer parms[])
{
  allowed(cmdGeometry);
}

// ****************************************************
// ******* ******* ******* MISC ******* ******* *******
// ****************************************************
RtVoid  GMANRenderMan::RiMakeTextureV(char *pic, char *tex, RtToken swrap, RtToken twrap,
				      RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
				      RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdMakeTexture);
}
RtVoid  GMANRenderMan::RiMakeBumpV(char *pic, char *tex, RtToken swrap, RtToken twrap,
				   RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
				   RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdMakeBump);
}
RtVoid  GMANRenderMan::RiMakeLatLongEnvironmentV(char *pic, char *tex, RtFilterFunc filterfunc,
				  RtFloat swidth, RtFloat twidth,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdMakeLatLongEnvironment);
}
RtVoid  GMANRenderMan::RiMakeCubeFaceEnvironmentV(char *px, char *nx, char *py, char *ny,
				   char *pz, char *nz, char *tex, RtFloat fov,
				   RtFilterFunc filterfunc, RtFloat swidth, 
				   RtFloat ywidth,
				   RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdMakeCubeFaceEnvironment);
}
RtVoid  GMANRenderMan::RiMakeShadowV(char *pic, char *tex,
				     RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdMakeShadow);
}

