/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanbitmap.h" /* Declaration Header */
#include "gmancolor.h"
#include "gmandefaults.h"

/*
 * RenderMan API GMANBitmap
 *
 */

// default constructor
GMANBitmap::GMANBitmap() : UniversalSuperClass(), background(DefaultBGColor) { 
  pixels = NULL;
  nPixels = 0;
  xres = 0;
  yres = 0;
};


// construct a bitmap with the specified width and height
GMANBitmap::GMANBitmap(int width, int height, const GMANColor &bgcolor=DefaultBGColor) : background(bgcolor) {
  pixels = NULL;
  nPixels = 0;
  set(width, height, bgcolor);
};

// default destructor 
GMANBitmap::~GMANBitmap() { 
  freeMemory();
};

RtVoid GMANBitmap::set(int width, int height, const GMANColor &bgcolor) {
  background = bgcolor;
  xres = width;
  yres = height;
  allocMemory();

  erase();

}

// copy operation
GMANBitmap &GMANBitmap::operator =(const GMANBitmap &amap) {

  background = amap.background;
  // setup memory
  if((xres != amap.xres) && (yres != amap.yres)) {
    freeMemory();
    xres = amap.xres;
    yres = amap.yres;
    allocMemory();
  };

  // copy all pixels
  for(int i=0; i<xres; i++) {
    for(int j=0; j<yres; j++) {
      setPixel(i,j, amap.getPixel(i,j));
    }
  }

  return *this;  
};

RtVoid GMANBitmap::freeMemory(RtVoid) {
  if(pixels) delete []pixels;
}

RtVoid GMANBitmap::allocMemory(RtVoid) {
  int pixels_needed = xres*yres;
  if(nPixels != pixels_needed) {
    nPixels = xres*yres;
    if(pixels) delete []pixels;
    pixels = new GMANColor[nPixels];
  }
}

RtVoid GMANBitmap::erase(RtVoid) {
  fill(background);
}

RtVoid GMANBitmap::fill(const GMANColor &fillColor) {
  for(int x=0; x<xres; x++) {
    for (int y=0; y<yres; y++) {
      setPixel(x, y, fillColor);
    }
  }
}


RtInt GMANBitmap::getWidth(RtVoid) const {
    return xres;
}

RtInt GMANBitmap::getHeight(RtVoid) const {
    return yres;
}
