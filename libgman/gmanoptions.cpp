/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* Added display and additional options LJL (2000/07/28) */

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

#include "gmanoptions.h"
#include "gmandefaults.h"

GMANOptions::OutputDefaults GMANOptions::outputDefaults = {
    GMANDisplayXRES,
    GMANDisplayYRES,
    GMANDisplayPAR
};

/*
 * RenderMan API camera, display and additional options.
 *
 */

GMANOptions::GMANOptions() : UniversalSuperClass(),
			     imagerModule(NULL),
			     imager(NULL)
{ 
  // **** CAMERA OPTIONS ****
  format.xres = 640;
  format.yres = 480;
  format.pixelAspectRatio = -1;

  frameAspectRatio = -1;

  setCropWindow(DefaultCropWindowXMIN, DefaultCropWindowXMAX,
		DefaultCropWindowYMIN, DefaultCropWindowYMAX);

  projection.name = RI_ORTHOGRAPHIC;

  clipping.nearDist = DefaultClippingNear;
  clipping.farDist = DefaultClippingFar;

  depthOfField.fstop = DefaultFStop;

  shutter.min = DefaultShutterOpen;
  shutter.max = DefaultShutterClose;

  // **** DISPLAY OPTIONS ****  
  pixelSamples.xsamples = DefaultPixelSamplesX;
  pixelSamples.ysamples = DefaultPixelSamplesY;

  pixelFilter.filterfunc = DefaultFilterFunc;
  pixelFilter.xwidth = DefaultFilterXWidth;
  pixelFilter.ywidth = DefaultFilterYWidth;

  exposure.gain = DefaultGain;
  exposure.gamma = DefaultGamma;

  colorQuantize.one = DefaultColorQuantizerOne;
  colorQuantize.min = DefaultColorQuantizerMin;
  colorQuantize.max = DefaultColorQuantizerMax;
  colorQuantize.ditheramplitude = DefaultColorQuantizerDA;

  depthQuantize.one = DefaultDepthQuantizerOne;

  // **** ADDITIONAL OPTIONS ****
  hider.type = RI_HIDDEN;
  relativeDetail = DefaultRelativeDetail;

  background = DefaultBGColor;
  screenWindowSet=false;
};

GMANOptions::~GMANOptions()
{ 
};

// ******* ******* CAMERA OPTIONS ******* *******
RtVoid GMANOptions::setFormat(RtInt xres, RtInt yres, RtFloat aspect)
{
  format.xres = xres;
  format.yres = yres;
  format.pixelAspectRatio = aspect;
}
const GMANOptions::FormatStruct GMANOptions::getFormat (RtVoid) const
{
    if (format.xres > 0 && format.pixelAspectRatio > 0 )
	return format;
    
    FormatStruct f=format;
    const OutputDefaults &d=getOutputDefaults();
    
    if (format.xres <= 0) {
	f.xres=d.xres;
	f.yres=d.yres;
    }
    if (format.pixelAspectRatio <= 0) {
	f.pixelAspectRatio=d.par;
    }
    return f;
}

RtVoid GMANOptions::setFrameAspectRatio(RtFloat ar)
{
    frameAspectRatio = ar; 
}

RtFloat GMANOptions::getFrameAspectRatio (RtVoid) const
{
    if (frameAspectRatio==-1) {
	FormatStruct f=getFormat();
	return f.xres*f.pixelAspectRatio/f.yres;
    } else {
	return frameAspectRatio;
    }
}

RtVoid GMANOptions::setScreenWindow(RtFloat left, 
				    RtFloat right, 
				    RtFloat bottom, 
				    RtFloat top)
{
    screenWindowSet = true;
    screenWindow.left = left;
    screenWindow.right = right;
    screenWindow.bottom = bottom;
    screenWindow.top = top;
}
const GMANOptions::ScreenWindowStruct 
GMANOptions::getScreenWindow (RtVoid) const
{
    if (screenWindowSet==true) {
	return screenWindow;
    }
    ScreenWindowStruct sw;
    RtFloat farDist=getFrameAspectRatio();
    if (farDist >= 1.0) {
	sw.left = -farDist;
	sw.right = farDist;
	sw.bottom = -1.0;
	sw.top = 1.0;
    } else {
	sw.left = -1.0;
	sw.right = 1.0;
	sw.bottom = -1.0/farDist;
	sw.top = 1.0/farDist;
    }
    return sw;
}

const GMANOptions::RasterInfo GMANOptions::getRasterInfo(RtVoid) const
{
    RasterInfo ri;
    FormatStruct f=getFormat();
    RtFloat farDist=getFrameAspectRatio();
    if (farDist>=1) {
	ri.xres=(RtInt) (f.yres*farDist/f.pixelAspectRatio);
	ri.yres=f.yres;
    } else {
	ri.xres=f.xres;
	ri.yres=(RtInt) (f.xres*farDist/f.pixelAspectRatio);
    }
    ri.rxmin=(RtInt) ceil(f.xres*cropWindow.xmin);
    ri.rxmax=(RtInt) ceil(f.xres*cropWindow.xmax-1);
    ri.rymin=(RtInt) ceil(f.yres*cropWindow.ymin);
    ri.rymax=(RtInt) ceil(f.yres*cropWindow.ymax-1);
    return ri;
}

RtVoid  GMANOptions::setCropWindow(RtFloat xmin, 
				   RtFloat xmax, 
				   RtFloat ymin, 
				   RtFloat ymax)
{
    cropWindow.xmin = GMANClamp<RtFloat>(xmin,0.0,1.0);
    cropWindow.xmax = GMANClamp<RtFloat>(xmax,0.0,1.0);
    cropWindow.ymin = GMANClamp<RtFloat>(ymin,0.0,1.0);
    cropWindow.ymax = GMANClamp<RtFloat>(ymax,0.0,1.0);
}

RtVoid  GMANOptions::setProjection (string n, GMANParameterList &p)
{
    projection.name=n;
    projection.pl=p;
}

RtVoid  GMANOptions::setClipping (RtFloat n, RtFloat f)
{
    clipping.nearDist=n;
    clipping.farDist=f;
}

RtVoid  GMANOptions::setDepthOfField (RtFloat fs,  RtFloat flength, RtFloat fdistance)
{
    depthOfField.fstop=fs;
    depthOfField.focallength=flength;
    depthOfField.focaldistance=fdistance;
}

RtVoid  GMANOptions::setShutter (RtFloat mn, RtFloat mx)
{
    shutter.min=mn;
    shutter.max=mx;
}


// ******* ******* DISPLAY OPTIONS ******* *******
RtVoid  GMANOptions::setPixelVariance (RtFloat var)
{
    pixelVariance=var;
}

RtVoid  GMANOptions::setPixelSamples(RtFloat xsamples, RtFloat ysamples)
{
    pixelSamples.xsamples = xsamples;
    pixelSamples.ysamples = ysamples;
}

RtVoid  GMANOptions::setPixelFilter (RtFilterFunc ff, RtFloat xw, RtFloat yw)
{
    pixelFilter.filterfunc=ff;
    pixelFilter.xwidth=xw;
    pixelFilter.ywidth=yw;
}

RtVoid  GMANOptions::setExposure (RtFloat gn, RtFloat gmm)
{
    exposure.gain=gn;
    exposure.gamma=gmm;
}

RtVoid  GMANOptions::setImager (string name,
				GMANParameterList &pl, 
				GMANRenderer &rd)
{
    if(imagerModule) delete(imagerModule);
    imagerModule = new GMANLoadableShader(name.c_str());
    if(imagerModule->getType() == GMANShader::IMAGER) {
	imager = imagerModule->getImager();
    } else {
	throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Specified imager shader is not an imager shader."));
    }
    
    imager->set(pl);
    imager->set(rd);
}

RtVoid  GMANOptions::setColorQuantize (RtInt o, RtInt mn, RtInt mx, RtFloat da)
{
    colorQuantize.one=o;
    colorQuantize.min=mn;
    colorQuantize.max=mx;
    colorQuantize.ditheramplitude=da;
}

RtVoid  GMANOptions::setDepthQuantize (RtInt o, RtInt mn, RtInt mx, RtFloat da)
{
    depthQuantize.one=o;
    depthQuantize.min=mn;
    depthQuantize.max=mx;
    depthQuantize.ditheramplitude=da;
}

RtVoid  GMANOptions::setDisplay (string nm, string tp, string md, GMANParameterList &p)
{
    display.name=nm;
    display.type=tp;
    display.mode=md;
    display.pl=p;
}


// ******* ******* ADDITIONAL OPTIONS ******* *******
RtVoid  GMANOptions::setHider (string nm, GMANParameterList &p)
{
    hider.type=nm;
    hider.pl=p;
}

RtVoid  GMANOptions::setColorSamples (RtInt nb, RtFloat *nr, RtFloat *rn)
{
    GMANColorSamples cs(nb, nr, rn);
    colorSamples=cs;
}

RtVoid  GMANOptions::setRelativeDetail (RtFloat rd)
{
    relativeDetail=rd;
}


const GMANOptions::OutputDefaults &GMANOptions::getOutputDefaults(RtVoid) const
{
    return outputDefaults;
}
