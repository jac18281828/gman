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
 

#ifndef __GMAN_GMANFRAMEBUFFER_H
#define __GMAN_GMANFRAMEBUFFER_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// a bitmap object
#include "gmanbitmap.h"

// the filter object forward declaration
class GMANDLL GMANFilter;

/*
 * RenderMan API GMANFrameBuffer
 *
 * A matrix of color objects used to represent the frame buffer.
 *
 */

// support anti-aliasing
class GMANDLL  GMANFrameBuffer : public GMANBitmap {
private:
  int		filterWidth;
  int		filterHeight;

  RtFilterFunc   filter;
  
public:
  // default constructor
  GMANFrameBuffer(); 
  // construct based on a specified width/height and background
  GMANFrameBuffer(int width, int height, const GMANColor &background); 

  ~GMANFrameBuffer(); // default destructor

  // Set the type of filter used by the anti-aliasing code
  RtVoid setFilter(RtFilterFunc filter, int fwidth, int fheight);

  // get the type of filter used by the anti-aliasing code
  RtFilterFunc getFilter(RtVoid) {
    return filter;
  };
  
  // get a supersampled pixel from the frame buffer
  GMANColor GMANFrameBuffer::getSuperSampledPixel(int x, int y);

  // get the width of the filter in pixels
  
  int getFilterWidth(RtVoid) { return filterWidth; };

  // get the height of the filter in pixels
  int getFilterHeight(RtVoid) { return filterHeight; };

};


#endif

