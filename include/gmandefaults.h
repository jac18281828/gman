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
 

#ifndef __GMAN_DEFAULTS_H
#define __GMAN_DEFAULTS_H 1

#include "ri.h"

#include "gmancolor.h"

/* Some important default values */

/******* ******* CAMERA OPTIONS ******* *******/
extern const RtFloat   DefaultCropWindowXMIN;
extern const RtFloat   DefaultCropWindowXMAX;
extern const RtFloat   DefaultCropWindowYMIN;
extern const RtFloat   DefaultCropWindowYMAX;

extern const RtFloat   DefaultClippingNear;
extern const RtFloat   DefaultClippingFar;

extern const RtFloat   DefaultFStop;

extern const RtFloat   DefaultShutterOpen;
extern const RtFloat   DefaultShutterClose;

/******* ******* DISPLAY OPTIONS ******* *******/
extern const RtFloat   DefaultPixelSamplesX;
extern const RtFloat   DefaultPixelSamplesY;

extern const RtFilterFunc DefaultFilterFunc;
extern const RtFloat   DefaultFilterXWidth;
extern const RtFloat   DefaultFilterYWidth;

extern const RtFloat   DefaultGain;
extern const RtFloat   DefaultGamma;

extern const RtInt     DefaultColorQuantizerOne;
extern const RtInt     DefaultColorQuantizerMin;
extern const RtInt     DefaultColorQuantizerMax;
extern const RtFloat   DefaultColorQuantizerDA;

extern const RtInt     DefaultDepthQuantizerOne;

/******* ******* ADDITIONAL OPTIONS ******* *******/
extern const RtInt     DefaultColorSamples;
extern const RtFloat   DefaultCSMatrixNRGB [9];
extern const RtFloat   DefaultCSMatrixRGBN [9];

extern const RtFloat   DefaultRelativeDetail;

extern const GMANColor DefaultBGColor; // Black

extern const GMANAlpha DefaultAlpha; // opaque

extern const RtInt GMANDisplayXRES;
extern const RtInt GMANDisplayYRES;
extern const RtFloat GMANDisplayPAR;

#endif






