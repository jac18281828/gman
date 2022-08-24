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
 

#ifndef __GMAN_GMANBITMAP_H
#define __GMAN_GMANBITMAP_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// the color object
#include "gmancolor.h"


/*
 * RenderMan API GMANBitmap
 *
 */

class GMANDLL GMANBitmap : public UniversalSuperClass {
protected:
  GMANColor	*pixels;
  int            nPixels;

  GMANColor background;

  int		  xres;
  int		  yres;

  // protected methods

  // these may be overridden by a specific output device 
  // depending on how it allocates or frees it's memory
  
  virtual RtVoid freeMemory(RtVoid);
  virtual RtVoid allocMemory(RtVoid);
  

public:
  GMANBitmap(); // default constructor

  // construct a bitmap with the specifed width and height
  GMANBitmap(int width, int height, const GMANColor &background);

  virtual ~GMANBitmap(); // default destructor


  // setup buffer
  virtual RtVoid set(int width, int height, const GMANColor &bgcolor);

  // copy operation
  GMANBitmap &operator=(const GMANBitmap &amap);

  // what is the value of a specific pixel
  virtual const GMANColor &getPixel(int x, int y) const { 
    return pixels[y*xres+x]; 
  };

  // set the color value of a specific pixel
  virtual RtVoid setPixel(int x, int y, const GMANColor &color) {
    pixels[y*xres+x] = color;
  };

  // set a specific row in the frame buffer
  virtual RtVoid setRow(int y, const GMANColor *row) {
    for(int i=0;i<xres; i++) {
      setPixel(i, y, row[i]);
    }
  }
  
  // get a specific row in the frame buffer
  virtual const GMANColor *getRow(int y) const {
    return pixels + y*xres;
  }  

  // overwrite color value with the specified color
  // keeping track of alpha layers
  RtVoid overlayPixel(int x, int y, const GMANColor &color, 
		      const GMANAlpha &alpha) {
    GMANCombine combine;
    GMANColor result = combine(getPixel(x,y), color, alpha);
    setPixel(x, y, result);
  };

  // Erase bitmap to background color
  RtVoid erase(RtVoid);

  // Fill bitmap to specified color
  RtVoid fill(const GMANColor &fillColor);

  RtInt getWidth(void) const;
  RtInt getHeight(void) const; 
 
};

#endif

