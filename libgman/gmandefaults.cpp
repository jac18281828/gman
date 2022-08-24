/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2002, 2001, 2000, 1999 John Cairns 
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
#include "gmandefaults.h"

const RtFloat   DefaultCropWindowXMIN = 0.0;
const RtFloat   DefaultCropWindowXMAX = 1.0;
const RtFloat   DefaultCropWindowYMIN = 0.0;
const RtFloat   DefaultCropWindowYMAX = 1.0;

const RtFloat   DefaultClippingNear = (RtFloat)RI_EPSILON;
const RtFloat   DefaultClippingFar = (RtFloat)RI_INFINITY;

const RtFloat   DefaultFStop = RI_INFINITY;

const RtFloat   DefaultShutterOpen = 0;
const RtFloat   DefaultShutterClose = 0;

/******* ******* DISPLAY OPTIONS ******* *******/
const RtFloat   DefaultPixelSamplesX = 2;
const RtFloat   DefaultPixelSamplesY = 2;

const RtFilterFunc DefaultFilterFunc = RiGaussianFilter;
const RtFloat   DefaultFilterXWidth = 2;
const RtFloat   DefaultFilterYWidth = 2;

const RtFloat   DefaultGain = 1;
const RtFloat   DefaultGamma = 1;

const RtInt     DefaultColorQuantizerOne = 255;
const RtInt     DefaultColorQuantizerMin = 0;
const RtInt     DefaultColorQuantizerMax = 255;
const RtFloat   DefaultColorQuantizerDA = 0.5;

const RtInt     DefaultDepthQuantizerOne = 0;

/******* ******* ADDITIONAL OPTIONS ******* *******/
const RtInt     DefaultColorSamples = 3;
const RtFloat   DefaultCSMatrixNRGB [9] = {1,0,0,0,1,0,0,0,1};
const RtFloat   DefaultCSMatrixRGBN [9] = {1,0,0,0,1,0,0,0,1};

const RtFloat   DefaultRelativeDetail = 1.0;

const GMANColor DefaultBGColor(1.0); // Black

const GMANAlpha DefaultAlpha(1.0); // opaque

const RtInt GMANDisplayXRES = 100;
const RtInt GMANDisplayYRES = 100;
const RtFloat GMANDisplayPAR  = 1.0;
