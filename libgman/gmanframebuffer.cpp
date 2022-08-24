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
#include "gmanframebuffer.h" /* Declaration Header */


/*
 * RenderMan API GMANFrameBuffer
 *
 */

// default constructor
GMANFrameBuffer::GMANFrameBuffer() : GMANBitmap() { 
  // by default do a bartlett 3x3
  filter = RiBoxFilter;
  filterWidth = 3;
  filterHeight = 3;
};

GMANFrameBuffer::GMANFrameBuffer(int width, int height, const GMANColor &background) : GMANBitmap(width, height, background) { 
  // by default do a bartlett 3x3
  filter = RiBoxFilter;
  filterWidth = 3;
  filterHeight = 3;
};


// default destructor 
GMANFrameBuffer::~GMANFrameBuffer() { 
};

// Set the type of filter used by the anti-aliasing code
RtVoid GMANFrameBuffer::setFilter(RtFilterFunc filt, int fwidth, int fheight) {
  filter = filt;
  filterWidth = fwidth;
  filterHeight = fheight;
}


// apply a filter windowed about x and y to achieve the super sampled
// pixel
GMANColor GMANFrameBuffer::getSuperSampledPixel(int x, int y) {

  GMANColor filterVal(0.0);
  for(int i=0; i<filterWidth; i++) {
    for(int j=0; j<filterHeight; j++) {

      GMANColor   pixelVal(getPixel(x+i, y+j));
      pixelVal.scale(filter(x+i, y+j, filterWidth, filterHeight));
      
      filterVal +=  pixelVal;
    }
  };

  // compute average.
  filterVal /= (GMANColor::ColorSampleType)(filterWidth*filterHeight);
  
  return filterVal;
};
