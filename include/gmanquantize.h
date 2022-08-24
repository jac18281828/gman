/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2002, 2001, 2000, 1999 by John Cairns 
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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/
 

#ifndef __GMAN_GMANQUANTIZE_H
#define __GMAN_GMANQUANTIZE_H 1


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
// color object
#include "gmancolor.h"

/*
 * RenderMan API GMANQuantize
 *
 * Color depth and alpha quantization
 * 
 */

class GMANDLL  GMANQuantize : public UniversalSuperClass {
  public:
    // public types
    typedef enum { 
	RGB,
	RGBA,
	RGBAZ,
	A, 
	AZ,
	Z } DisplayMode;

  private:

    // private data
    DisplayMode mode;
    RtInt	one;
    RtInt	minVal;
    RtInt	maxVal;
    RtFloat     ditherAmplitude;
    

public:
    GMANQuantize(DisplayMode md,
		 RtInt oneMap,
		 RtInt mn,
		 RtInt mx,
		 RtFloat ditheramp); // default constructor

  ~GMANQuantize(); // default destructor


    // inline color reduction
    GMANColor &doColor(GMANColor &col);

    // inline color reduction
    GMANColorRGB &doColor(GMANColorRGB &col);
};


#endif

