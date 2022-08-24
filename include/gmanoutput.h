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
 

#ifndef __GMAN_GMANOUTPUT_H
#define __GMAN_GMANOUTPUT_H 1


/* Headers */

// STL
#include <string>
#if HAVE_STD_NAMESPACE
using std::string;
#endif

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// the frame buffer
#include "gmanframebuffer.h"

#include "gmanquantize.h"

#include "gmangamma.h"

/*
 * RenderMan API GMANOutput
 *
 * An interface for output of a frame buffer.
 * ... the frame buffer is detailed in the Renderer,
 * and its contents can be stored with an output
 *
 */

class GMANDLL  GMANOutput : public GMANFrameBuffer {
  public:

    // public types
    typedef enum { 
	RGB=GMANQuantize::RGB,
	RGBA=GMANQuantize::RGBA,
	RGBAZ=GMANQuantize::RGBAZ,
	A=GMANQuantize::A, 
	AZ=GMANQuantize::AZ,
	Z=GMANQuantize::Z } DisplayMode;
    
  protected:
    string	outputName;

    GMANQuantize	*quantizer;

    GMANGammaCorrect	gammaCorrect;

  public:

    GMANOutput();; // default constructor
  
    //name, width, height
    GMANOutput(const char *name, int width, int height);

    // path, width, height, background

    GMANOutput(const char *name, 
	       int width, 
	       int height, 
	       const GMANColor &background);

    virtual ~GMANOutput(); // default destructor

    // set the output device exposure control
    RtVoid setExposure(RtFloat gain, RtFloat gamma) {
	gammaCorrect.setExposure(gain, gamma);
    }

    // set the output name
    virtual RtVoid setName(const char *name) {
	outputName = name;
    };

    // call this to set up the quantization
    virtual RtVoid setQuantization(DisplayMode mode,
				   RtInt one,
				   RtInt min,
				   RtInt max,
				   RtFloat ditheramplitude);

  
    //
    // Copy or swap the frame buffer bank so the 
    // buffer data becomes visible.
    // or save the image data to the display device
    //

    virtual RtVoid save(DisplayMode mode, RtFloat gain, RtFloat gamma) = 0;

};


#endif

