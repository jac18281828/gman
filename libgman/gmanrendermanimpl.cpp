/* SPDX-License-Identifier: LGPL-2.1-or-later */

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

#include <cstring>
#include <memory>
#include <string>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanrendermanimpl.h" /* Declaration Header */

#include "gmanrenderer.h"  
#include "gmanloadablerenderer.h"

#include "gmanlinearworldmanager.h"

#include "gmanviewingsystem.h"
#include "gmanvsorthographic.h"
#include "gmanvsperspective.h"

#include "gmanoutputpnm.h"
#include "gmanoutputpng.h"
#include "gmanoutputtiff.h"
#include "gmanoutputx11.h"
#include "gmaninlineparse.h"
#include "gmanvector.h"
#include "gmanmath.h"

/*
 * RenderMan API RiRenderMan
 *
 */

// default constructor
GMANRenderManImpl::GMANRenderManImpl() : GMANGraphicState() 
{
  viewingSystem = NULL;
};

// default destructor 
GMANRenderManImpl::~GMANRenderManImpl() 
{
};


// declare shading language variable
RtToken GMANRenderManImpl::RiDeclare(const char *name, const char *declaration) 
{
  GMANInlineParse ip;
  std::string a(name);
  std::string b(declaration);

  b+=" ";
  b+=a;
  ip.parse(b);
  dictionary.addToken(ip.getIdentifier(),ip.getClass(),ip.getType(),ip.getQuantity(),false);
  return const_cast<char *> (name);
}


/* RenderMan state machine */

// begin new renderman rendering instance
RtVoid GMANRenderManImpl::RiBegin(RtToken name)
{
  
  if(name == NULL) {
    name = "gmanzbuffer";
  }
  
  //
  // Load the renderer from a shared object by name
  std::string objectName = "lib";
  objectName += name;
  objectName += ".so";
  GMANLoadableRenderer* loadedRenderer =
      new GMANLoadableRenderer(objectName.c_str());
  renderer = loadedRenderer;
  worldManager = renderer->getWorldManager();
  
  objectManager= renderer->getObjectManager();
}

RtVoid GMANRenderManImpl::RiEnd(RtVoid)
{
  delete renderer;
  //  delete worldManager;
  //delete objectManager;
  if (viewingSystem)
    delete viewingSystem;
}

RtVoid GMANRenderManImpl::RiFrameBegin(RtInt /*frame*/)
{
  enterMode(F);
}

RtVoid GMANRenderManImpl::RiFrameEnd(RtVoid)
{
  leaveMode(F);
}

RtVoid GMANRenderManImpl::RiWorldBegin(RtVoid)
{
  enterMode(W);

  // RiProjection is legal only before RiWorldBegin, so the CTM right here
  // *is* the world-to-camera transform -- nothing after this point can
  // still be camera setup. RenderMan's camera looks down +z (left-handed);
  // GMANMatrix4::prjPersp relies on that to give w=z its sign.
  GMANMatrix4 worldToCamera = getTransform().interpolate(0.0);

  // FIXME FIXME FIXME
  // this order is messed up!!!
  GMANOptions::RasterInfo ri = getOptions().getRasterInfo();

  // Built locally and only handed to the `output` member once every
  // fallible step below (including viewing-system construction, which can
  // throw on a singular worldToCamera) has succeeded, so a GMANError
  // unwinding through here can't leak the driver.
  std::unique_ptr<GMANOutput> newOutput;

  if(getOptions().getDisplay().type == "file") {
    size_t startExt = getOptions().getDisplay().name.rfind(".");
    std::string ext;
    if(startExt != std::string::npos) {
      ext = getOptions().getDisplay().name.substr(startExt+1);
    }
    debug("Displaying to file with extension, %s.", ext.c_str());

    if((ext == "tif") || (ext == "tiff")) {

      newOutput.reset(new GMANOutputTIFF( getOptions().
				   getDisplay().name.c_str(),
				   ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				   ri.rymax-ri.rymin+1));
    } else if (ext == "png") {

      newOutput.reset(new GMANOutputPNG( getOptions().
				  getDisplay().name.c_str(),
				  ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				  ri.rymax-ri.rymin+1));


    } else if (ext == "pnm") {

      newOutput.reset(new GMANOutputPNM( getOptions().
				  getDisplay().name.c_str(),
				  ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				  ri.rymax-ri.rymin+1));
    }

  } else {  // type == framebuffer
    // our frame buffer
    newOutput.reset(new GMANOutputX11( getOptions().
				getDisplay().name.c_str(),
				ri.rxmax-ri.rxmin+1, // if the user use RiCropWindow
				ri.rymax-ri.rymin+1));
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
    fov = param[0];
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
					 worldToCamera,
					 cw.nearDist,
					 cw.farDist);
  } else {
    viewingSystem=new GMANVSPerspective(ri.xres,
					ri.yres,
					sw,
					worldToCamera,
					fov,
					cw.nearDist,
					cw.farDist);
  }

  output = newOutput.release();
}

RtVoid GMANRenderManImpl::RiWorldEnd(RtVoid)
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
RtObjectHandle GMANRenderManImpl::RiObjectBegin(RtVoid)
{
  enterMode(O);
  //return GetObjectManager()->Create()->GetHandle();
  return (RtObjectHandle) 0;
}

// commit the most recently created object
RtVoid GMANRenderManImpl::RiObjectEnd(RtVoid)
{
  leaveMode(O);
  //objectManagerStack.top().Commit();
}

RtVoid  GMANRenderManImpl::RiObjectInstance(RtObjectHandle /*handle*/)
{
  allowed(cmdObjectInstance);
}

RtVoid  GMANRenderManImpl::RiAttributeBegin(RtVoid)
{
  enterMode(A);
}
RtVoid  GMANRenderManImpl::RiAttributeEnd(RtVoid)
{
  leaveMode(A);
}

RtVoid  GMANRenderManImpl::RiTransformBegin(RtVoid)
{
  enterMode(T);
}
RtVoid  GMANRenderManImpl::RiTransformEnd(RtVoid)
{
  leaveMode(T);
}

RtVoid  GMANRenderManImpl::RiSolidBegin(RtToken /*operation*/)
{
  enterMode(S);
}
RtVoid  GMANRenderManImpl::RiSolidEnd(RtVoid)
{
  leaveMode(S);

}

RtVoid  GMANRenderManImpl::RiMotionBeginV(RtInt n, RtFloat times[])
{
  enterMotion(n,times);
}
RtVoid  GMANRenderManImpl::RiMotionEnd(RtVoid)
{
  leaveMode(M);
}


// **************************************************************
// ******* ******* ******* CAMERA OPTIONS ******* ******* *******
// **************************************************************
RtVoid  GMANRenderManImpl::RiFormat (RtInt xres, RtInt yres, RtFloat aspect)
{
  allowed(cmdFormat);
  getOptions().setFormat (xres, yres, aspect); 
}
RtVoid  GMANRenderManImpl::RiFrameAspectRatio (RtFloat aspect)
{
  allowed(cmdFrameAspectRatio);
  getOptions().setFrameAspectRatio (aspect);
}
RtVoid  GMANRenderManImpl::RiScreenWindow (RtFloat left, RtFloat right, RtFloat bottom, RtFloat top)
{
  allowed(cmdScreenWindow);
  getOptions().setScreenWindow (left, right, bottom, top);
}
RtVoid  GMANRenderManImpl::RiCropWindow (RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax)
{
  allowed(cmdCropWindow);
  getOptions().setCropWindow(xmin, xmax, ymin, ymax);
}
RtVoid  GMANRenderManImpl::RiProjectionV (RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdProjection);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setProjection (name, p);
}
RtVoid  GMANRenderManImpl::RiClipping(RtFloat hither, RtFloat yon)
{
  allowed(cmdClipping);
  getOptions().setClipping(hither, yon);
}
RtVoid  GMANRenderManImpl::RiDepthOfField (RtFloat fstop, RtFloat focallength, RtFloat focaldistance)
{
  allowed(cmdDepthOfField);
  getOptions().setDepthOfField(fstop, focallength, focaldistance);
}
RtVoid  GMANRenderManImpl::RiShutter(RtFloat min, RtFloat max)
{
  allowed(cmdShutter);
  getOptions().setShutter(min, max);
}

// ***************************************************************
// ******* ******* ******* DISPLAY OPTIONS ******* ******* *******
// ***************************************************************
RtVoid  GMANRenderManImpl::RiPixelVariance(RtFloat variation)
{
  allowed(cmdPixelVariance);
  getOptions().setPixelVariance(variation);
}
RtVoid  GMANRenderManImpl::RiPixelSamples(RtFloat xsamples, RtFloat ysamples)
{ 
  allowed(cmdPixelSamples);
  if (xsamples<1.0) {xsamples=1.0;}
  if (ysamples<1.0) {ysamples=1.0;}
  getOptions().setPixelSamples(xsamples, ysamples);
}
RtVoid  GMANRenderManImpl::RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth)
{
  allowed(cmdPixelFilter);
  getOptions().setPixelFilter(filterfunc, xwidth, ywidth);
}
RtVoid  GMANRenderManImpl::RiExposure(RtFloat gain, RtFloat gamma)
{
  allowed(cmdExposure);
  getOptions().setExposure(gain, gamma);
}
RtVoid  GMANRenderManImpl::RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdImager);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setImager(name, p, *renderer);
}
RtVoid  GMANRenderManImpl::RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl)
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
RtVoid  GMANRenderManImpl::RiDisplayV(char *name, RtToken type, RtToken mode,
				  RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdDisplay);

  // RISpec: a name with no leading '+' replaces the display set; one
  // with a leading '+' adds to it. GMAN renders through a single active
  // display (see AGENTS.md's RIB support table) rather than a real set,
  // so "add" means "keep whichever of the two can actually write
  // output" instead of accumulating displays.
  std::string nm(name);
  bool add = !nm.empty() && nm[0] == '+';
  if (add) {
    nm.erase(0, 1);
  }

  bool haveDisplay = !getOptions().getDisplay().type.empty();
  bool replaces = !add || !haveDisplay;
  bool upgrades = add && haveDisplay &&
                  getOptions().getDisplay().type != "file" &&
                  std::string(type) == "file";

  if (!replaces && !upgrades) {
    return; // keep the currently active display
  }

  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setDisplay (nm, type, mode, p);
}

// ******************************************************************
// ******* ******* ******* ADDITIONAL OPTIONS ******* ******* *******
// ******************************************************************
RtVoid  GMANRenderManImpl::RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdHider);
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setHider(type, p);
}
RtVoid  GMANRenderManImpl::RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[])
{
  allowed(cmdColorSamples);
  getOptions().setColorSamples(n, nRGB, RGBn);
}
RtVoid  GMANRenderManImpl::RiRelativeDetail(RtFloat relativedetail)
{
  allowed(cmdRelativeDetail);
  getOptions().setRelativeDetail(relativedetail);
}
RtVoid  GMANRenderManImpl::RiOptionV(RtToken /*name*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
}






// ******************************************************************
// ******* ******* ******* SHADING ATTRIBUTES ******* ******* *******
// ******************************************************************
RtVoid  GMANRenderManImpl::RiColor(RtColor color)
{
  allowed(cmdColor);
  getAttributes().setColor(color);
}
RtVoid  GMANRenderManImpl::RiOpacity(RtColor color)
{
  allowed(cmdOpacity);
  getAttributes().setOpacity(color);
}
RtVoid  GMANRenderManImpl::RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
					    RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4)
{
  allowed(cmdTextureCoordinates);
  getAttributes().setTextureCoordinates(s1, t1, s2, t2, s3, t3, s4, t4);
}
namespace {

// GMANParameterList::getPointer throws GMANError(RIE_CONSISTENCY,
// TOKEN_NOT_FOUND) for a token this parameter list simply doesn't carry --
// it never returns NULL for "absent," only for a token nobody declared at
// all (see SPEC.md's account of the same throw against RI_FOV). Every
// light parameter here is optional, so probing for one is routine, not
// exceptional; this is the one place that distinction has to be made by
// hand.
RtFloat *tryGetPointer(GMANDictionary &dictionary, GMANParameterList &pl,
			RtToken token) {
  try {
    return (RtFloat *) pl.getPointer(dictionary.getTokenId(token));
  } catch (GMANError &) {
    return NULL;
  }
}

// "intensity" (RtFloat) and "lightcolor" (RtColor), the two parameters the
// three built-in light types share.
RtVoid readCommonLightParams(GMANDictionary &dictionary,
			      GMANParameterList &pl,
			      RtFloat &intensity, GMANColor &color) {
  RtFloat *ip = tryGetPointer(dictionary, pl, RI_INTENSITY);
  if (ip) {
    intensity = ip[0];
  }
  RtFloat *cp = tryGetPointer(dictionary, pl, RI_LIGHTCOLOR);
  if (cp) {
    color = GMANColor(cp[0], cp[1], cp[2]);
  }
}

// "from"/"to" (RtPoint), read with the RISpec's own defaults: from
// (0,0,0), to (0,0,1) -- a distantlight or pointlight with no explicit
// position or direction still declares something sane.
RtVoid readFromTo(GMANDictionary &dictionary, GMANParameterList &pl,
		   GMANPoint &from, GMANPoint &to) {
  RtFloat *fp = tryGetPointer(dictionary, pl, RI_FROM);
  if (fp) {
    from = GMANPoint(fp[0], fp[1], fp[2]);
  }
  RtFloat *tp = tryGetPointer(dictionary, pl, RI_TO);
  if (tp) {
    to = GMANPoint(tp[0], tp[1], tp[2]);
  }
}

}  // namespace

RtLightHandle GMANRenderManImpl::RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdLightSource);
  GMANParameterList paramList(dictionary, n, tokens, parms);

  // Captured at declaration time, exactly the mechanism every primitive
  // uses (RiSphereV et al, near :803): the light's position/direction
  // travel through the CTM now in effect, landing in camera space to
  // match every other quantity shading math touches. A light is a point
  // or a direction, not a solid, so no inverse transpose is needed here --
  // that correction is for surface normals only.
  GMANTransform transform(getTransform());

  std::string lightName(name);
  RtFloat intensity = 1.0;
  GMANColor color((RtFloat) 1.0, (RtFloat) 1.0, (RtFloat) 1.0);
  readCommonLightParams(dictionary, paramList, intensity, color);
  GMANColor cl(color.getRed() * intensity, color.getGreen() * intensity,
	       color.getBlue() * intensity);

  GMANLightType type;
  GMANPoint position(0.0, 0.0, 0.0);
  GMANVector direction(0.0, 0.0, 1.0);

  if (lightName == "ambientlight") {
    type = GMAN_LIGHT_AMBIENT;
  } else if (lightName == "distantlight") {
    type = GMAN_LIGHT_DISTANT;
    GMANPoint from(0.0, 0.0, 0.0);
    GMANPoint to(0.0, 0.0, 1.0);
    readFromTo(dictionary, paramList, from, to);
    GMANPoint camFrom = transform.apply(from);
    GMANPoint camTo = transform.apply(to);
    direction = GMANVector(camFrom, camTo);
    direction.normalize();
  } else if (lightName == "pointlight") {
    type = GMAN_LIGHT_POINT;
    GMANPoint from(0.0, 0.0, 0.0);
    GMANPoint to(0.0, 0.0, 1.0);
    readFromTo(dictionary, paramList, from, to);
    position = transform.apply(from);
  } else {
    warning("Unknown light shader '%s'; ignoring RiLightSource.",
	    lightName.c_str());
    return (RtLightHandle) 0;
  }

  GMANLight *light = new GMANLight(type, cl, position, direction);
  RtLightHandle handle = gmanLightSourceMgr().add(light);

  // Per the RISpec: a light is active in the current graphics state the
  // instant it is declared, as if RiIlluminate(handle, RI_TRUE) had just
  // been called. Without this, GMANAttributes::lightList never carries
  // the handle and no light this call creates ever reaches a shader --
  // RiIlluminate only needs to be called explicitly to turn a light back
  // on after RiIlluminate(handle, RI_FALSE) turned it off, or to bring an
  // outer-scope light into a nested attribute block.
  getAttributes().setIlluminate(handle, RI_TRUE);

  return handle;
}
RtLightHandle GMANRenderManImpl::RiAreaLightSourceV(RtToken /*name*/,
						RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdAreaLightSource);
  return (RtLightHandle) 0;
}
RtVoid  GMANRenderManImpl::RiIlluminate(RtLightHandle light, RtBoolean onoff)
{
  allowed(cmdIlluminate);
  getAttributes().setIlluminate(light, onoff);
}
RtVoid  GMANRenderManImpl::RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdSurface);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setSurface(name, p, *renderer);
}
RtVoid  GMANRenderManImpl::RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdAtmosphere);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setAtmosphere(name, p, *renderer);
}
RtVoid  GMANRenderManImpl::RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdInterior);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setInterior(name, p, *renderer);
}
RtVoid  GMANRenderManImpl::RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdExterior);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setExterior(name, p, *renderer);
}
RtVoid  GMANRenderManImpl::RiShadingRate(RtFloat size)
{
  allowed(cmdShadingRate);
  getAttributes().setShadingRate(size);
}
RtVoid  GMANRenderManImpl::RiShadingInterpolation(RtToken type)
{
  allowed(cmdShadingInterpolation);
  if (strcmp(type, RI_CONSTANT) && strcmp(type, RI_SMOOTH)) {
    GMANError error(RIE_UNIMPLEMENT,RIE_WARNING,
		    "Unknown shading interpolation type");
    throw error;
  }
  getAttributes().setShadingInterpolation (type);
}
RtVoid  GMANRenderManImpl::RiMatte(RtBoolean onoff)
{
  allowed(cmdMatte);
  getAttributes().setMatte(onoff);
}

// *******************************************************************
// ******* ******* ******* GEOMETRY ATTRIBUTES ******* ******* *******
// *******************************************************************
RtVoid  GMANRenderManImpl::RiBound(RtBound b)
{
  allowed(cmdBound);
  getAttributes().setBound(b);
}
RtVoid  GMANRenderManImpl::RiDetail(RtBound d)
{
  allowed(cmdDetail);
  getAttributes().setDetail(d);
}
RtVoid  GMANRenderManImpl::RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis)
{
  allowed(cmdDetailRange);
  getAttributes().setDetailRange(minvis, lowtran, uptran, maxvis);
}
RtVoid  GMANRenderManImpl::RiGeometricApproximation(RtToken type, RtFloat value)
{
  allowed(cmdGeometricApproximation);
  // strcmp, not pointer equality: type is a token parsed out of the RIB and
  // is never the RI_FLATNESS global itself, so the pointer test rejected
  // every value including the only legal one. RiShadingInterpolation and
  // RiOrientation just below compare the same kind of token with strcmp.
  if (strcmp(type, RI_FLATNESS)) {
    GMANError error(RIE_UNIMPLEMENT,RIE_WARNING,"Unknown geometric approximation type");
    throw error;
  }
  getAttributes().setGeometricApproximation (type, value);
}
RtVoid  GMANRenderManImpl::RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep)
{
  allowed(cmdBasis);
  getAttributes().setUVBasis(ubasis, ustep, vbasis, vstep);
}
RtVoid  GMANRenderManImpl::RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[],
				   RtFloat knot[], RtFloat min[], RtFloat max[], RtInt n[],
				   RtFloat u[], RtFloat v[], RtFloat w[])
{
  allowed(cmdTrimCurve);
  GMANTrimCurve tc(nloops, ncurves, order, knot, min, max, n, u, v, w);
  getAttributes().setTrimCurves (tc);
}
RtVoid  GMANRenderManImpl::RiOrientation(RtToken o)
{
  allowed(cmdOrientation);
  if (strcmp(o, RI_INSIDE) && strcmp(o, RI_OUTSIDE) &&
      strcmp(o, RI_LH) && strcmp(o, RI_RH)) {
    GMANError error(RIE_UNIMPLEMENT,RIE_WARNING,"Unknown orientation type");
    throw error;
  }
  getAttributes().setOrientation (o);
}

RtVoid  GMANRenderManImpl::RiReverseOrientation(RtVoid)
{
  allowed(cmdReverseOrientation);
  getAttributes().toggleOrientation();
}

RtVoid  GMANRenderManImpl::RiSides(RtInt sides)
{
  allowed(cmdSides);
  getAttributes().setSides(sides);
}
RtVoid  GMANRenderManImpl::RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdDisplacement);
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setDisplacement (name, p, *renderer);
}


// ***************************************************************
// ******* ******* ******* TRANSFORMATIONS ******* ******* *******
// ***************************************************************
RtVoid  GMANRenderManImpl::RiIdentity(RtVoid)
{
  allowed(cmdIdentity);
  GMANMatrix4 m;
  setTransform(m);
}
RtVoid  GMANRenderManImpl::RiTransform(RtMatrix transform)
{
  allowed(cmdTransform);
  GMANMatrix4 m(transform);
  setTransform(m);
}
RtVoid  GMANRenderManImpl::RiConcatTransform(RtMatrix transform)
{
  allowed(cmdConcatTransform);
  GMANMatrix4 m(transform);
  buildTransform(m);
}
RtVoid  GMANRenderManImpl::RiPerspective(RtFloat fov)
{
  allowed(cmdPerspective);
  GMANMatrix4 m;
  m.persp(fov);
  buildTransform(m);
}
RtVoid  GMANRenderManImpl::RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz)
{
  allowed(cmdTranslate);
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  buildTransform(m);
}
RtVoid  GMANRenderManImpl::RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)
{
  allowed(cmdRotate);
  GMANMatrix4 m;
  m.rot(angle*DEGTORAD, dx, dy, dz);
  buildTransform(m);
}
RtVoid  GMANRenderManImpl::RiScale(RtFloat sx, RtFloat sy, RtFloat sz)
{
  allowed(cmdScale);
  GMANMatrix4 m;
  m.scale(sx, sy, sz);
  buildTransform(m);
}
RtVoid  GMANRenderManImpl::RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1,
			      RtFloat dx2, RtFloat dy2, RtFloat dz2)
{
  allowed(cmdSkew);
  GMANMatrix4 m;
  GMANVector a(dx1,dy1,dz1);
  GMANVector b(dx2,dy2,dz2);
  m.skew (angle, a, b);
  buildTransform(m);
}
RtVoid  GMANRenderManImpl::RiDeformationV(RtToken /*name*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{}
RtVoid  GMANRenderManImpl::RiCoordinateSystem(RtToken /*space*/)
{
  allowed(cmdCoordinateSystem);
}
RtVoid  GMANRenderManImpl::RiCoordSysTransform(RtToken /*space*/)
{
  allowed(cmdCoordSysTransform);
}

RtPoint *GMANRenderManImpl::RiTransformPoints(RtToken /*fromspace*/, RtToken /*tospace*/, RtInt /*n*/,
					  RtPoint /*points*/[])
{
  allowed(cmdTransformPoints);
  return (RtPoint *) 0;
}

// AttributeV
RtVoid GMANRenderManImpl::RiAttributeV(RtToken /*name*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{}





// **********************************************************
// ******* ******* ******* PRIMITIVES ******* ******* *******
// **********************************************************
RtVoid  GMANRenderManImpl::RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[])
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
RtVoid  GMANRenderManImpl::RiGeneralPolygonV(RtInt /*nloops*/, RtInt /*nverts*/[], RtInt /*n*/,
					 RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdGeneralPolygon);
}
RtVoid  GMANRenderManImpl::RiPointsPolygonsV(RtInt /*npolys*/, RtInt /*nverts*/[], RtInt /*verts*/[],  RtInt /*n*/,
					 RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdPointsPolygon);
}
RtVoid  GMANRenderManImpl::RiPointsGeneralPolygonsV(RtInt /*npolys*/, RtInt /*nloops*/[], RtInt /*nverts*/[],
						RtInt /*verts*/[], RtInt /*n*/, RtToken /*tokens*/[], 
						RtPointer /*parms*/[])
{
  allowed(cmdPointsGeneralPolygons);
}
RtVoid  GMANRenderManImpl::RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[])
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
RtVoid  GMANRenderManImpl::RiPatchMeshV(RtToken /*type*/, RtInt /*nu*/, RtToken /*uwrap*/,
				    RtInt /*nv*/, RtToken /*vwrap*/, RtInt /*n*/, RtToken /*tokens*/[], 
				    RtPointer /*parms*/[])
{
  allowed(cmdPatchMesh);
}
RtVoid  GMANRenderManImpl::RiNuPatchV(RtInt /*nu*/, RtInt /*uorder*/, RtFloat /*uknot*/[], RtFloat /*umin*/,
				  RtFloat /*umax*/, RtInt /*nv*/, RtInt /*vorder*/, RtFloat /*vknot*/[],
				  RtFloat /*vmin*/, RtFloat /*vmax*/,
				  RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdNuPatch);
}
  
RtVoid  GMANRenderManImpl::RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
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
RtVoid  GMANRenderManImpl::RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
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
RtVoid  GMANRenderManImpl::RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
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
RtVoid  GMANRenderManImpl::RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
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
RtVoid  GMANRenderManImpl::RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
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
RtVoid  GMANRenderManImpl::RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
			       RtInt n, RtToken tokens[], RtPointer parms[])
{
  allowed(cmdDisk);
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4);

  GMANTransform* transform = new GMANTransform((getTransform()));
  GMANPrimitive* prim;

  prim = objectManager->getRSDisk( height, radius, tmax, paramList,
				   &(getOptions()),
				   &(getAttributes()),
				   transform);
  worldManager->add(prim);
  delete transform;
}
RtVoid  GMANRenderManImpl::RiTorusV(RtFloat majrad,RtFloat minrad,RtFloat phimin,RtFloat phimax,
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
  
RtVoid  GMANRenderManImpl::RiBlobbyV(RtInt /*nleaf*/, RtInt /*ncode*/, RtInt /*code*/[],
				 RtInt /*nflt*/, RtFloat /*flt*/[],
				 RtInt /*nstr*/, RtToken /*str*/[], 
				 RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdBlobby);
}
RtVoid  GMANRenderManImpl::RiPointsV(RtInt /*npoints*/,
				 RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdPoints);
}
RtVoid  GMANRenderManImpl::RiCurvesV(RtToken /*type*/, RtInt /*ncurves*/,
				 RtInt /*nvertices*/[], RtToken /*wrap*/,
				 RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdCurves);
}
RtVoid  GMANRenderManImpl::RiSubdivisionMeshV(RtToken /*mask*/, RtInt /*nf*/, RtInt /*nverts*/[],
					  RtInt /*verts*/[],
					  RtInt /*ntags*/, RtToken /*tags*/[], RtInt /*numargs*/[],
					  RtInt /*intargs*/[], RtFloat /*floatargs*/[],
					  RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdSubdivisionMesh);
}

RtVoid  GMANRenderManImpl::RiProcedural(RtPointer /*data*/, RtBound /*bound*/,
				    RtVoid (*/*subdivfunc*/)(RtPointer, RtFloat),
				    RtVoid (*/*freefunc*/)(RtPointer))
{
  allowed(cmdProcedural);
}
RtVoid  GMANRenderManImpl::RiGeometryV(RtToken /*type*/, RtInt /*n*/, RtToken /*tokens*/[], 
				   RtPointer /*parms*/[])
{
  allowed(cmdGeometry);
}

// ****************************************************
// ******* ******* ******* MISC ******* ******* *******
// ****************************************************
RtVoid  GMANRenderManImpl::RiMakeTextureV(char */*pic*/, char */*tex*/, RtToken /*swrap*/, RtToken /*twrap*/,
				      RtFilterFunc /*filterfunc*/, RtFloat /*swidth*/, RtFloat /*twidth*/,
				      RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdMakeTexture);
}
RtVoid  GMANRenderManImpl::RiMakeBumpV(char */*pic*/, char */*tex*/, RtToken /*swrap*/, RtToken /*twrap*/,
				   RtFilterFunc /*filterfunc*/, RtFloat /*swidth*/, RtFloat /*twidth*/,
				   RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdMakeBump);
}
RtVoid  GMANRenderManImpl::RiMakeLatLongEnvironmentV(char */*pic*/, char */*tex*/, RtFilterFunc /*filterfunc*/,
				  RtFloat /*swidth*/, RtFloat /*twidth*/,
				  RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdMakeLatLongEnvironment);
}
RtVoid  GMANRenderManImpl::RiMakeCubeFaceEnvironmentV(char */*px*/, char */*nx*/, char */*py*/, char */*ny*/,
				   char */*pz*/, char */*nz*/, char */*tex*/, RtFloat /*fov*/,
				   RtFilterFunc /*filterfunc*/, RtFloat /*swidth*/, 
				   RtFloat /*ywidth*/,
				   RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdMakeCubeFaceEnvironment);
}
RtVoid  GMANRenderManImpl::RiMakeShadowV(char */*pic*/, char */*tex*/,
				     RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[])
{
  allowed(cmdMakeShadow);
}



// ******************************************************************
// ******* ******* ******* ERROR HANDLER ******* ******* *******
// ******************************************************************

// Present on the interface and absent from this class until phase 0. It
// installs the handler GMANHandleError dispatches through.
RtVoid GMANRenderManImpl::RiErrorHandler(RtErrorHandler handler)
{
  GMANErrorHandler = handler;
}
